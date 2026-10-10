/*
 * Mathom Notes: guarded cross-session undo history.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Journal version 1 deliberately supports text edits and ordinary Mathom
 * deletions. Unrecognized commands form a history barrier: older commands are
 * not resurrected because their original context could no longer be proven.
 * No encrypted document is copied into this unencrypted journal.
 */
#include "persistenthistory.h"

#include "basketscene.h"
#include "bnpview.h"
#include "global.h"
#include "history.h"
#include "note.h"
#include "notecontent.h"
#include "notefactory.h"
#include "tag.h"
#include "xmlwork.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUndoStack>
#include <QXmlStreamWriter>
#include <QtGlobal>

namespace {
constexpr int JournalSchema = 1;
constexpr int MaxPerDocument = 20;
constexpr qint64 MaxFileBytes = 32LL * 1024 * 1024;
constexpr qint64 MaxJournalBytes = 96LL * 1024 * 1024;
constexpr qint64 MaxAgeMilliseconds = 24LL * 60 * 60 * 1000;

QString journalPath()
{
    // Never put deleted content in the version-control repository or backups.
    // One private journal per actual Mathom profile, including custom folders.
    const QString directory = QStandardPaths::writableLocation(
        QStandardPaths::GenericDataLocation) + QStringLiteral("/mathom-undo/");
    QDir().mkpath(directory);
    const QByteArray profileKey = QCryptographicHash::hash(
        QDir(Global::savesFolder()).absolutePath().toUtf8(),
        QCryptographicHash::Sha256).toHex();
    return directory + QString::fromLatin1(profileKey) + QStringLiteral(".json");
}

QString identifier(Note *note)
{
    return note ? note->uuid() : QString();
}

bool safeRelativePath(const QString &name)
{
    if (name.isEmpty() || QDir::isAbsolutePath(name) || name.contains(QLatin1Char('\\')))
        return false;
    const QString cleaned = QDir::cleanPath(name);
    return cleaned == name && cleaned != QStringLiteral("..")
        && !cleaned.startsWith(QStringLiteral("../"));
}

bool fileContents(const QString &path, QByteArray *data, qint64 maxLength)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maxLength)
        return false;
    *data = file.readAll();
    return data->size() == file.size();
}

// Fingerprint the structural XML plus the content actually referenced by
// that XML. Detached deleted Mathoms are excluded (they live in the journal).
QString documentFingerprint(BasketScene *basket)
{
    if (!basket || basket->isEncrypted())
        return {};

    QByteArray xml;
    if (!fileContents(basket->fullPath() + QStringLiteral(".basket"), &xml, MaxJournalBytes))
        return {};

    QDomDocument document;
    if (!document.setContent(xml))
        return {};

    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(xml);

    const QSet<QString> backedTypes = {
        QStringLiteral("text"), QStringLiteral("html"), QStringLiteral("image"),
        QStringLiteral("animation"), QStringLiteral("sound"), QStringLiteral("file"),
        QStringLiteral("launcher"), QStringLiteral("spreadsheet"),
        QStringLiteral("unknown")
    };

    QSet<QString> referenced;
    const QDomNodeList notes = document.elementsByTagName(QStringLiteral("note"));
    for (int i = 0; i < notes.count(); ++i) {
        const QDomElement element = notes.at(i).toElement();
        if (!backedTypes.contains(element.attribute(QStringLiteral("type"))))
            continue;
        const QDomElement content = element.firstChildElement(QStringLiteral("content"));
        if (content.isNull())
            continue;
        const QString name = content.text();
        if (!safeRelativePath(name))
            return {};
        referenced.insert(name);
    }

    QStringList names = referenced.values();
    names.sort();
    for (const QString &name : names) {
        QByteArray bytes;
        if (!fileContents(QDir(basket->fullPath()).filePath(name), &bytes,
                          MaxJournalBytes))
            return {};
        hash.addData(name.toUtf8());
        hash.addData(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256));
    }
    return QString::fromLatin1(hash.result().toHex());
}

void visitNotes(Note *first, QHash<QString, Note *> *nodes, bool *valid)
{
    for (Note *note = first; note; note = note->next()) {
        const QString id = note->uuid();
        if (id.isEmpty() || nodes->contains(id)) {
            *valid = false;
            return;
        }
        nodes->insert(id, note);
        visitNotes(note->firstChild(), nodes, valid);
        if (!*valid)
            return;
    }
}

QJsonObject snapshotNote(Note *note)
{
    if (!note || !note->content())
        return {};

    QJsonObject json;
    json.insert(QStringLiteral("uuid"), note->uuid());
    json.insert(QStringLiteral("page"), note->pageId());
    json.insert(QStringLiteral("type"), note->content()->lowerTypeName());
    json.insert(QStringLiteral("added"), note->addedDate().toString(Qt::ISODate));
    json.insert(QStringLiteral("modified"), note->lastModificationDate().toString(Qt::ISODate));
    json.insert(QStringLiteral("x"), static_cast<double>(note->x()));
    json.insert(QStringLiteral("y"), static_cast<double>(note->y()));

    QString contentXml;
    QXmlStreamWriter writer(&contentXml);
    writer.writeStartElement(QStringLiteral("wrapper"));
    note->content()->saveToNode(writer);
    writer.writeEndElement();
    json.insert(QStringLiteral("xml"), contentXml);

    QJsonArray tags;
    for (State *state : note->states()) {
        if (state)
            tags.append(state->id());
    }
    json.insert(QStringLiteral("tags"), tags);

    if (note->content()->useFile()) {
        const QString name = note->content()->fileName();
        QByteArray bytes;
        if (!safeRelativePath(name)
            || !fileContents(note->content()->fullPath(), &bytes, MaxFileBytes))
            return {};
        json.insert(QStringLiteral("fileName"), name);
        json.insert(QStringLiteral("fileBytes"), QString::fromLatin1(bytes.toBase64()));
        json.insert(QStringLiteral("fileHash"), QString::fromLatin1(
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
    }
    return json;
}

bool validSnapshot(const QJsonObject &snapshot)
{
    if (snapshot.value(QStringLiteral("uuid")).toString().isEmpty()
        || snapshot.value(QStringLiteral("type")).toString().isEmpty())
        return false;

    QDomDocument doc;
    if (!doc.setContent(snapshot.value(QStringLiteral("xml")).toString())
        || doc.documentElement().firstChildElement(QStringLiteral("content")).isNull())
        return false;

    if (snapshot.contains(QStringLiteral("fileName"))) {
        const QString name = snapshot.value(QStringLiteral("fileName")).toString();
        const QByteArray encoded = snapshot.value(QStringLiteral("fileBytes")).toString().toLatin1();
        if (!safeRelativePath(name)
            || encoded.size() > (MaxFileBytes * 4 / 3 + 8))
            return false;
        const QByteArray bytes = QByteArray::fromBase64(encoded);
        if (bytes.size() > MaxFileBytes
            || QString::fromLatin1(QCryptographicHash::hash(
                   bytes, QCryptographicHash::Sha256).toHex())
                != snapshot.value(QStringLiteral("fileHash")).toString())
            return false;
    }
    return true;
}

// Restore a suspended Mathom from the journal without inserting it in a Page.
// It becomes visible only when the user invokes Undo.
Note *createSuspendedNote(BasketScene *basket, const QJsonObject &snapshot)
{
    if (!basket || !validSnapshot(snapshot))
        return nullptr;

    if (snapshot.contains(QStringLiteral("fileName"))) {
        const QString fileName = snapshot.value(QStringLiteral("fileName")).toString();
        const QByteArray bytes = QByteArray::fromBase64(
            snapshot.value(QStringLiteral("fileBytes")).toString().toLatin1());
        const QString path = QDir(basket->fullPath()).filePath(fileName);
        QByteArray existing;
        if (QFileInfo::exists(path)) {
            if (!fileContents(path, &existing, MaxFileBytes) || existing != bytes)
                return nullptr;
        } else {
            if (!QDir().mkpath(QFileInfo(path).absolutePath()))
                return nullptr;
            QSaveFile output(path);
            if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size()
                || !output.commit())
                return nullptr;
        }
    }

    QDomDocument dom;
    if (!dom.setContent(snapshot.value(QStringLiteral("xml")).toString()))
        return nullptr;

    auto *note = new Note(basket);
    note->setUuid(snapshot.value(QStringLiteral("uuid")).toString());
    note->setPageId(snapshot.value(QStringLiteral("page")).toString());
    NoteFactory::loadNode(dom.documentElement().firstChildElement(QStringLiteral("content")),
                          snapshot.value(QStringLiteral("type")).toString(), note, false);
    if (!note->content()) {
        delete note;
        return nullptr;
    }
    note->setAddedDate(QDateTime::fromString(
        snapshot.value(QStringLiteral("added")).toString(), Qt::ISODate));
    note->setLastModificationDate(QDateTime::fromString(
        snapshot.value(QStringLiteral("modified")).toString(), Qt::ISODate));
    note->setX(snapshot.value(QStringLiteral("x")).toDouble());
    note->setY(snapshot.value(QStringLiteral("y")).toDouble());
    for (const QJsonValue &tagId : snapshot.value(QStringLiteral("tags")).toArray()) {
        if (State *state = Tag::stateById(tagId.toString()))
            note->addState(state, true);
    }
    note->setVisible(false);
    return note;
}

QJsonObject positionInfo(const DeletedMathomPosition &p)
{
    QJsonObject json;
    json.insert(QStringLiteral("uuid"), identifier(p.note));
    json.insert(QStringLiteral("parent"), identifier(p.parent));
    json.insert(QStringLiteral("previous"), identifier(p.previous));
    json.insert(QStringLiteral("next"), identifier(p.next));
    return json;
}

} // namespace

// The friend declarations in history.h authorize exact reconstruction of the
// existing undo commands. No duplicate undo engine is introduced.
bool PersistentHistory::isRecovered(const QUndoCommand *command)
{
    if (const auto *text = dynamic_cast<const MathomTextEditCommand *>(command))
        return text->m_restoredOrigin;
    if (const auto *deleted = dynamic_cast<const MathomDeleteCommand *>(command))
        return deleted->m_restoredOrigin;
    return false;
}

bool PersistentHistory::save(BNPView *view)
{
    if (!view || !view->globalUndoStack())
        return false;
    const QUndoStack *stack = view->globalUndoStack();
    QJsonArray operations;
    QSet<QString> documentNames;
    QHash<QString, int> perDocument;
    int first = stack->count();

    for (int i = stack->count() - 1; i >= 0; --i) {
        const QUndoCommand *command = stack->command(i);
        QJsonObject record;
        BasketScene *basket = nullptr;

        if (const auto *deleteCommand = dynamic_cast<const MathomDeleteCommand *>(command)) {
            basket = deleteCommand->m_basket;
            record.insert(QStringLiteral("type"), QStringLiteral("delete"));
            QJsonArray positions;
            for (const DeletedMathomPosition &p : deleteCommand->m_positions) {
                if (!p.note) {
                    positions = {};
                    break;
                }
                QJsonObject position = positionInfo(p);
                if (deleteCommand->m_deleted) {
                    const QJsonObject snapshot = snapshotNote(p.note);
                    if (snapshot.isEmpty()) {
                        positions = {};
                        break;
                    }
                    position.insert(QStringLiteral("snapshot"), snapshot);
                }
                positions.append(position);
            }
            if (positions.isEmpty())
                break;
            record.insert(QStringLiteral("positions"), positions);
        } else if (const auto *edit = dynamic_cast<const MathomTextEditCommand *>(command)) {
            basket = edit->m_basket;
            if (!edit->m_note)
                break;
            record.insert(QStringLiteral("type"), QStringLiteral("text"));
            record.insert(QStringLiteral("uuid"), edit->m_note->uuid());
            record.insert(QStringLiteral("old"), edit->m_oldState);
            record.insert(QStringLiteral("new"), edit->m_newState);
            record.insert(QStringLiteral("rich"), edit->m_richText);
            record.insert(QStringLiteral("newMathom"), edit->m_newMathom);
            record.insert(QStringLiteral("oldEmpty"), edit->m_oldStateWasEmpty);
            record.insert(QStringLiteral("suspended"), edit->m_mathomSuspended);
            record.insert(QStringLiteral("parent"), identifier(edit->m_parentNote));
            record.insert(QStringLiteral("previous"), identifier(edit->m_previousNote));
            record.insert(QStringLiteral("next"), identifier(edit->m_nextNote));
            if (edit->m_mathomSuspended) {
                const QJsonObject snapshot = snapshotNote(edit->m_note);
                if (snapshot.isEmpty())
                    break;
                record.insert(QStringLiteral("snapshot"), snapshot);
            }
        } else {
            // A command of another type is a chronological integrity barrier.
            // Never jump past it to replay older operations out of context.
            break;
        }

        if (!basket || basket->isEncrypted())
            break;
        const QString documentName = basket->folderName();
        if (documentName.isEmpty() || perDocument.value(documentName) >= MaxPerDocument)
            break;
        record.insert(QStringLiteral("basket"), documentName);
        record.insert(QStringLiteral("label"), command->text());
        record.insert(QStringLiteral("applied"), i < stack->index());
        operations.prepend(record);
        documentNames.insert(documentName);
        ++perDocument[documentName];
        first = i;
    }

    if (operations.isEmpty()) {
        QFile::remove(journalPath());
        return true;
    }

    QJsonObject hashes;
    for (const QString &name : documentNames) {
        BasketScene *basket = view->basketForFolderName(name);
        const QString fingerprint = documentFingerprint(basket);
        if (fingerprint.isEmpty()) {
            QFile::remove(journalPath());
            return false;
        }
        hashes.insert(name, fingerprint);
    }

    QJsonObject journal;
    journal.insert(QStringLiteral("schema"), JournalSchema);
    journal.insert(QStringLiteral("timestampMs"),
                   static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
    journal.insert(QStringLiteral("index"), stack->index() - first);
    journal.insert(QStringLiteral("hashes"), hashes);
    journal.insert(QStringLiteral("commands"), operations);

    const QByteArray bytes = QJsonDocument(journal).toJson(QJsonDocument::Compact);
    if (bytes.size() > MaxJournalBytes)
        return false;

    QSaveFile output(journalPath());
    if (!output.open(QIODevice::WriteOnly))
        return false;
    output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return output.write(bytes) == bytes.size() && output.commit();
}

int PersistentHistory::restore(BNPView *view)
{
    if (!view || !view->globalUndoStack() || view->globalUndoStack()->count())
        return 0;

    QByteArray bytes;
    if (!fileContents(journalPath(), &bytes, MaxJournalBytes))
        return 0;
    QJsonParseError error;
    const QJsonDocument parsed = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !parsed.isObject())
        return 0;
    const QJsonObject journal = parsed.object();
    if (journal.value(QStringLiteral("schema")).toInt() != JournalSchema)
        return 0;
    const qint64 savedAt = static_cast<qint64>(
        journal.value(QStringLiteral("timestampMs")).toDouble());
    const qint64 age = QDateTime::currentMSecsSinceEpoch() - savedAt;
    if (savedAt <= 0 || age < 0 || age > MaxAgeMilliseconds) {
        QFile::remove(journalPath());
        return 0;
    }

    const QJsonArray operations = journal.value(QStringLiteral("commands")).toArray();
    if (operations.isEmpty() || operations.size() > 100)
        return 0;
    const int recordedIndex = journal.value(QStringLiteral("index")).toInt(-1);
    if (recordedIndex < 0 || recordedIndex > operations.size())
        return 0;

    const QJsonObject hashes = journal.value(QStringLiteral("hashes")).toObject();
    QHash<QString, BasketScene *> baskets;
    QHash<QString, QHash<QString, Note *>> nodes;
    QHash<QString, int> perDocument;
    for (auto it = hashes.constBegin(); it != hashes.constEnd(); ++it) {
        BasketScene *basket = view->basketForFolderName(it.key());
        if (!basket || basket->isEncrypted())
            return 0;
        if (!basket->isLoaded())
            basket->load();
        if (documentFingerprint(basket) != it.value().toString())
            return -1; // The user document changed since the history checkpoint.
        baskets.insert(it.key(), basket);
        bool valid = true;
        visitNotes(basket->firstNote(), &nodes[it.key()], &valid);
        if (!valid)
            return 0;
    }

    // Validate all records before materializing any suspended note or file.
    for (int i = 0; i < operations.size(); ++i) {
        const QJsonObject record = operations.at(i).toObject();
        const QString name = record.value(QStringLiteral("basket")).toString();
        if (!baskets.contains(name) || ++perDocument[name] > MaxPerDocument)
            return 0;
        if (record.value(QStringLiteral("applied")).toBool() != (i < recordedIndex))
            return 0;
        const QString type = record.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("delete")) {
            const QJsonArray positions = record.value(QStringLiteral("positions")).toArray();
            if (positions.isEmpty())
                return 0;
            for (const QJsonValue &v : positions) {
                const QJsonObject position = v.toObject();
                if (position.value(QStringLiteral("uuid")).toString().isEmpty())
                    return 0;
                if (record.value(QStringLiteral("applied")).toBool()) {
                    const QJsonObject snapshot = position.value(QStringLiteral("snapshot")).toObject();
                    if (!validSnapshot(snapshot)
                        || snapshot.value(QStringLiteral("uuid")).toString()
                            != position.value(QStringLiteral("uuid")).toString())
                        return 0;
                }
            }
        } else if (type == QStringLiteral("text")) {
            if (record.value(QStringLiteral("uuid")).toString().isEmpty())
                return 0;
            if (record.value(QStringLiteral("suspended")).toBool()
                && !validSnapshot(record.value(QStringLiteral("snapshot")).toObject()))
                return 0;
        } else {
            return 0;
        }
    }

    // Pass 1: reconstruct detached Mathoms and their associated file content.
    for (const QJsonValue &value : operations) {
        const QJsonObject record = value.toObject();
        const QString name = record.value(QStringLiteral("basket")).toString();
        const QString type = record.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("delete")
            && record.value(QStringLiteral("applied")).toBool()) {
            for (const QJsonValue &entry : record.value(QStringLiteral("positions")).toArray()) {
                const QJsonObject snapshot = entry.toObject().value(QStringLiteral("snapshot")).toObject();
                const QString id = snapshot.value(QStringLiteral("uuid")).toString();
                if (nodes[name].contains(id))
                    return 0;
                Note *note = createSuspendedNote(baskets.value(name), snapshot);
                if (!note)
                    return 0;
                nodes[name].insert(id, note);
            }
        } else if (type == QStringLiteral("text")
                   && record.value(QStringLiteral("suspended")).toBool()) {
            const QJsonObject snapshot = record.value(QStringLiteral("snapshot")).toObject();
            const QString id = snapshot.value(QStringLiteral("uuid")).toString();
            if (nodes[name].contains(id))
                continue; // A later deletion may already have materialized it.
            Note *note = createSuspendedNote(baskets.value(name), snapshot);
            if (!note)
                return 0;
            nodes[name].insert(id, note);
        }
    }

    // All references must resolve before the first command is pushed.
    // An invalid anchor could otherwise restore Mathoms at the wrong place.
    for (const QJsonValue &value : operations) {
        const QJsonObject record = value.toObject();
        const auto &map = nodes[record.value(QStringLiteral("basket")).toString()];
        const QString type = record.value(QStringLiteral("type")).toString();
        auto validRef = [&](const QJsonObject &object, const QString &key) {
            const QString id = object.value(key).toString();
            return id.isEmpty() || map.contains(id);
        };
        if (type == QStringLiteral("delete")) {
            for (const QJsonValue &entry : record.value(QStringLiteral("positions")).toArray()) {
                const QJsonObject p = entry.toObject();
                if (!map.contains(p.value(QStringLiteral("uuid")).toString())
                    || !validRef(p, QStringLiteral("parent"))
                    || !validRef(p, QStringLiteral("previous"))
                    || !validRef(p, QStringLiteral("next")))
                    return 0;
            }
        } else if (!map.contains(record.value(QStringLiteral("uuid")).toString())
                   || !validRef(record, QStringLiteral("parent"))
                   || !validRef(record, QStringLiteral("previous"))
                   || !validRef(record, QStringLiteral("next"))) {
            return 0;
        }
    }

    // Pass 2: reconstruct the original QUndoCommand classes, never replacing
    // the document tree or swapping the user's data directory.
    QUndoStack *stack = view->globalUndoStack();
    for (const QJsonValue &value : operations) {
        const QJsonObject record = value.toObject();
        const QString name = record.value(QStringLiteral("basket")).toString();
        BasketScene *basket = baskets.value(name);
        auto &map = nodes[name];
        const bool applied = record.value(QStringLiteral("applied")).toBool();
        if (record.value(QStringLiteral("type")).toString() == QStringLiteral("delete")) {
            QList<DeletedMathomPosition> positions;
            for (const QJsonValue &v : record.value(QStringLiteral("positions")).toArray()) {
                const QJsonObject p = v.toObject();
                const QString uuid = p.value(QStringLiteral("uuid")).toString();
                if (!map.contains(uuid))
                    return stack->count();
                DeletedMathomPosition position;
                position.note = map.value(uuid);
                position.parent = map.value(p.value(QStringLiteral("parent")).toString());
                position.previous = map.value(p.value(QStringLiteral("previous")).toString());
                position.next = map.value(p.value(QStringLiteral("next")).toString());
                positions.append(position);
            }
            auto *command = new MathomDeleteCommand(basket, {});
            command->m_positions = positions;
            command->m_deleted = applied;
            command->m_firstRedo = true;
            command->m_skipRestoredUndo = !applied;
            command->m_restoredOrigin = true;
            command->setText(record.value(QStringLiteral("label")).toString());
            stack->push(command);
        } else {
            Note *note = map.value(record.value(QStringLiteral("uuid")).toString());
            if (!note)
                return stack->count();
            auto *command = new MathomTextEditCommand(
                note,
                record.value(QStringLiteral("old")).toString(),
                record.value(QStringLiteral("new")).toString(),
                record.value(QStringLiteral("rich")).toBool(),
                record.value(QStringLiteral("newMathom")).toBool(),
                record.value(QStringLiteral("oldEmpty")).toBool());
            command->m_mathomSuspended = record.value(QStringLiteral("suspended")).toBool();
            command->m_parentNote = map.value(record.value(QStringLiteral("parent")).toString());
            command->m_previousNote = map.value(record.value(QStringLiteral("previous")).toString());
            command->m_nextNote = map.value(record.value(QStringLiteral("next")).toString());
            command->m_skipRestoredUndo = !applied;
            command->m_restoredOrigin = true;
            command->setText(record.value(QStringLiteral("label")).toString());
            stack->push(command);
        }
    }

    // Set the saved undo/redo cursor without changing current document data.
    while (stack->index() > recordedIndex)
        stack->undo();
    return stack->count();
}
