#include "AppSettings.h"

#include <QSettings>
#include <QtGlobal>

#include <algorithm>

namespace {
const char kLastImageDirectoryKey[] = "paths/lastImageDirectory";
const char kLastSaveDirectoryKey[] = "paths/lastSaveDirectory";
const char kLastExportDirectoryKey[] = "paths/lastExportDirectory";
const char kLastDiagnosticDirectoryKey[] = "paths/lastDiagnosticDirectory";
const char kSerialPortKey[] = "control/serialPort";
const char kSerialBaudRateKey[] = "control/serialBaudRate";

constexpr int kDefaultBaudRate = 115200;
constexpr int kMinimumBaudRate = 1200;
constexpr int kMaximumBaudRate = 3000000;

QString defaultSerialPort() {
#ifdef Q_OS_WIN
    return QString();
#else
    return QStringLiteral("/dev/ttyWCH0");
#endif
}
}

AppSettings::AppSettings()
    : AppSettings(std::make_unique<QSettings>()) {
}

AppSettings::AppSettings(const QString& iniFilePath)
    : AppSettings(std::make_unique<QSettings>(iniFilePath, QSettings::IniFormat)) {
}

AppSettings::AppSettings(std::unique_ptr<QSettings> settings)
    : settings_(std::move(settings)) {
}

AppSettings::~AppSettings() = default;

QString AppSettings::lastImageDirectory() const {
    return settings_->value(QLatin1String(kLastImageDirectoryKey)).toString();
}

void AppSettings::setLastImageDirectory(const QString& directory) {
    settings_->setValue(QLatin1String(kLastImageDirectoryKey), directory);
}

QString AppSettings::lastSaveDirectory() const {
    return settings_->value(QLatin1String(kLastSaveDirectoryKey)).toString();
}

void AppSettings::setLastSaveDirectory(const QString& directory) {
    settings_->setValue(QLatin1String(kLastSaveDirectoryKey), directory);
}

QString AppSettings::lastExportDirectory() const {
    return settings_->value(QLatin1String(kLastExportDirectoryKey)).toString();
}

void AppSettings::setLastExportDirectory(const QString& directory) {
    settings_->setValue(QLatin1String(kLastExportDirectoryKey), directory);
}

QString AppSettings::lastDiagnosticDirectory() const {
    return settings_->value(QLatin1String(kLastDiagnosticDirectoryKey)).toString();
}

void AppSettings::setLastDiagnosticDirectory(const QString& directory) {
    settings_->setValue(QLatin1String(kLastDiagnosticDirectoryKey), directory);
}

QString AppSettings::serialPort() const {
    return settings_->value(QLatin1String(kSerialPortKey), defaultSerialPort()).toString();
}

void AppSettings::setSerialPort(const QString& portName) {
    settings_->setValue(QLatin1String(kSerialPortKey), portName);
}

int AppSettings::serialBaudRate() const {
    return std::max(kMinimumBaudRate,
        std::min(kMaximumBaudRate,
            settings_->value(QLatin1String(kSerialBaudRateKey), kDefaultBaudRate).toInt()));
}

void AppSettings::setSerialBaudRate(int baudRate) {
    settings_->setValue(QLatin1String(kSerialBaudRateKey),
        std::max(kMinimumBaudRate, std::min(kMaximumBaudRate, baudRate)));
}

void AppSettings::sync() {
    settings_->sync();
}
