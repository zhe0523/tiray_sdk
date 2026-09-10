#pragma once

#include <QDateTime>
#include <QFile>
#include <QMap>
#include <QMetaType>
#include <QObject>
#include <QString>

enum class AppLogLevel {
    Debug,
    Info,
    Warning,
    Error
};

struct AppLogEntry {
    QDateTime timestamp;
    AppLogLevel level = AppLogLevel::Info;
    QString category;
    QString message;

    QString formatted() const;
};

/*
 * 应用日志服务：同一条结构化日志同时发送给界面并写入滚动文本文件。
 * 只记录事件和元数据，不接收或序列化图像像素。
 */
class AppLogService final : public QObject {
    Q_OBJECT

public:
    explicit AppLogService(QObject* parent = nullptr);
    ~AppLogService() override;

    bool start(const QString& directory = QString(), QString* errorMessage = nullptr);
    bool isStarted() const;
    QString logDirectory() const;
    QString currentLogPath() const;
    void setRotationPolicy(qint64 maxFileSizeBytes, int maxArchiveFiles);

    void log(AppLogLevel level, const QString& category, const QString& message);
    void debug(const QString& category, const QString& message);
    void info(const QString& category, const QString& message);
    void warning(const QString& category, const QString& message);
    void error(const QString& category, const QString& message);

    bool exportDiagnostics(
        const QString& outputPath,
        const QMap<QString, QString>& metadata,
        QString* errorMessage = nullptr);

signals:
    void entryAdded(const AppLogEntry& entry);
    void persistenceError(const QString& message);

private:
    QString archivePath(int index) const;
    bool openCurrentLog(QString* errorMessage);
    bool rotateIfNeeded(qint64 incomingBytes, QString* errorMessage);
    bool rotateLogs(QString* errorMessage);
    void reportPersistenceError(const QString& message);

    QFile logFile_;
    QString logDirectory_;
    qint64 maxFileSizeBytes_ = 5 * 1024 * 1024;
    int maxArchiveFiles_ = 5;
    bool started_ = false;
};

Q_DECLARE_METATYPE(AppLogLevel)
Q_DECLARE_METATYPE(AppLogEntry)
