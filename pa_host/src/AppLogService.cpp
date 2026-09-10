#include "AppLogService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>

#include <algorithm>

namespace {
QString levelName(AppLogLevel level) {
    switch (level) {
    case AppLogLevel::Debug:
        return QStringLiteral("DEBUG");
    case AppLogLevel::Info:
        return QStringLiteral("INFO");
    case AppLogLevel::Warning:
        return QStringLiteral("WARN");
    case AppLogLevel::Error:
        return QStringLiteral("ERROR");
    }
    return QStringLiteral("INFO");
}

QString defaultLogDirectory() {
    QString root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (root.isEmpty()) {
        root = QDir::homePath() + QStringLiteral("/.pa_host");
    }
    return QDir(root).filePath(QStringLiteral("logs"));
}

bool writeUtf8(QIODevice* device, const QString& text) {
    const QByteArray bytes = text.toUtf8();
    return device->write(bytes) == bytes.size();
}
}

QString AppLogEntry::formatted() const {
    const QString safeCategory = category.trimmed().isEmpty()
        ? QStringLiteral("SYSTEM")
        : category.trimmed().toUpper();
    return QStringLiteral("[%1] [%2] [%3] %4")
        .arg(timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
            levelName(level),
            safeCategory,
            message);
}

AppLogService::AppLogService(QObject* parent)
    : QObject(parent) {
    qRegisterMetaType<AppLogLevel>("AppLogLevel");
    qRegisterMetaType<AppLogEntry>("AppLogEntry");
}

AppLogService::~AppLogService() {
    if (logFile_.isOpen()) {
        logFile_.flush();
        logFile_.close();
    }
}

bool AppLogService::start(const QString& directory, QString* errorMessage) {
    if (logFile_.isOpen()) {
        logFile_.flush();
        logFile_.close();
    }
    started_ = false;
    logDirectory_ = directory.trimmed().isEmpty() ? defaultLogDirectory() : directory;
    if (!QDir().mkpath(logDirectory_)) {
        const QString message = QStringLiteral("无法创建日志目录: %1").arg(logDirectory_);
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    }
    if (!openCurrentLog(errorMessage)) {
        return false;
    }
    started_ = true;
    return true;
}

bool AppLogService::isStarted() const {
    return started_;
}

QString AppLogService::logDirectory() const {
    return logDirectory_;
}

QString AppLogService::currentLogPath() const {
    return logFile_.fileName();
}

void AppLogService::setRotationPolicy(qint64 maxFileSizeBytes, int maxArchiveFiles) {
    maxFileSizeBytes_ = std::max<qint64>(1024, maxFileSizeBytes);
    maxArchiveFiles_ = std::max(0, maxArchiveFiles);
}

void AppLogService::log(
    AppLogLevel level,
    const QString& category,
    const QString& message) {
    AppLogEntry entry;
    entry.timestamp = QDateTime::currentDateTime();
    entry.level = level;
    entry.category = category;
    entry.message = message;

    const QByteArray line = entry.formatted().toUtf8() + '\n';
    if (started_ && logFile_.isOpen()) {
        QString rotationError;
        if (!rotateIfNeeded(line.size(), &rotationError)) {
            reportPersistenceError(rotationError);
        }
        if (logFile_.write(line) != line.size() || !logFile_.flush()) {
            reportPersistenceError(QStringLiteral("日志写入失败: %1").arg(logFile_.errorString()));
        }
    }
    emit entryAdded(entry);
}

void AppLogService::debug(const QString& category, const QString& message) {
    log(AppLogLevel::Debug, category, message);
}

void AppLogService::info(const QString& category, const QString& message) {
    log(AppLogLevel::Info, category, message);
}

void AppLogService::warning(const QString& category, const QString& message) {
    log(AppLogLevel::Warning, category, message);
}

void AppLogService::error(const QString& category, const QString& message) {
    log(AppLogLevel::Error, category, message);
}

bool AppLogService::exportDiagnostics(
    const QString& outputPath,
    const QMap<QString, QString>& metadata,
    QString* errorMessage) {
    if (!currentLogPath().isEmpty()
        && QFileInfo(outputPath).absoluteFilePath()
            == QFileInfo(currentLogPath()).absoluteFilePath()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("诊断文件不能覆盖当前日志文件");
        }
        return false;
    }
    if (logFile_.isOpen()) {
        logFile_.flush();
    }

    QSaveFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = output.errorString();
        }
        return false;
    }

    const QString header = QStringLiteral(
        "PA Host Diagnostics\n"
        "generated=%1\n"
        "application=%2\n"
        "version=%3\n"
        "qt=%4\n"
        "os=%5\n"
        "kernel=%6 %7\n"
        "cpu=%8\n"
        "build_abi=%9\n\n"
        "[metadata]\n")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
            QCoreApplication::applicationName(),
            QCoreApplication::applicationVersion(),
            QString::fromLatin1(qVersion()),
            QSysInfo::prettyProductName(),
            QSysInfo::kernelType(),
            QSysInfo::kernelVersion(),
            QSysInfo::currentCpuArchitecture(),
            QSysInfo::buildAbi());
    if (!writeUtf8(&output, header)) {
        if (errorMessage != nullptr) {
            *errorMessage = output.errorString();
        }
        return false;
    }
    for (auto it = metadata.cbegin(); it != metadata.cend(); ++it) {
        if (!writeUtf8(&output, QStringLiteral("%1=%2\n").arg(it.key(), it.value()))) {
            if (errorMessage != nullptr) {
                *errorMessage = output.errorString();
            }
            return false;
        }
    }

    if (!writeUtf8(&output, QStringLiteral("\n[logs]\n"))) {
        if (errorMessage != nullptr) {
            *errorMessage = output.errorString();
        }
        return false;
    }
    QFileInfoList files;
    if (!logDirectory_.isEmpty()) {
        files = QDir(logDirectory_).entryInfoList(
            {QStringLiteral("pa_host.log"), QStringLiteral("pa_host.*.log")},
            QDir::Files,
            QDir::Name);
    }
    for (const QFileInfo& fileInfo : files) {
        if (!writeUtf8(&output,
                QStringLiteral("\n--- %1 ---\n").arg(fileInfo.fileName()))) {
            if (errorMessage != nullptr) {
                *errorMessage = output.errorString();
            }
            return false;
        }
        QFile input(fileInfo.absoluteFilePath());
        if (!input.open(QIODevice::ReadOnly)) {
            writeUtf8(&output, QStringLiteral("[无法读取日志文件]\n"));
            continue;
        }
        while (!input.atEnd()) {
            const QByteArray chunk = input.read(64 * 1024);
            if (chunk.isEmpty() && input.error() != QFile::NoError) {
                break;
            }
            if (output.write(chunk) != chunk.size()) {
                if (errorMessage != nullptr) {
                    *errorMessage = output.errorString();
                }
                return false;
            }
        }
    }

    if (!output.commit()) {
        if (errorMessage != nullptr) {
            *errorMessage = output.errorString();
        }
        return false;
    }
    return true;
}

QString AppLogService::archivePath(int index) const {
    return QDir(logDirectory_).filePath(
        QStringLiteral("pa_host.%1.log").arg(index));
}

bool AppLogService::openCurrentLog(QString* errorMessage) {
    logFile_.setFileName(QDir(logDirectory_).filePath(QStringLiteral("pa_host.log")));
    if (!logFile_.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法打开日志文件: %1").arg(logFile_.errorString());
        }
        return false;
    }
    return true;
}

bool AppLogService::rotateIfNeeded(qint64 incomingBytes, QString* errorMessage) {
    if (logFile_.size() == 0 || logFile_.size() + incomingBytes <= maxFileSizeBytes_) {
        return true;
    }
    return rotateLogs(errorMessage);
}

bool AppLogService::rotateLogs(QString* errorMessage) {
    logFile_.flush();
    logFile_.close();
    const QString currentPath = QDir(logDirectory_).filePath(QStringLiteral("pa_host.log"));

    if (maxArchiveFiles_ == 0) {
        if (QFile::exists(currentPath) && !QFile::remove(currentPath)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("无法清理旧日志: %1").arg(currentPath);
            }
            if (!openCurrentLog(nullptr)) {
                started_ = false;
            }
            return false;
        }
    } else {
        QFile::remove(archivePath(maxArchiveFiles_));
        for (int index = maxArchiveFiles_ - 1; index >= 1; --index) {
            const QString source = archivePath(index);
            if (QFile::exists(source)
                && !QFile::rename(source, archivePath(index + 1))) {
                if (errorMessage != nullptr) {
                    *errorMessage = QStringLiteral("无法滚动日志文件: %1").arg(source);
                }
                if (!openCurrentLog(nullptr)) {
                    started_ = false;
                }
                return false;
            }
        }
        if (QFile::exists(currentPath) && !QFile::rename(currentPath, archivePath(1))) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("无法归档当前日志: %1").arg(currentPath);
            }
            if (!openCurrentLog(nullptr)) {
                started_ = false;
            }
            return false;
        }
    }

    if (!openCurrentLog(errorMessage)) {
        started_ = false;
        return false;
    }
    return true;
}

void AppLogService::reportPersistenceError(const QString& message) {
    emit persistenceError(message);
}
