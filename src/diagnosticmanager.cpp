/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "diagnosticmanager.h"

#include <KLocalizedString>
#include <QAbstractButton>
#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QWidget>

#ifdef Q_OS_LINUX
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace
{
constexpr auto sessionPrefix = "session-";
constexpr auto sessionSuffix = ".jsonl";
constexpr auto reportPrefix = "report-";
constexpr auto reportSuffix = ".txt";
constexpr int reportEventLimit = 400;

QString sessionStem(const QString &sessionPath)
{
    QString name = QFileInfo(sessionPath).fileName();
    if (name.startsWith(QLatin1String(sessionPrefix)))
        name.remove(0, QString::fromLatin1(sessionPrefix).size());
    if (name.endsWith(QLatin1String(sessionSuffix)))
        name.chop(QString::fromLatin1(sessionSuffix).size());
    return name;
}

QString safeObjectName(QObject *object)
{
    if (!object)
        return QString();

    const QString name = object->objectName().trimmed();
    if (name.isEmpty())
        return QStringLiteral("<unnamed>");
    return name.left(120);
}

QString fileSha256(const QString &path, int maxHexCharacters = 64)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("indisponible");

    QCryptographicHash hash(QCryptographicHash::Sha256);

    while (!file.atEnd()) {
        const QByteArray chunk = file.read(1024 * 1024);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError)
            return QStringLiteral("indisponible");
        hash.addData(chunk);
    }

    return QString::fromLatin1(
        hash.result().toHex().left(maxHexCharacters));
}

QString mathomRuntimeLibrary(const QString &fileName)
{
    const QFileInfo executable(
        QCoreApplication::applicationFilePath());

    return QDir::cleanPath(
        executable.dir().filePath(
            QStringLiteral("../lib/%1").arg(fileName)));
}

QString binaryBuildId()
{
    QCryptographicHash hash(QCryptographicHash::Sha256);

    const QStringList files = {
        QCoreApplication::applicationFilePath(),
        mathomRuntimeLibrary(QStringLiteral("libLibBasket.so.2"))
    };

    bool hashedSomething = false;

    for (const QString &path : files) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;

        hashedSomething = true;

        hash.addData(QFileInfo(path).fileName().toUtf8());
        hash.addData(QByteArray(1, '\0'));

        while (!file.atEnd()) {
            const QByteArray chunk = file.read(1024 * 1024);
            if (chunk.isEmpty()
                && file.error() != QFileDevice::NoError) {
                break;
            }
            hash.addData(chunk);
        }
    }

    if (!hashedSomething)
        return QStringLiteral("indisponible");

    return QStringLiteral("sha256:%1")
        .arg(QString::fromLatin1(
            hash.result().toHex().left(16)));
}

QString limitedOutput(QString text, int maximumCharacters = 24000)
{
    text = text.trimmed();

    if (text.size() <= maximumCharacters)
        return text;

    return text.left(maximumCharacters)
        + QStringLiteral(
            "\n...[sortie tronquee automatiquement par Mathom]...");
}

QString runDiagnosticCommand(
    const QString &program,
    const QStringList &arguments,
    int timeoutMs = 4000)
{
    QString executable = program;

    if (!program.contains(QLatin1Char('/')))
        executable = QStandardPaths::findExecutable(program);

    if (executable.isEmpty())
        return QStringLiteral("[commande indisponible : %1]")
            .arg(program);

    QProcess process;
    process.setProgram(executable);
    process.setArguments(arguments);
    process.start();

    if (!process.waitForStarted(1000)) {
        return QStringLiteral(
                   "[impossible de demarrer : %1]")
            .arg(program);
    }

    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);

        return QStringLiteral(
                   "[delai depasse pour : %1]")
            .arg(program);
    }

    QString result =
        QString::fromUtf8(process.readAllStandardOutput())
            .trimmed();

    const QString error =
        QString::fromUtf8(process.readAllStandardError())
            .trimmed();

    if (!error.isEmpty()) {
        if (!result.isEmpty())
            result += QLatin1Char('\n');
        result += error;
    }

    if (result.isEmpty())
        result = QStringLiteral("[aucune sortie]");

    if (process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != 0) {
        result.prepend(
            QStringLiteral("[code retour %1]\n")
                .arg(process.exitCode()));
    }

    return limitedOutput(result);
}

QString filteredJournal(const QString &journal, qint64 pid)
{
    const QString pidMarker =
        pid > 0
            ? QStringLiteral("[%1]").arg(pid)
            : QString();

    const QStringList keywords = {
        QStringLiteral("mathom"),
        QStringLiteral("apprun"),
        QStringLiteral("segfault"),
        QStringLiteral("sigsegv"),
        QStringLiteral("qt6"),
        QStringLiteral("glibc"),
        QStringLiteral("apport"),
        QStringLiteral("whoopsie"),
        QStringLiteral("core dumped"),
        QStringLiteral("signal 11"),
        QStringLiteral("trap")
    };

    QStringList kept;

    const QStringList lines =
        journal.split(QLatin1Char('\n'), Qt::SkipEmptyParts);

    for (const QString &line : lines) {
        const QString lower = line.toLower();

        bool relevant =
            !pidMarker.isEmpty()
            && line.contains(pidMarker);

        if (!relevant) {
            for (const QString &keyword : keywords) {
                if (lower.contains(keyword)) {
                    relevant = true;
                    break;
                }
            }
        }

        if (relevant)
            kept.append(line);
    }

    if (kept.isEmpty())
        return QStringLiteral(
            "[aucune ligne pertinente ou journal inaccessible]");

    return limitedOutput(kept.join(QLatin1Char('\n')));
}

QString readTextFileLimited(
    const QString &path,
    int maximumCharacters = 24000)
{
    QFile file(path);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();

    return limitedOutput(
        QString::fromUtf8(file.readAll()),
        maximumCharacters);
}

QString formatDuration(qint64 milliseconds)
{
    if (milliseconds < 0)
        return QStringLiteral("inconnue");

    qint64 seconds = milliseconds / 1000;
    const qint64 hours = seconds / 3600;
    seconds %= 3600;
    const qint64 minutes = seconds / 60;
    seconds %= 60;

    if (hours > 0)
        return QStringLiteral("%1 h %2 min %3 s").arg(hours).arg(minutes).arg(seconds);
    if (minutes > 0)
        return QStringLiteral("%1 min %2 s").arg(minutes).arg(seconds);
    return QStringLiteral("%1 s").arg(seconds);
}

QString humanEventName(const QString &event)
{
    if (event == QStringLiteral("MOUSE_PRESS"))
        return QStringLiteral("clic souris");
    if (event == QStringLiteral("KEY_PRESS"))
        return QStringLiteral("touche speciale");
    if (event == QStringLiteral("TEXT_INPUT"))
        return QStringLiteral("saisie de texte");
    if (event == QStringLiteral("SHELF_SWITCH_BEGIN"))
        return QStringLiteral("changement d'etagere commence");
    if (event == QStringLiteral("SHELF_SWITCH_OK"))
        return QStringLiteral("changement d'etagere termine");
    if (event == QStringLiteral("NEW_SHELF_DIALOG_BEGIN"))
        return QStringLiteral("ouverture de la creation d'etagere");
    if (event == QStringLiteral("NEW_SHELF_DIALOG_END"))
        return QStringLiteral("fermeture de la creation d'etagere");
    if (event == QStringLiteral("DELETE_SHELF_REQUEST"))
        return QStringLiteral("demande de suppression d'etagere");
    if (event == QStringLiteral("DELETE_SHELF_BEGIN"))
        return QStringLiteral("suppression d'etagere commencee");
    if (event == QStringLiteral("DELETE_SHELF_OK"))
        return QStringLiteral("suppression d'etagere terminee");
    if (event == QStringLiteral("FOCUS_MODE_TOGGLE_BEGIN"))
        return QStringLiteral("changement du mode concentration commence");
    if (event == QStringLiteral("FOCUS_MODE_TOGGLE_OK"))
        return QStringLiteral("changement du mode concentration termine");
    if (event == QStringLiteral("NEW_MATHOM_BEGIN"))
        return QStringLiteral("creation d'un Mathom commencee");
    if (event == QStringLiteral("NEW_MATHOM_OK"))
        return QStringLiteral("creation d'un Mathom terminee");
    if (event == QStringLiteral("MATHOM_EDIT_BEGIN"))
        return QStringLiteral("edition d'un Mathom commencee");
    if (event == QStringLiteral("MATHOM_EDIT_END"))
        return QStringLiteral("edition d'un Mathom terminee");
    if (event == QStringLiteral("MATHOM_EDIT_CLOSE_BEGIN"))
        return QStringLiteral("validation d'un Mathom commencee");
    if (event == QStringLiteral("MATHOM_EDIT_CLOSE_OK"))
        return QStringLiteral("validation d'un Mathom terminee");
    if (event == QStringLiteral("PASTE_BEGIN"))
        return QStringLiteral("collage commence");
    if (event == QStringLiteral("PASTE_OK"))
        return QStringLiteral("collage termine");
    if (event == QStringLiteral("SHELF_SAVE_BEGIN"))
        return QStringLiteral("sauvegarde d'etagere commencee");
    if (event == QStringLiteral("SHELF_SAVE_OK"))
        return QStringLiteral("sauvegarde d'etagere reussie");
    if (event == QStringLiteral("SHELF_SAVE_FAILED"))
        return QStringLiteral("sauvegarde d'etagere echouee");
    if (event == QStringLiteral("TREE_SAVE_BEGIN"))
        return QStringLiteral("sauvegarde de l'arborescence commencee");
    if (event == QStringLiteral("TREE_SAVE_OK"))
        return QStringLiteral("sauvegarde de l'arborescence reussie");
    if (event == QStringLiteral("HEARTBEAT"))
        return QStringLiteral("Mathom actif");
    return event;
}

QString operationBase(const QString &event)
{
    static const QStringList suffixes = {
        QStringLiteral("_BEGIN"),
        QStringLiteral("_OK"),
        QStringLiteral("_FAILED"),
        QStringLiteral("_END")
    };

    for (const QString &suffix : suffixes) {
        if (event.endsWith(suffix))
            return event.left(event.size() - suffix.size());
    }

    return QString();
}

bool isUserActionEvent(const QString &event)
{
    // Keep "user action" strictly limited to direct input events.
    // Business events (edit started, shelf switch, new Mathom, etc.) may be
    // triggered automatically by Mathom and must not be presented as a
    // deliberate user action in the report summary.
    return event == QStringLiteral("MOUSE_PRESS")
        || event == QStringLiteral("KEY_PRESS")
        || event == QStringLiteral("TEXT_INPUT");
}

QString detailsSummary(const QVariantMap &details)
{
    QStringList parts;

    if (details.contains(QStringLiteral("folder")))
        parts << QStringLiteral("dossier=%1").arg(details.value(QStringLiteral("folder")).toString());

    if (details.contains(QStringLiteral("class")))
        parts << QStringLiteral("zone=%1").arg(details.value(QStringLiteral("class")).toString());

    if (details.contains(QStringLiteral("object"))) {
        const QString object = details.value(QStringLiteral("object")).toString();
        if (!object.isEmpty() && object != QStringLiteral("<unnamed>"))
            parts << QStringLiteral("objet=%1").arg(object);
    }

    if (details.contains(QStringLiteral("x")) && details.contains(QStringLiteral("y")))
        parts << QStringLiteral("position=%1,%2")
                     .arg(details.value(QStringLiteral("x")).toInt())
                     .arg(details.value(QStringLiteral("y")).toInt());

    if (details.contains(QStringLiteral("target_class")))
        parts << QStringLiteral("zone=%1").arg(details.value(QStringLiteral("target_class")).toString());

    if (details.contains(QStringLiteral("characters")))
        parts << QStringLiteral("caracteres=%1").arg(details.value(QStringLiteral("characters")).toInt());

    if (details.contains(QStringLiteral("duration_ms")))
        parts << QStringLiteral("duree=%1 ms").arg(details.value(QStringLiteral("duration_ms")).toLongLong());

    if (details.contains(QStringLiteral("selected_count")))
        parts << QStringLiteral("selection=%1").arg(details.value(QStringLiteral("selected_count")).toInt());

    return parts.join(QStringLiteral(" ; "));
}

QString recordSummary(const QVariantMap &record)
{
    const QString timestamp = record.value(QStringLiteral("timestamp")).toString();
    const QString event = record.value(QStringLiteral("event")).toString();
    const QVariantMap details = record.value(QStringLiteral("details")).toMap();
    const QString detailText = detailsSummary(details);

    QString result = QStringLiteral("%1 - %2").arg(timestamp, humanEventName(event));
    if (!detailText.isEmpty())
        result += QStringLiteral(" (%1)").arg(detailText);
    return result;
}

struct ParsedSession
{
    QString sessionId;
    QString openedAt;
    QString lastTimestamp;
    QString lastEvent;
    QString lastUserAction;
    QString lastCompletedOperation;
    QString lastSave;
    QString version;
    QString qtVersion;
    QString os;
    QString kernel;
    QString architecture;
    QString buildId;
    qint64 pid = -1;
    qint64 uptimeMs = -1;
    qint64 lastMemoryKiB = -1;
    qint64 peakMemoryKiB = -1;
    qint64 measuredPeakMemoryKiB = -1;
    int eventCount = 0;
    QStringList activeOperations;
    QList<QByteArray> rawLines;
};

ParsedSession parseSession(QFile &input)
{
    ParsedSession summary;

    while (!input.atEnd()) {
        const QByteArray rawLine = input.readLine().trimmed();
        if (rawLine.isEmpty())
            continue;

        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(rawLine, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
            continue;

        const QVariantMap record = document.object().toVariantMap();
        const QString event = record.value(QStringLiteral("event")).toString();
        const QString timestamp = record.value(QStringLiteral("timestamp")).toString();
        const QVariantMap details = record.value(QStringLiteral("details")).toMap();

        summary.rawLines.append(rawLine);
        ++summary.eventCount;
        summary.lastTimestamp = timestamp;
        summary.lastEvent = recordSummary(record);
        summary.uptimeMs = qMax(summary.uptimeMs, record.value(QStringLiteral("uptime_ms"), -1).toLongLong());

        if (summary.sessionId.isEmpty())
            summary.sessionId = record.value(QStringLiteral("session")).toString();

        if (event == QStringLiteral("SESSION_OPEN")) {
            summary.openedAt = timestamp;
            summary.version = details.value(QStringLiteral("version")).toString();
            summary.qtVersion = details.value(QStringLiteral("qt_version")).toString();
            summary.os = details.value(QStringLiteral("os")).toString();
            summary.kernel = details.value(QStringLiteral("kernel")).toString();
            summary.architecture = details.value(QStringLiteral("architecture")).toString();
            summary.buildId = details.value(QStringLiteral("build_id")).toString();
            summary.pid = details.value(QStringLiteral("pid"), -1).toLongLong();
        }

        const qint64 rssKiB = details.value(QStringLiteral("rss_kib"), -1).toLongLong();
        if (rssKiB >= 0) {
            summary.lastMemoryKiB = rssKiB;
            summary.peakMemoryKiB = qMax(summary.peakMemoryKiB, rssKiB);
        }

        const qint64 peakRssKiB =
            details.value(QStringLiteral("peak_rss_kib"), -1).toLongLong();
        if (peakRssKiB >= 0)
            summary.measuredPeakMemoryKiB =
                qMax(summary.measuredPeakMemoryKiB, peakRssKiB);

        if (isUserActionEvent(event))
            summary.lastUserAction = recordSummary(record);

        if (event.endsWith(QStringLiteral("_OK"))
            || event.endsWith(QStringLiteral("_END"))) {
            summary.lastCompletedOperation = recordSummary(record);
        }

        if (event == QStringLiteral("SHELF_SAVE_OK")
            || event == QStringLiteral("TREE_SAVE_OK")) {
            summary.lastSave = QStringLiteral("OK - %1").arg(timestamp);
        } else if (event == QStringLiteral("SHELF_SAVE_FAILED")) {
            summary.lastSave = QStringLiteral("ECHEC - %1").arg(timestamp);
        }

        if (event.endsWith(QStringLiteral("_BEGIN"))) {
            const QString base = operationBase(event);
            if (!base.isEmpty())
                summary.activeOperations.append(base);
        } else if (event.endsWith(QStringLiteral("_OK"))
                   || event.endsWith(QStringLiteral("_FAILED"))
                   || event.endsWith(QStringLiteral("_END"))) {
            const QString base = operationBase(event);
            if (!base.isEmpty()) {
                const int index = summary.activeOperations.lastIndexOf(base);
                if (index >= 0)
                    summary.activeOperations.removeAt(index);
            }

            // Editing starts as MATHOM_EDIT_BEGIN and is completed by
            // MATHOM_EDIT_CLOSE_OK, so pair those two event families.
            if (event == QStringLiteral("MATHOM_EDIT_CLOSE_OK")
                || event == QStringLiteral("MATHOM_EDIT_CLOSE_FAILED")
                || event == QStringLiteral("MATHOM_EDIT_END")) {
                const int editIndex =
                    summary.activeOperations.lastIndexOf(QStringLiteral("MATHOM_EDIT"));
                if (editIndex >= 0)
                    summary.activeOperations.removeAt(editIndex);
            }
        }
    }

    return summary;
}
}

DiagnosticManager &DiagnosticManager::instance()
{
    static DiagnosticManager manager;
    return manager;
}

DiagnosticManager::DiagnosticManager() = default;

DiagnosticManager::~DiagnosticManager()
{
    closeSession();
}

QString DiagnosticManager::diagnosticsDirectory() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/diagnostics");
}

QString DiagnosticManager::currentSessionId() const
{
    return m_sessionId;
}

QStringList DiagnosticManager::pendingReports() const
{
    return m_pendingReports;
}

void DiagnosticManager::openDiagnosticsFolder()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(diagnosticsDirectory()));
}

qint64 DiagnosticManager::currentMemoryRssKiB() const
{
#ifdef Q_OS_LINUX
    QFile status(QStringLiteral("/proc/self/status"));
    if (status.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!status.atEnd()) {
            const QByteArray line = status.readLine();
            if (!line.startsWith("VmRSS:"))
                continue;

            const QList<QByteArray> fields = line.simplified().split(' ');
            if (fields.size() >= 2)
                return fields.at(1).toLongLong();
        }
    }

    QFile statm(QStringLiteral("/proc/self/statm"));
    if (statm.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QList<QByteArray> fields = statm.readAll().simplified().split(' ');
        if (fields.size() >= 2) {
            const qint64 residentPages = fields.at(1).toLongLong();
            const long pageSize = sysconf(_SC_PAGESIZE);
            if (residentPages >= 0 && pageSize > 0)
                return residentPages * pageSize / 1024;
        }
    }
#endif
    return -1;
}

void DiagnosticManager::startSession()
{
    if (m_started)
        return;

    QDir dir(diagnosticsDirectory());
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        return;

    inspectPreviousSessions();

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    m_sessionId = QStringLiteral("MTH-%1-%2").arg(stamp, uuid);
    m_sessionPath = dir.filePath(QStringLiteral("%1%2%3").arg(
        QLatin1String(sessionPrefix), m_sessionId, QLatin1String(sessionSuffix)));

    m_sessionFile = new QFile(m_sessionPath, this);
    if (!m_sessionFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        delete m_sessionFile;
        m_sessionFile = nullptr;
        return;
    }

    m_startedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_started = true;
    m_closed = false;

    m_textInputFlushTimer = new QTimer(this);
    m_textInputFlushTimer->setSingleShot(true);
    m_textInputFlushTimer->setInterval(800);
    connect(m_textInputFlushTimer, &QTimer::timeout, this, &DiagnosticManager::flushTextInput);

    qApp->installEventFilter(this);

    QVariantMap details;
    details.insert(QStringLiteral("version"), QCoreApplication::applicationVersion());
    details.insert(QStringLiteral("qt_version"), QString::fromLatin1(qVersion()));
    details.insert(QStringLiteral("os"), QSysInfo::prettyProductName());
    details.insert(QStringLiteral("kernel"), QStringLiteral("%1 %2").arg(QSysInfo::kernelType(), QSysInfo::kernelVersion()));
    details.insert(QStringLiteral("architecture"), QSysInfo::currentCpuArchitecture());
    details.insert(QStringLiteral("pid"), QCoreApplication::applicationPid());
    details.insert(QStringLiteral("build_id"), binaryBuildId());

    const qint64 rssKiB = currentMemoryRssKiB();
    if (rssKiB >= 0)
        details.insert(QStringLiteral("rss_kib"), rssKiB);

#ifdef Q_OS_LINUX
    struct rusage usage {};
    if (getrusage(RUSAGE_SELF, &usage) == 0)
        details.insert(QStringLiteral("peak_rss_kib"), qint64(usage.ru_maxrss));
#endif

    logEvent(QStringLiteral("SESSION_OPEN"), details);

    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(60000);
    connect(m_heartbeatTimer, &QTimer::timeout, this, [this]() {
        QVariantMap heartbeatDetails;
        const qint64 currentRssKiB = currentMemoryRssKiB();
        if (currentRssKiB >= 0)
            heartbeatDetails.insert(QStringLiteral("rss_kib"), currentRssKiB);

#ifdef Q_OS_LINUX
        struct rusage usage {};
        if (getrusage(RUSAGE_SELF, &usage) == 0)
            heartbeatDetails.insert(QStringLiteral("peak_rss_kib"), qint64(usage.ru_maxrss));
#endif

        logEvent(QStringLiteral("HEARTBEAT"), heartbeatDetails);
    });
    m_heartbeatTimer->start();
}

void DiagnosticManager::closeSession()
{
    if (!m_started || m_closed)
        return;

    flushTextInput();

    logEvent(QStringLiteral("SESSION_CLOSE"), {
        {QStringLiteral("reason"), QStringLiteral("normal_exit")}
    });

    m_closed = true;

    if (m_heartbeatTimer)
        m_heartbeatTimer->stop();
    if (m_textInputFlushTimer)
        m_textInputFlushTimer->stop();

    if (qApp)
        qApp->removeEventFilter(this);

    if (m_sessionFile) {
        m_sessionFile->flush();
        m_sessionFile->close();
    }
}

QVariantMap DiagnosticManager::baseRecord(const QString &event) const
{
    QVariantMap record;
    record.insert(QStringLiteral("timestamp"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    record.insert(QStringLiteral("session"), m_sessionId);
    record.insert(QStringLiteral("event"), event);

    if (m_startedAtMs > 0)
        record.insert(QStringLiteral("uptime_ms"), QDateTime::currentMSecsSinceEpoch() - m_startedAtMs);

    return record;
}

void DiagnosticManager::logEvent(const QString &event, const QVariantMap &details)
{
    if (!m_started || !m_sessionFile)
        return;

    QVariantMap record = baseRecord(event);
    if (!details.isEmpty())
        record.insert(QStringLiteral("details"), details);

    writeRecord(record);
}

void DiagnosticManager::writeRecord(const QVariantMap &record)
{
    if (!m_sessionFile || !m_sessionFile->isOpen())
        return;

    const QJsonObject object = QJsonObject::fromVariantMap(record);
    m_sessionFile->write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    m_sessionFile->write("\n");
    m_sessionFile->flush();
}

void DiagnosticManager::flushTextInput()
{
    if (m_pendingTextCharacters <= 0)
        return;

    const qint64 nowUptime =
        m_startedAtMs > 0 ? QDateTime::currentMSecsSinceEpoch() - m_startedAtMs : 0;

    QVariantMap details;
    details.insert(QStringLiteral("characters"), m_pendingTextCharacters);
    details.insert(QStringLiteral("target_class"), m_pendingTextTarget);
    details.insert(QStringLiteral("duration_ms"),
                   qMax<qint64>(0, nowUptime - m_pendingTextStartedAtMs));

    logEvent(QStringLiteral("TEXT_INPUT"), details);

    m_pendingTextCharacters = 0;
    m_pendingTextStartedAtMs = 0;
    m_pendingTextTarget.clear();
}

bool DiagnosticManager::sessionClosedNormally(const QString &path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    bool closed = false;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine();
        if (line.contains("\"event\":\"SESSION_CLOSE\""))
            closed = true;
    }
    return closed;
}

void DiagnosticManager::inspectPreviousSessions()
{
    QDir dir(diagnosticsDirectory());
    const QStringList sessions = dir.entryList(
        {QStringLiteral("%1*%2").arg(QLatin1String(sessionPrefix), QLatin1String(sessionSuffix))},
        QDir::Files,
        QDir::Name);

    QString lastClosed;
    for (const QString &name : sessions) {
        const QString path = dir.filePath(name);
        if (sessionClosedNormally(path))
            lastClosed = name;
    }

    if (!lastClosed.isEmpty())
        cleanupOldSessions(lastClosed);

    const QStringList remaining = dir.entryList(
        {QStringLiteral("%1*%2").arg(QLatin1String(sessionPrefix), QLatin1String(sessionSuffix))},
        QDir::Files,
        QDir::Name);

    for (const QString &name : remaining) {
        const QString path = dir.filePath(name);
        if (sessionClosedNormally(path))
            continue;

        const QString reportPath = createReportForSession(path);
        if (!reportPath.isEmpty())
            m_pendingReports.append(reportPath);
    }
}

void DiagnosticManager::cleanupOldSessions(const QString &lastClosedSessionFile)
{
    QDir dir(diagnosticsDirectory());
    const QStringList sessions = dir.entryList(
        {QStringLiteral("%1*%2").arg(QLatin1String(sessionPrefix), QLatin1String(sessionSuffix))},
        QDir::Files,
        QDir::Name);

    for (const QString &name : sessions) {
        if (name >= lastClosedSessionFile)
            continue;

        const QString oldSessionPath = dir.filePath(name);
        const QString oldReportPath = dir.filePath(
            QStringLiteral("%1%2%3").arg(
                QLatin1String(reportPrefix),
                sessionStem(oldSessionPath),
                QLatin1String(reportSuffix)));

        QFile::remove(oldReportPath);
        QFile::remove(oldSessionPath);
    }
}

QString DiagnosticManager::createReportForSession(const QString &sessionPath)
{
    QDir dir(diagnosticsDirectory());
    const QString reportPath = dir.filePath(
        QStringLiteral("%1%2%3").arg(
            QLatin1String(reportPrefix),
            sessionStem(sessionPath),
            QLatin1String(reportSuffix)));

    if (QFileInfo::exists(reportPath))
        return reportPath;

    QFile input(sessionPath);
    if (!input.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();

    const ParsedSession summary = parseSession(input);

    QFile report(reportPath);
    if (!report.open(QIODevice::WriteOnly | QIODevice::Text))
        return QString();

    auto writeLine = [&report](const QString &line = QString()) {
        report.write(line.toUtf8());
        report.write("\n");
    };

    writeLine(QStringLiteral("MATHOM - RAPPORT DE DIAGNOSTIC"));
    writeLine(QStringLiteral("=============================="));
    writeLine();

    writeLine(QStringLiteral("RESUME DE L'ARRET ANORMAL"));
    writeLine(QStringLiteral("--------------------------"));
    writeLine(QStringLiteral("Session : %1").arg(summary.sessionId));
    writeLine(QStringLiteral("Debut : %1").arg(summary.openedAt));
    writeLine(QStringLiteral("Derniere trace : %1").arg(summary.lastTimestamp));
    writeLine(QStringLiteral("Duree connue : %1").arg(formatDuration(summary.uptimeMs)));
    writeLine(QStringLiteral("Evenements enregistres : %1").arg(summary.eventCount));
    writeLine();
    writeLine(QStringLiteral("Mathom : %1").arg(summary.version));
    writeLine(QStringLiteral("Build : %1").arg(summary.buildId.isEmpty() ? QStringLiteral("non renseigne") : summary.buildId));
    writeLine(QStringLiteral("Qt : %1").arg(summary.qtVersion));
    writeLine(QStringLiteral("Systeme : %1").arg(summary.os));
    writeLine(QStringLiteral("Noyau : %1").arg(summary.kernel));
    writeLine(QStringLiteral("Architecture : %1").arg(summary.architecture));

    if (summary.lastMemoryKiB >= 0)
        writeLine(QStringLiteral("Memoire derniere mesure : %1 Mio").arg(summary.lastMemoryKiB / 1024.0, 0, 'f', 1));

    const qint64 retainedPeakMemoryKiB =
        qMax(summary.peakMemoryKiB, summary.measuredPeakMemoryKiB);
    if (retainedPeakMemoryKiB >= 0)
        writeLine(QStringLiteral("Pic memoire RSS retenu : %1 Mio").arg(retainedPeakMemoryKiB / 1024.0, 0, 'f', 1));

    writeLine();
    writeLine(QStringLiteral("Derniere action utilisateur directe :"));
    writeLine(summary.lastUserAction.isEmpty()
                  ? QStringLiteral("  aucune action utilisateur identifiee")
                  : QStringLiteral("  %1").arg(summary.lastUserAction));

    writeLine(QStringLiteral("Derniere operation terminee correctement :"));
    writeLine(summary.lastCompletedOperation.isEmpty()
                  ? QStringLiteral("  aucune")
                  : QStringLiteral("  %1").arg(summary.lastCompletedOperation));

    writeLine(QStringLiteral("Derniere sauvegarde connue :"));
    writeLine(summary.lastSave.isEmpty()
                  ? QStringLiteral("  aucune information")
                  : QStringLiteral("  %1").arg(summary.lastSave));

    writeLine(QStringLiteral("Operation(s) encore ouverte(s) au moment de l'arret :"));
    if (summary.activeOperations.isEmpty()) {
        writeLine(QStringLiteral("  aucune operation BEGIN sans fin correspondante"));
    } else {
        for (const QString &operation : summary.activeOperations)
            writeLine(QStringLiteral("  - %1").arg(humanEventName(operation + QStringLiteral("_BEGIN"))));
    }

    writeLine();
    writeLine(QStringLiteral("Cause technique :"));
    writeLine(QStringLiteral("  SESSION_CLOSE absent : la session s'est terminee de facon anormale."));
    writeLine(QStringLiteral("  Le journal seul ne permet pas de distinguer avec certitude un plantage,"));
    writeLine(QStringLiteral("  un arret force du processus ou une extinction brutale de l'ordinateur."));
    writeLine();

    writeLine(QStringLiteral("Dernier evenement enregistre :"));
    writeLine(summary.lastEvent.isEmpty()
                  ? QStringLiteral("  aucun")
                  : QStringLiteral("  %1").arg(summary.lastEvent));

    writeLine();
    writeLine(QStringLiteral("CONFIDENTIALITE"));
    writeLine(QStringLiteral("---------------"));
    writeLine(QStringLiteral("Le rapport n'enregistre pas le texte saisi dans les Mathoms."));
    writeLine(QStringLiteral("Les frappes de texte sont regroupees en compteurs de caracteres."));
    writeLine();

    writeLine(QStringLiteral("JOURNAL RECENT"));
    writeLine(QStringLiteral("--------------"));

    const int firstLine = qMax(0, summary.rawLines.size() - reportEventLimit);
    if (firstLine > 0) {
        writeLine(QStringLiteral("%1 evenement(s) plus ancien(s) ne sont pas inclus dans ce rapport.")
                      .arg(firstLine));
        writeLine(QStringLiteral("Le journal local complet reste conserve selon la politique de Mathom."));
        writeLine();
    }

    for (int i = firstLine; i < summary.rawLines.size(); ++i) {
        report.write(summary.rawLines.at(i));
        report.write("\n");
    }

    report.flush();
    report.close();

    return reportPath;
}

void DiagnosticManager::openReport(const QString &reportPath)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(reportPath));
}

void DiagnosticManager::appendPostMortemDiagnostics(
    const QString &reportPath)
{
    static const QByteArray marker(
        "DIAGNOSTIC POST-MORTEM AUTOMATIQUE");

    QFile existing(reportPath);
    if (existing.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (existing.readAll().contains(marker))
            return;
    }

    QString stem = QFileInfo(reportPath).completeBaseName();

    if (!stem.startsWith(QLatin1String(reportPrefix)))
        return;

    stem.remove(
        0,
        QString::fromLatin1(reportPrefix).size());

    const QString sessionPath =
        QDir(diagnosticsDirectory()).filePath(
            QStringLiteral("%1%2%3")
                .arg(
                    QLatin1String(sessionPrefix),
                    stem,
                    QLatin1String(sessionSuffix)));

    QFile sessionFile(sessionPath);

    if (!sessionFile.open(
            QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    const ParsedSession summary =
        parseSession(sessionFile);

    QDateTime crashTime =
        QDateTime::fromString(
            summary.lastTimestamp,
            Qt::ISODateWithMs);

    if (!crashTime.isValid()) {
        crashTime =
            QDateTime::fromString(
                summary.lastTimestamp,
                Qt::ISODate);
    }

    QString systemJournal =
        QStringLiteral(
            "[horodatage du crash indisponible]");

    QString userJournal =
        QStringLiteral(
            "[horodatage du crash indisponible]");

    if (crashTime.isValid()) {
        const QString since =
            crashTime.addSecs(-15)
                .toString(
                    QStringLiteral(
                        "yyyy-MM-dd HH:mm:ss"));

        const QString until =
            crashTime.addSecs(15)
                .toString(
                    QStringLiteral(
                        "yyyy-MM-dd HH:mm:ss"));

        const QString rawSystem =
            runDiagnosticCommand(
                QStringLiteral("journalctl"),
                {
                    QStringLiteral("-b"),
                    QStringLiteral("--since"),
                    since,
                    QStringLiteral("--until"),
                    until,
                    QStringLiteral("--no-pager")
                },
                5000);

        systemJournal =
            filteredJournal(rawSystem, summary.pid);

        const QString rawUser =
            runDiagnosticCommand(
                QStringLiteral("journalctl"),
                {
                    QStringLiteral("--user"),
                    QStringLiteral("-b"),
                    QStringLiteral("--since"),
                    since,
                    QStringLiteral("--until"),
                    until,
                    QStringLiteral("--no-pager")
                },
                5000);

        userJournal =
            filteredJournal(rawUser, summary.pid);
    }

    const QString packageInfo =
        runDiagnosticCommand(
            QStringLiteral("dpkg-query"),
            {
                QStringLiteral("-W"),
                QStringLiteral(
                    "-f=Package: ${Package}\\n"
                    "Version: ${Version}\\n"
                    "Architecture: ${Architecture}\\n"),
                QStringLiteral("mathom")
            });

    const QString executable =
        QCoreApplication::applicationFilePath();

    const QString libBasket =
        mathomRuntimeLibrary(
            QStringLiteral("libLibBasket.so.2"));

    const QString qtWidgets =
        mathomRuntimeLibrary(
            QStringLiteral("libQt6Widgets.so.6"));

    QString hashes;
    hashes +=
        QStringLiteral("mathom : sha256:%1\n")
            .arg(fileSha256(executable));

    hashes +=
        QStringLiteral(
            "libLibBasket.so.2 : sha256:%1\n")
            .arg(fileSha256(libBasket));

    hashes +=
        QStringLiteral(
            "libQt6Widgets.so.6 : sha256:%1")
            .arg(fileSha256(qtWidgets));

    QString coredumpInfo;

    if (summary.pid > 0) {
        coredumpInfo =
            runDiagnosticCommand(
                QStringLiteral("coredumpctl"),
                {
                    QStringLiteral("info"),
                    QString::number(summary.pid),
                    QStringLiteral("--no-pager")
                },
                6000);
    } else {
        coredumpInfo =
            QStringLiteral(
                "[PID de la session precedente indisponible]");
    }

    bool hasUsefulCoredump =
        !coredumpInfo.contains(
            QStringLiteral("No coredumps found"),
            Qt::CaseInsensitive)
        && !coredumpInfo.contains(
            QStringLiteral("aucune sortie"),
            Qt::CaseInsensitive)
        && !coredumpInfo.contains(
            QStringLiteral("commande indisponible"),
            Qt::CaseInsensitive);

    QString apportInfo;
    bool hasApportStack = false;

#ifdef Q_OS_LINUX
    QDir crashDir(QStringLiteral("/var/crash"));

    const QFileInfoList crashFiles =
        crashDir.entryInfoList(
            {
                QStringLiteral(
                    "_opt_mathom_usr_bin_mathom.*.crash")
            },
            QDir::Files,
            QDir::Time);

    QFileInfo selectedCrash;

    for (const QFileInfo &candidate : crashFiles) {
        if (!crashTime.isValid()) {
            selectedCrash = candidate;
            break;
        }

        const qint64 delta =
            candidate.lastModified().secsTo(crashTime);

        if (delta >= -300 && delta <= 300) {
            selectedCrash = candidate;
            break;
        }
    }

    if (selectedCrash.exists()) {
        apportInfo +=
            QStringLiteral("Fichier Apport : %1\n")
                .arg(selectedCrash.fileName());

        const QString apportUnpack =
            QStandardPaths::findExecutable(
                QStringLiteral("apport-unpack"));

        if (!apportUnpack.isEmpty()) {
            QTemporaryDir temporaryDirectory;

            if (temporaryDirectory.isValid()) {
                const QString unpackResult =
                    runDiagnosticCommand(
                        apportUnpack,
                        {
                            selectedCrash.absoluteFilePath(),
                            temporaryDirectory.path()
                        },
                        8000);

                if (!unpackResult.contains(
                        QStringLiteral("code retour"),
                        Qt::CaseInsensitive)) {
                    const QStringList fields = {
                        QStringLiteral("ProblemType"),
                        QStringLiteral("Date"),
                        QStringLiteral("ExecutablePath"),
                        QStringLiteral("Signal"),
                        QStringLiteral("SignalName"),
                        QStringLiteral("JournalErrors"),
                        QStringLiteral("SegvAnalysis"),
                        QStringLiteral("StacktraceTop"),
                        QStringLiteral("Stacktrace"),
                        QStringLiteral("ThreadStacktrace")
                    };

                    for (const QString &field : fields) {
                        const QString value =
                            readTextFileLimited(
                                QDir(
                                    temporaryDirectory.path())
                                    .filePath(field));

                        if (value.isEmpty())
                            continue;

                        apportInfo +=
                            QStringLiteral(
                                "\n----- %1 -----\n%2\n")
                                .arg(field, value);

                        if (field.startsWith(
                                QStringLiteral(
                                    "Stacktrace"))) {
                            hasApportStack = true;
                        }

                        if (field
                                == QStringLiteral(
                                    "ThreadStacktrace")) {
                            hasApportStack = true;
                        }
                    }
                } else {
                    apportInfo += unpackResult;
                    apportInfo += QLatin1Char('\n');
                }
            }
        } else {
            apportInfo +=
                QStringLiteral(
                    "[apport-unpack indisponible]\n");
        }
    } else {
        apportInfo =
            QStringLiteral(
                "[aucun rapport Apport correspondant "
                "a la fenetre du crash]");
    }
#else
    apportInfo =
        QStringLiteral(
            "[diagnostic Apport disponible uniquement "
            "sous Linux]");
#endif

    const bool complete =
        hasApportStack || hasUsefulCoredump;

    QString diagnostic;
    QTextStream out(&diagnostic);

    out << "\n\n"
        << "DIAGNOSTIC POST-MORTEM AUTOMATIQUE\n"
        << "==================================\n\n";

    out << "Niveau de diagnostic : "
        << (complete ? "COMPLET" : "PARTIEL")
        << "\n\n";

    out << "Ce diagnostic a ete execute automatiquement "
           "par Mathom sans privilege sudo avant "
           "la preparation du courriel.\n\n";

    out << "PAQUET INSTALLE AU MOMENT DE L'ENVOI\n"
        << "-------------------------------------\n"
        << packageInfo << "\n\n";

    out << "EMPREINTES DES COMPOSANTS\n"
        << "-------------------------\n"
        << hashes << "\n\n";

    out << "JOURNAL SYSTEME AUTOUR DU CRASH\n"
        << "-------------------------------\n"
        << systemJournal << "\n\n";

    out << "JOURNAL UTILISATEUR AUTOUR DU CRASH\n"
        << "-----------------------------------\n"
        << userJournal << "\n\n";

    out << "COREDUMPCTL\n"
        << "-----------\n"
        << coredumpInfo << "\n\n";

    out << "APPORT\n"
        << "------\n"
        << limitedOutput(apportInfo, 60000)
        << "\n\n";

    out << "CONFIDENTIALITE DU DIAGNOSTIC COMPLEMENTAIRE\n"
        << "--------------------------------------------\n"
        << "Aucun texte contenu dans les Mathoms "
           "n'est collecte.\n"
        << "Les journaux sont limites a une fenetre "
           "de 30 secondes autour de la derniere trace "
           "et filtres sur les evenements techniques "
           "pertinents pour Mathom.\n"
        << "Des chemins de fichiers, versions de "
           "bibliotheques, identifiants de processus "
           "et adresses memoire techniques peuvent "
           "apparaitre dans les traces systeme.\n";

    QFile report(reportPath);

    if (!report.open(
            QIODevice::Append | QIODevice::Text)) {
        return;
    }

    report.write(diagnostic.toUtf8());
    report.flush();
}

void DiagnosticManager::prepareEmail(const QString &reportPath)
{
    appendPostMortemDiagnostics(reportPath);

    const QString subject =
        QStringLiteral("[Mathom Bug] %1").arg(QFileInfo(reportPath).completeBaseName());

    QFile report(reportPath);
    QString reportText;

    if (report.open(QIODevice::ReadOnly | QIODevice::Text))
        reportText = QString::fromUtf8(report.readAll());

    if (reportText.isEmpty()) {
        reportText =
            QStringLiteral("Le rapport de diagnostic n'a pas pu etre relu par Mathom.\n"
                           "Fichier local : %1")
                .arg(QFileInfo(reportPath).fileName());
    }

    const QString body =
        QStringLiteral("Bonjour,\n\n"
                       "Mathom a detecte un arret anormal.\n"
                       "Voici le rapport de diagnostic genere automatiquement.\n"
                       "Le contenu des Mathoms n'est pas inclus dans ce rapport.\n\n"
                       "---------------- RAPPORT MATHOM ----------------\n\n")
        + reportText
        + QStringLiteral("\n\n-------------- FIN DU RAPPORT --------------\n");

    logEvent(
        QStringLiteral("REPORT_EMAIL_PREPARE_BEGIN"),
        {{QStringLiteral("report"), QFileInfo(reportPath).fileName()}});

    const QString xdgEmail = QStandardPaths::findExecutable(QStringLiteral("xdg-email"));
    if (!xdgEmail.isEmpty()) {
        const QStringList arguments = {
            QStringLiteral("--utf8"),
            QStringLiteral("--subject"),
            subject,
            QStringLiteral("--body"),
            body,
            QStringLiteral("contact@thorinux.fr")
        };

        if (QProcess::startDetached(xdgEmail, arguments)) {
            logEvent(
                QStringLiteral("REPORT_EMAIL_PREPARED"),
                {{QStringLiteral("method"), QStringLiteral("xdg-email-body")},
                 {QStringLiteral("report_in_body"), true}});
            return;
        }
    }

    QUrl mail(QStringLiteral("mailto:contact@thorinux.fr"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("subject"), subject);
    query.addQueryItem(QStringLiteral("body"), body);
    mail.setQuery(query);

    QDesktopServices::openUrl(mail);

    logEvent(
        QStringLiteral("REPORT_EMAIL_PREPARED"),
        {{QStringLiteral("method"), QStringLiteral("mailto-body")},
         {QStringLiteral("report_in_body"), true}});
}

void DiagnosticManager::showPendingReportDialog(QWidget *parent)
{
    if (m_pendingReports.isEmpty())
        return;

    const QString reportPath = m_pendingReports.constLast();

    for (;;) {
        QMessageBox box(parent);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(i18n("Mathom - Diagnostic Report"));
        box.setText(i18n("Mathom did not close normally during the previous session."));
        box.setInformativeText(
            i18n("A technical report was created. It does not contain the text of your notes.\n\n"
                 "You can review it first, then return here to send it to Thorinux."));

        auto *viewButton =
            box.addButton(i18n("View report"), QMessageBox::ActionRole);
        auto *emailButton =
            box.addButton(i18n("Prepare an email to Thorinux"), QMessageBox::ActionRole);
        box.addButton(i18n("Close"), QMessageBox::RejectRole);

        box.exec();

        if (box.clickedButton() == viewButton) {
            logEvent(
                QStringLiteral("REPORT_VIEW"),
                {{QStringLiteral("report"), QFileInfo(reportPath).fileName()}});
            openReport(reportPath);
            continue;
        }

        if (box.clickedButton() == emailButton)
            prepareEmail(reportPath);

        break;
    }
}

bool DiagnosticManager::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_started || m_closed || !event)
        return QObject::eventFilter(watched, event);

    const QString className = QString::fromLatin1(watched->metaObject()->className());

    if (event->type() == QEvent::MouseButtonPress) {
        // A single physical click is propagated through many Qt widgets.
        // Log only the QWidgetWindow delivery to keep one useful trace per click.
        if (className == QStringLiteral("QWidgetWindow")) {
            flushTextInput();

            const auto *mouseEvent = static_cast<QMouseEvent *>(event);
            QVariantMap details;
            details.insert(QStringLiteral("class"), className);
            details.insert(QStringLiteral("object"), safeObjectName(watched));
            details.insert(QStringLiteral("button"), int(mouseEvent->button()));
            details.insert(QStringLiteral("x"), qRound(mouseEvent->position().x()));
            details.insert(QStringLiteral("y"), qRound(mouseEvent->position().y()));
            logEvent(QStringLiteral("MOUSE_PRESS"), details);
        }
    } else if (event->type() == QEvent::KeyPress) {
        const auto *keyEvent = static_cast<QKeyEvent *>(event);
        const bool isEditor =
            watched->inherits("QTextEdit")
            || watched->inherits("QLineEdit");

        if (!isEditor)
            return QObject::eventFilter(watched, event);

        const bool printable =
            !keyEvent->text().isEmpty()
            && keyEvent->text().at(0).isPrint();

        if (printable) {
            const QString target = className;
            if (m_pendingTextCharacters > 0
                && m_pendingTextTarget != target) {
                flushTextInput();
            }

            if (m_pendingTextCharacters == 0) {
                m_pendingTextStartedAtMs =
                    QDateTime::currentMSecsSinceEpoch() - m_startedAtMs;
                m_pendingTextTarget = target;
            }

            m_pendingTextCharacters += keyEvent->text().size();
            if (m_textInputFlushTimer)
                m_textInputFlushTimer->start();
        } else {
            flushTextInput();

            QVariantMap details;
            details.insert(QStringLiteral("class"), className);
            details.insert(QStringLiteral("object"), safeObjectName(watched));
            details.insert(QStringLiteral("key"), keyEvent->key());
            details.insert(QStringLiteral("modifiers"), int(keyEvent->modifiers()));
            logEvent(QStringLiteral("KEY_PRESS"), details);
        }
    }

    return QObject::eventFilter(watched, event);
}
