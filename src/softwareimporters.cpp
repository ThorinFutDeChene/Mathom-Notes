/**
 * SPDX-FileCopyrightText: (C) 2003 Sébastien Laoût <slaout@linux62.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "softwareimporters.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QStack>
#include <QStandardPaths>
#include <QString>
#include <QStringDecoder>
#include <QTextStream>
#include <QVBoxLayout>
#include <QtXml/QDomDocument>

#include <KConfigGroup>
#include <KLocalizedString>
#include <KMessageBox>
#include <KTextEdit>

#include "basketfactory.h"
#include "basketscene.h"
#include "bnpview.h"
#include "debugwindow.h"
#include "global.h"
#include "note.h"
#include "notefactory.h"
#include "tools.h"
#include "xmlwork.h"

/** class TreeImportDialog: */

TreeImportDialog::TreeImportDialog(QWidget *parent)
    : QDialog(parent)
{
    auto *page = new QWidget(this);
    auto *topLayout = new QVBoxLayout(page);

    // QDialog options
    setWindowTitle(i18n("Import Hierarchy"));

    auto *mainWidget = new QWidget(this);
    auto *mainLayout = new QVBoxLayout;
    setLayout(mainLayout);
    mainLayout->addWidget(mainWidget);

    setObjectName("ImportHeirachy");
    setModal(true);

    m_choices = new QGroupBox(i18n("How to Import the Mathoms?"), page);
    m_choiceLayout = new QVBoxLayout();
    m_choices->setLayout(m_choiceLayout);

    m_hierarchy_choice = new QRadioButton(i18n("&Keep original hierarchy (all mathoms in separate Mathom-Houses)"), m_choices);
    m_separate_baskets_choice = new QRadioButton(i18n("&First-level mathoms in separate Mathom-Houses"), m_choices);
    m_one_basket_choice = new QRadioButton(i18n("&All mathoms in one Mathom-House"), m_choices);

    m_hierarchy_choice->setChecked(true);
    m_choiceLayout->addWidget(m_hierarchy_choice);
    m_choiceLayout->addWidget(m_separate_baskets_choice);
    m_choiceLayout->addWidget(m_one_basket_choice);

    topLayout->addWidget(m_choices);
    topLayout->addStretch(10);

    mainLayout->addWidget(page);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton *okButton = buttonBox->button(QDialogButtonBox::Ok);
    okButton->setDefault(true);
    okButton->setShortcut(Qt::CTRL | Qt::Key_Return);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &TreeImportDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &TreeImportDialog::reject);
    mainLayout->addWidget(buttonBox);
}

TreeImportDialog::~TreeImportDialog() = default;

int TreeImportDialog::choice()
{
    if (m_hierarchy_choice->isChecked())
        return 1;
    else if (m_separate_baskets_choice->isChecked())
        return 2;
    else
        return 3;
}

/** class TextFileImportDialog: */

TextFileImportDialog::TextFileImportDialog(QWidget *parent, bool hasCurrentBasket)
    : QDialog(parent)
{
    setWindowTitle(i18n("Import Text File"));
    setObjectName("ImportTextFile");
    setModal(true);

    auto *mainLayout = new QVBoxLayout(this);

    m_choices = new QGroupBox(i18n("Format of the Text File"), this);
    mainLayout->addWidget(m_choices);
    m_choiceLayout = new QVBoxLayout(m_choices);

    m_emptyline_choice = new QRadioButton(i18n("Mathoms separated by an &empty line"), m_choices);
    m_newline_choice = new QRadioButton(i18n("One &mathom per line"), m_choices);
    m_dash_choice = new QRadioButton(i18n("Mathoms begin with a &dash (-)"), m_choices);
    m_star_choice = new QRadioButton(i18n("Mathoms begin with a &star (*)"), m_choices);
    m_anotherSeparator = new QRadioButton(i18n("&Use another separator:"), m_choices);
    m_all_in_one_choice = new QRadioButton(i18n("&All in one mathom"), m_choices);

    m_choiceLayout->addWidget(m_emptyline_choice);
    m_choiceLayout->addWidget(m_newline_choice);
    m_choiceLayout->addWidget(m_dash_choice);
    m_choiceLayout->addWidget(m_star_choice);
    m_choiceLayout->addWidget(m_anotherSeparator);

    auto *indentedTextEdit = new QWidget(m_choices);
    m_choiceLayout->addWidget(indentedTextEdit);
    auto *hLayout = new QHBoxLayout(indentedTextEdit);
    hLayout->addSpacing(20);
    m_customSeparator = new KTextEdit(indentedTextEdit);
    hLayout->addWidget(m_customSeparator);

    m_choiceLayout->addWidget(m_all_in_one_choice);
    m_emptyline_choice->setChecked(true);
    connect(m_customSeparator, &KTextEdit::textChanged,
            this, &TextFileImportDialog::customSeparatorChanged);

    auto *destinationGroup = new QGroupBox(i18n("Import destination"), this);
    mainLayout->addWidget(destinationGroup);
    auto *destinationLayout = new QVBoxLayout(destinationGroup);
    m_destinationChoice = new QComboBox(destinationGroup);
    destinationLayout->addWidget(m_destinationChoice);

    if (hasCurrentBasket) {
        m_destinationChoice->addItem(i18n("Current Page"), static_cast<int>(Destination::CurrentPage));
        m_destinationChoice->addItem(i18n("New Page in the current location"), static_cast<int>(Destination::NewPage));
    }
    m_destinationChoice->addItem(i18n("New Mathom-House"), static_cast<int>(Destination::NewHouse));
    // Keep the legacy destination as the default to avoid surprise imports.
    m_destinationChoice->setCurrentIndex(m_destinationChoice->count() - 1);

    auto *encodingGroup = new QGroupBox(i18n("Text encoding"), this);
    mainLayout->addWidget(encodingGroup);
    auto *encodingLayout = new QVBoxLayout(encodingGroup);
    m_encodingChoice = new QComboBox(encodingGroup);
    encodingLayout->addWidget(m_encodingChoice);
    m_encodingChoice->addItem(i18n("Automatic (Unicode BOM, otherwise UTF-8)"), static_cast<int>(Encoding::Auto));
    m_encodingChoice->addItem(QStringLiteral("UTF-8"), static_cast<int>(Encoding::Utf8));
    m_encodingChoice->addItem(QStringLiteral("ISO-8859-1 (Latin-1)"), static_cast<int>(Encoding::Latin1));

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton *okButton = buttonBox->button(QDialogButtonBox::Ok);
    okButton->setDefault(true);
    okButton->setShortcut(Qt::CTRL | Qt::Key_Return);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &TextFileImportDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &TextFileImportDialog::reject);
    mainLayout->addWidget(buttonBox);
}

TextFileImportDialog::~TextFileImportDialog() = default;

QString TextFileImportDialog::separator()
{
    if (m_emptyline_choice->isChecked())
        return QStringLiteral("\n\n");
    else if (m_newline_choice->isChecked())
        return QStringLiteral("\n");
    else if (m_dash_choice->isChecked())
        return QStringLiteral("\n-");
    else if (m_star_choice->isChecked())
        return QStringLiteral("\n*");
    else if (m_anotherSeparator->isChecked())
        return m_customSeparator->toPlainText();
    else if (m_all_in_one_choice->isChecked())
        return {};
    else
        return QStringLiteral("\n\n");
}

bool TextFileImportDialog::customSeparatorSelected() const
{
    return m_anotherSeparator->isChecked();
}

TextFileImportDialog::Destination TextFileImportDialog::destination() const
{
    return static_cast<Destination>(m_destinationChoice->currentData().toInt());
}

TextFileImportDialog::Encoding TextFileImportDialog::encoding() const
{
    return static_cast<Encoding>(m_encodingChoice->currentData().toInt());
}

void TextFileImportDialog::customSeparatorChanged()
{
    if (!m_anotherSeparator->isChecked())
        m_anotherSeparator->toggle();
}

/** namespace SoftwareImporters: */

void SoftwareImporters::finishImport(BasketScene *basket)
{
    // Unselect the last inserted group:
    basket->unselectAll();

    // Focus the FIRST note (the last inserted note is currently focused!):
    basket->setFocusedNote(basket->firstNoteShownInStack());

    // Relayout every notes at their new place and simulate a load animation (because already loaded just after the creation).
    // Without a relayouting, notes on the bottom would comes from the top (because they were inserted on top) and clutter the animation load:
    basket->relayoutNotes();
    basket->save();
}

namespace
{
QStringList splitImportedText(const QString &content, const QString &separator)
{
    // Recognize empty lines containing spaces or tabs, not only two bare LFs.
    if (separator == QStringLiteral("\n\n")) {
        return content.split(QRegularExpression(QStringLiteral("\n[ \t]*\n+")));
    }

    // Keep the bullet characters themselves. The former importer removed
    // the leading '-' or '*' on all items except the first.
    if (separator == QStringLiteral("\n-") || separator == QStringLiteral("\n*")) {
        const QChar marker = separator.at(1);
        QStringList result;
        QString current;
        const QStringList lines = content.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
        for (const QString &line : lines) {
            if (line.startsWith(marker) && !current.trimmed().isEmpty()) {
                result.append(current);
                current.clear();
            }
            if (!current.isEmpty())
                current.append(QLatin1Char('\n'));
            current.append(line);
        }
        if (!current.isEmpty())
            result.append(current);
        return result;
    }

    return separator.isEmpty() ? QStringList{content} : content.split(separator);
}
}

void SoftwareImporters::importTextFile()
{
    const QString fileName = QFileDialog::getOpenFileName(
        Global::bnpView,
        i18n("Import Text File"),
        QString(),
        i18n("Text files (*.txt *.text *.md *.log *.csv);;All files (*)"));
    if (fileName.isEmpty())
        return;

    BasketScene *currentBasket = Global::bnpView->currentBasket();
    if (currentBasket && !currentBasket->isLoaded())
        currentBasket->load();

    const bool mayUseCurrentBasket = currentBasket && !currentBasket->isLocked();
    TextFileImportDialog dialog(Global::bnpView, mayUseCurrentBasket);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString separator = dialog.separator();
    if (dialog.customSeparatorSelected() && separator.isEmpty()) {
        KMessageBox::error(Global::bnpView, i18n("Enter a non-empty custom separator."));
        return;
    }

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        KMessageBox::error(Global::bnpView,
                           i18n("Cannot read %1: %2", fileName, file.errorString()));
        return;
    }
    const QByteArray bytes = file.readAll();
    if (file.error() != QFile::NoError) {
        KMessageBox::error(Global::bnpView,
                           i18n("Cannot read %1: %2", fileName, file.errorString()));
        return;
    }

    QStringConverter::Encoding encoding = QStringConverter::Utf8;
    if (dialog.encoding() == TextFileImportDialog::Encoding::Latin1) {
        encoding = QStringConverter::Latin1;
    } else if (dialog.encoding() == TextFileImportDialog::Encoding::Auto) {
        // Check UTF-32 signatures before UTF-16: both can start with FF FE.
        if (bytes.startsWith(QByteArray::fromHex("fffe0000")))
            encoding = QStringConverter::Utf32LE;
        else if (bytes.startsWith(QByteArray::fromHex("0000feff")))
            encoding = QStringConverter::Utf32BE;
        else if (bytes.startsWith("\xFF\xFE"))
            encoding = QStringConverter::Utf16LE;
        else if (bytes.startsWith("\xFE\xFF"))
            encoding = QStringConverter::Utf16BE;
    }

    QStringDecoder decoder(encoding, QStringConverter::Flag::Stateless);
    QString content = decoder(bytes);
    if (decoder.hasError() || content.contains(QChar::Null)) {
        KMessageBox::error(Global::bnpView,
            i18n("The text file contains invalid characters for the selected encoding. "
                 "Try another encoding or check that this is a text file."));
        return;
    }

    // Normalize Windows and classic Mac line endings before splitting.
    content.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    content.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    QStringList parts = splitImportedText(content, separator);
    parts.removeIf([](const QString &part) { return part.trimmed().isEmpty(); });
    if (parts.isEmpty()) {
        KMessageBox::information(Global::bnpView, i18n("The selected file contains no importable text."));
        return;
    }

    BasketScene *basket = currentBasket;
    if (dialog.destination() == TextFileImportDialog::Destination::NewHouse) {
        const QString title = i18nc("From TextFile.txt", "From %1", QFileInfo(fileName).fileName());
        basket = BasketFactory::newBasket(QStringLiteral("txt"), title);
        if (!basket)
            return;
        basket->load();
    } else if (!basket || basket->isLocked()) {
        KMessageBox::error(Global::bnpView, i18n("The target location is not available or is locked."));
        return;
    }

    if (dialog.destination() == TextFileImportDialog::Destination::NewPage) {
        const QString newPageId = basket->createPage();
        if (newPageId.isEmpty()) {
            KMessageBox::error(Global::bnpView, i18n("Cannot create a new Page for the import."));
            return;
        }
        basket->renamePage(newPageId, QFileInfo(fileName).completeBaseName());
    }

    // A new import destination always starts with a single column, even
    // when its Page would otherwise inherit a free or multi-column layout.
    // setCurrentPageDisposition creates the initial Page when a new House
    // has none yet. Never change the layout of an existing destination Page.
    if (dialog.destination() != TextFileImportDialog::Destination::CurrentPage) {
        basket->setCurrentPageDisposition(/*freeLayout=*/false, /*columnCount=*/1);
    }

    Note *first = nullptr;
    Note *last = nullptr;
    int imported = 0;
    bool partialFailure = false;
    for (const QString &part : parts) {
        // Explicit plain text: do not treat #RGB, URLs or markup as other Mathom types.
        Note *note = NoteFactory::createNoteText(part, basket, /*reallyPlainText=*/true);
        if (!note) {
            partialFailure = true;
            break;
        }
        if (last) {
            last->setNext(note);
            note->setPrev(last);
        } else {
            first = note;
        }
        last = note;
        ++imported;
    }

    if (first) {
        // One insertion assigns the active Page to the entire linked list.
        basket->insertCreatedNote(first);
        basket->unselectAll();
        basket->setFocusedNote(first);
        basket->relayoutNotes();
        basket->save();
    }

    if (partialFailure) {
        KMessageBox::error(Global::bnpView,
            i18n("The import was interrupted: %1 of %2 Mathoms were created. "
                 "Check available disk space before retrying.", imported, parts.size()));
    } else {
        KMessageBox::information(Global::bnpView,
            i18n("Imported %1 Mathoms from %2.", imported, QFileInfo(fileName).fileName()));
    }
}

#include "moc_softwareimporters.cpp"
