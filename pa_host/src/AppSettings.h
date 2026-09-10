#pragma once

#include <QString>

#include <memory>

class QSettings;

/* 应用配置边界：集中维护稳定键名、默认值和数值范围。 */
class AppSettings final {
public:
    AppSettings();
    explicit AppSettings(const QString& iniFilePath);
    ~AppSettings();

    AppSettings(const AppSettings&) = delete;
    AppSettings& operator=(const AppSettings&) = delete;

    QString lastImageDirectory() const;
    void setLastImageDirectory(const QString& directory);
    QString lastSaveDirectory() const;
    void setLastSaveDirectory(const QString& directory);
    QString lastExportDirectory() const;
    void setLastExportDirectory(const QString& directory);
    QString lastDiagnosticDirectory() const;
    void setLastDiagnosticDirectory(const QString& directory);

    QString serialPort() const;
    void setSerialPort(const QString& portName);
    int serialBaudRate() const;
    void setSerialBaudRate(int baudRate);
    void sync();

private:
    explicit AppSettings(std::unique_ptr<QSettings> settings);

    std::unique_ptr<QSettings> settings_;
};
