#include "AppLogService.h"
#include "AppSettings.h"
#include "FramePresentationController.h"
#include "ILineTransport.h"
#include "ImageAcquisitionController.h"
#include "ImageAlgorithms.h"
#include "ImageExportService.h"
#include "ImageSession.h"
#include "ImageSource.h"
#include "ImageTransferWorkflowController.h"
#include "MtfAnalysis.h"
#include "PaDeviceController.h"
#include "PaBinaryProtocol.h"
#include "PaProtocol.h"
#include "ReplayPresentationScheduler.h"
#include "TiRawImage.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include <functional>
#include <iterator>

namespace {
struct TestCase {
    const char* name;
    std::function<bool()> run;
};

class FakeLineTransport final : public ILineTransport {
public:
    explicit FakeLineTransport(QObject* parent = nullptr)
        : ILineTransport(parent) {
    }

    bool open(const QString& portName, int baudRate, QString* errorMessage) override {
        lastPortName = portName;
        lastBaudRate = baudRate;
        if (failOpen) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("模拟打开失败");
            }
            emit connectionChanged(false);
            return false;
        }
        openState = true;
        emit connectionChanged(true);
        return true;
    }

    void close() override {
        openState = false;
        emit connectionChanged(false);
    }

    bool isOpen() const override {
        return openState;
    }

    QString portName() const override {
        return lastPortName;
    }

    bool sendBinaryFrame(const PaBinaryProtocol::Frame& frame, QString* errorMessage) override {
        if (!openState || failSend) {
            if (errorMessage != nullptr) *errorMessage = QStringLiteral("模拟二进制发送失败");
            return false;
        }
        sentBinaryFrames.push_back(frame);
        return true;
    }

    void injectError(const QString& message) {
        emit errorOccurred(message);
    }

    void injectBinaryFrame(const PaBinaryProtocol::Frame& frame) {
        emit binaryFrameReceived(frame);
    }

    bool openState = false;
    bool failOpen = false;
    bool failSend = false;
    QString lastPortName;
    int lastBaudRate = 0;
    QList<PaBinaryProtocol::Frame> sentBinaryFrames;
};

class FakeImageSource final : public IImageSource {
public:
    explicit FakeImageSource(QObject* parent = nullptr)
        : IImageSource(parent) {
    }

    bool start(QString* errorMessage) override {
        stats_ = {};
        if (failStart) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("模拟图像源启动失败");
            }
            return false;
        }
        running_ = true;
        emit runningChanged(true);
        if (emitFrameDuringStart) {
            deliverFrame(startupFrame);
        }
        return true;
    }

    void stop() override {
        if (!running_) {
            return;
        }
        running_ = false;
        emit runningChanged(false);
    }

    bool isRunning() const override {
        return running_;
    }

    ImageSourceStats stats() const override {
        return stats_;
    }

    void deliverFrame(const ImageFrame& frame) {
        if (!running_) {
            return;
        }
        ++stats_.deliveredFrames;
        emit frameReady(frame);
    }

    void queueLateFrame(const ImageFrame& frame) {
        QTimer::singleShot(0, this, [this, frame]() {
            ++stats_.deliveredFrames;
            emit frameReady(frame);
        });
    }

    void finish() {
        if (!running_) {
            return;
        }
        running_ = false;
        emit runningChanged(false);
    }

    void injectError(const QString& message) {
        emit sourceError(message);
    }

    bool failStart = false;
    bool emitFrameDuringStart = false;
    ImageFrame startupFrame;

private:
    bool running_ = false;
    ImageSourceStats stats_;
};

void appendLe16(QByteArray* data, quint16 value) {
    data->append(static_cast<char>(value & 0xff));
    data->append(static_cast<char>((value >> 8) & 0xff));
}

bool writeTiraw(
    const QString& path,
    quint16 width,
    quint16 height,
    const QVector<quint16>& pixels,
    const QByteArray& magic = QByteArrayLiteral("TiRayRaw"),
    quint16 bytesPerPixel = 2) {
    QByteArray data;
    data.append(magic.leftJustified(8, '\0', true));
    appendLe16(&data, 1);
    appendLe16(&data, bytesPerPixel);
    appendLe16(&data, height);
    appendLe16(&data, width);
    for (const quint16 pixel : pixels) {
        appendLe16(&data, pixel);
    }

    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

bool check(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        qCritical("检查失败: %s (%s:%d)", expression, file, line);
    }
    return condition;
}

#define CHECK(expression) \
    do { \
        if (!check((expression), #expression, __FILE__, __LINE__)) { \
            return false; \
        } \
    } while (false)

bool fuzzyEqual(double actual, double expected, double tolerance = 1e-6) {
    return std::abs(actual - expected) <= tolerance;
}

void appendLe32(QByteArray* data, quint32 value) {
    data->append(static_cast<char>(value & 0xff));
    data->append(static_cast<char>((value >> 8) & 0xff));
    data->append(static_cast<char>((value >> 16) & 0xff));
    data->append(static_cast<char>((value >> 24) & 0xff));
}

void appendTlvU32(QByteArray* data, quint16 type, quint32 value) {
    appendLe16(data, type);
    appendLe16(data, 4);
    appendLe32(data, value);
}

bool testBinaryProtocol() {
    PaBinaryProtocol::Frame request;
    request.messageType = PaBinaryProtocol::MessageType::Request;
    request.command = 0x0001;
    request.sequence = 1;

    QByteArray encoded;
    QString error;
    CHECK(PaBinaryProtocol::encode(request, &encoded, &error));
    CHECK(encoded.toHex(' ').toUpper()
        == QByteArrayLiteral("AA 55 01 10 01 00 01 00 01 00 00 00 00 00 00 00 78 62"));

    PaBinaryProtocol::Frame decoded;
    CHECK(PaBinaryProtocol::decode(encoded, &decoded, &error));
    CHECK(decoded.command == 0x0001);
    CHECK(decoded.sequence == 1);
    CHECK(decoded.payload.isEmpty());

    PaBinaryProtocol::Frame payloadFrame = request;
    payloadFrame.messageType = PaBinaryProtocol::MessageType::Done;
    payloadFrame.command = 0x0200;
    payloadFrame.sequence = 42;
    payloadFrame.payload = QByteArray::fromHex("0102030405");
    QByteArray payloadEncoded;
    CHECK(PaBinaryProtocol::encode(payloadFrame, &payloadEncoded, &error));
    payloadEncoded[0] = static_cast<char>(0x99);
    CHECK(!PaBinaryProtocol::decode(payloadEncoded, &decoded, &error));

    PaBinaryProtocol::StreamParser parser;
    const QByteArray stream = QByteArrayLiteral("noise") + encoded + encoded;
    CHECK(parser.feed(stream.left(5), &error).isEmpty());
    const auto frames = parser.feed(stream.mid(5), &error);
    CHECK(frames.size() == 2);
    CHECK(frames.at(0).sequence == 1);
    CHECK(frames.at(1).command == 0x0001);
    CHECK(parser.bufferedBytes() == 0);
    return true;
}

bool testAppSettings() {
    QTemporaryDir directory;
    CHECK(directory.isValid());
    const QString settingsPath = directory.filePath(QStringLiteral("settings.ini"));

    {
        AppSettings settings(settingsPath);
#ifndef Q_OS_WIN
        CHECK(settings.serialPort() == QStringLiteral("/dev/ttyWCH0"));
#endif
        CHECK(settings.serialBaudRate() == 115200);
        settings.setLastImageDirectory(QStringLiteral("/tmp/images"));
        settings.setLastSaveDirectory(QStringLiteral("/tmp/save"));
        settings.setLastExportDirectory(QStringLiteral("/tmp/export"));
        settings.setLastDiagnosticDirectory(QStringLiteral("/tmp/diagnostics"));
        settings.setSerialPort(QStringLiteral("COM_TEST"));
        settings.setSerialBaudRate(921600);
        settings.sync();
    }

    {
        AppSettings settings(settingsPath);
        CHECK(settings.lastImageDirectory() == QStringLiteral("/tmp/images"));
        CHECK(settings.lastSaveDirectory() == QStringLiteral("/tmp/save"));
        CHECK(settings.lastExportDirectory() == QStringLiteral("/tmp/export"));
        CHECK(settings.lastDiagnosticDirectory() == QStringLiteral("/tmp/diagnostics"));
        CHECK(settings.serialPort() == QStringLiteral("COM_TEST"));
        CHECK(settings.serialBaudRate() == 921600);

        settings.setSerialBaudRate(1);
        CHECK(settings.serialBaudRate() == 1200);
    }
    return true;
}

bool testAppLogService() {
    QTemporaryDir directory;
    CHECK(directory.isValid());

    AppLogService service;
    service.setRotationPolicy(1024, 2);
    QVector<AppLogEntry> entries;
    QStringList persistenceErrors;
    QObject::connect(&service, &AppLogService::entryAdded,
        [&entries](const AppLogEntry& entry) {
            entries.push_back(entry);
        });
    QObject::connect(&service, &AppLogService::persistenceError,
        [&persistenceErrors](const QString& message) {
            persistenceErrors.push_back(message);
        });

    QString error;
    CHECK(service.start(directory.path(), &error));
    CHECK(service.isStarted());
    CHECK(service.logDirectory() == directory.path());
    CHECK(service.currentLogPath() == directory.filePath(QStringLiteral("pa_host.log")));

    for (int index = 0; index < 40; ++index) {
        service.info(QStringLiteral("test"),
            QStringLiteral("message-%1-%2")
                .arg(index)
                .arg(QString(80, QLatin1Char('x'))));
    }
    service.error(QStringLiteral("diagnostic"), QStringLiteral("final-marker"));

    CHECK(entries.size() == 41);
    CHECK(entries.first().formatted().contains(QStringLiteral("[INFO] [TEST]")));
    CHECK(entries.last().formatted().contains(QStringLiteral("[ERROR] [DIAGNOSTIC] final-marker")));
    CHECK(persistenceErrors.isEmpty());
    CHECK(QFileInfo::exists(directory.filePath(QStringLiteral("pa_host.log"))));
    CHECK(QFileInfo::exists(directory.filePath(QStringLiteral("pa_host.1.log"))));
    const QStringList logFiles = QDir(directory.path()).entryList(
        {QStringLiteral("pa_host.log"), QStringLiteral("pa_host.*.log")},
        QDir::Files);
    CHECK(logFiles.size() <= 3);

    QMap<QString, QString> metadata;
    metadata.insert(QStringLiteral("fixture"), QStringLiteral("app-log-service"));
    const QString diagnosticPath = directory.filePath(QStringLiteral("diagnostics.txt"));
    CHECK(service.exportDiagnostics(diagnosticPath, metadata, &error));
    QFile diagnosticFile(diagnosticPath);
    CHECK(diagnosticFile.open(QIODevice::ReadOnly));
    const QString diagnostic = QString::fromUtf8(diagnosticFile.readAll());
    CHECK(diagnostic.contains(QStringLiteral("PA Host Diagnostics")));
    CHECK(diagnostic.contains(QStringLiteral("fixture=app-log-service")));
    CHECK(diagnostic.contains(QStringLiteral("[logs]")));
    CHECK(diagnostic.contains(QStringLiteral("final-marker")));
    CHECK(!service.exportDiagnostics(service.currentLogPath(), metadata, &error));
    CHECK(error == QStringLiteral("诊断文件不能覆盖当前日志文件"));
    return true;
}

#if 0 // 旧文本协议测试已移除；主机仅保留二进制协议。
bool testPaDeviceController() {
    struct CommandResult {
        PaProtocol::Command command;
        bool success = false;
        QString detail;
    };

    FakeLineTransport transport;
    PaDeviceController controller(&transport);
    controller.setCommandTimeoutMs(15);
    QVector<CommandResult> results;
    QVector<PaDeviceStatus> statuses;
    QStringList errors;
    QObject::connect(&controller, &PaDeviceController::commandFinished,
        [&results](PaProtocol::Command command, bool success, const QString& detail) {
            results.push_back({command, success, detail});
        });
    QObject::connect(&controller, &PaDeviceController::deviceStatusChanged,
        [&statuses](const PaDeviceStatus& status) {
            statuses.push_back(status);
        });
    QObject::connect(&controller, &PaDeviceController::errorOccurred,
        [&errors](const QString& message) {
            errors.push_back(message);
        });

    QString error;
    CHECK(controller.state() == PaDeviceState::Disconnected);
    CHECK(!controller.sendCommand(PaProtocol::Command::Ping, &error));
    CHECK(error == QStringLiteral("串口未连接"));

    CHECK(controller.connectDevice(QStringLiteral("COM_TEST"), 115200, &error));
    CHECK(controller.isConnected());
    CHECK(controller.state() == PaDeviceState::Ready);
    CHECK(transport.lastPortName == QStringLiteral("COM_TEST"));
    CHECK(transport.lastBaudRate == 115200);

    CHECK(controller.sendCommand(PaProtocol::Command::Status, &error));
    CHECK(controller.state() == PaDeviceState::Busy);
    CHECK(controller.hasPendingCommand());
    CHECK(transport.sentLines.last() == QStringLiteral("STATUS"));
    CHECK(!controller.sendCommand(PaProtocol::Command::Ping, &error));
    CHECK(error == QStringLiteral("上一条命令尚未完成"));
    transport.injectLine(QStringLiteral("OK PONG"));
    CHECK(controller.hasPendingCommand());
    CHECK(controller.state() == PaDeviceState::Busy);

    transport.injectLine(QStringLiteral(
        "OK STATUS int_vector=0x10 pa_version=0x20 com_version=3 rst_state=4 wr_state=2 wr_end=1 corr_state=5 corr_end=0 "
        "model=PA-01 serial=SN0001 arm_version=1.2.3 fpga_version=4.5.6"));
    CHECK(controller.state() == PaDeviceState::Ready);
    CHECK(!controller.hasPendingCommand());
    CHECK(results.size() == 1);
    CHECK(results.last().command == PaProtocol::Command::Status);
    CHECK(results.last().success);
    CHECK(statuses.size() == 1);
    CHECK(statuses.last().valid);
    CHECK(statuses.last().interruptVector == 0x10);
    CHECK(statuses.last().paVersion == 0x20);
    CHECK(statuses.last().communicationVersion == 3);
    CHECK(statuses.last().resetState == 4);
    CHECK(statuses.last().writeState == 2);
    CHECK(statuses.last().correctionState == 5);
    CHECK(statuses.last().model == QStringLiteral("PA-01"));
    CHECK(statuses.last().serialNumber == QStringLiteral("SN0001"));
    CHECK(statuses.last().armVersion == QStringLiteral("1.2.3"));
    CHECK(statuses.last().fpgaVersion == QStringLiteral("4.5.6"));

    CHECK(controller.sendCommand(PaProtocol::Command::Ping, &error));
    transport.injectLine(QStringLiteral(
        "OK STATUS pa=0x21 com=3 rst=4 wr_state=2 wr_end=1 corr_state=5 corr_end=0"));
    CHECK(statuses.size() == 2);
    CHECK(statuses.last().paVersion == 0x21);
    CHECK(controller.hasPendingCommand());
    CHECK(controller.state() == PaDeviceState::Busy);
    transport.injectLine(QStringLiteral("OK PONG"));
    CHECK(controller.state() == PaDeviceState::Ready);
    CHECK(results.size() == 2);
    CHECK(results.last().success);

    CHECK(controller.sendCommand(PaProtocol::Command::Ping, &error));
    QEventLoop timeoutLoop;
    QTimer::singleShot(40, &timeoutLoop, &QEventLoop::quit);
    timeoutLoop.exec();
    CHECK(controller.state() == PaDeviceState::Error);
    CHECK(!controller.hasPendingCommand());
    CHECK(results.size() == 3);
    CHECK(!results.last().success);
    CHECK(results.last().detail.contains(QStringLiteral("超时")));

    // 错误状态下允许重试，成功响应后恢复 Ready。
    CHECK(controller.sendCommand(PaProtocol::Command::Ping, &error));
    transport.injectLine(QStringLiteral("OK PONG"));
    CHECK(controller.state() == PaDeviceState::Ready);
    CHECK(results.last().success);

    // STOP_TRANSFER 会中止本地等待，写出后立即完成且不启动响应超时。
    const int resultsBeforeImmediateStop = results.size();
    CHECK(controller.sendCommand(PaProtocol::Command::StartContinuous, &error));
    CHECK(controller.hasPendingCommand());
    CHECK(controller.state() == PaDeviceState::Busy);
    CHECK(controller.sendCommand(PaProtocol::Command::StopTransfer, &error));
    CHECK(transport.sentLines.last() == QStringLiteral("STOP_TRANSFER"));
    CHECK(!controller.hasPendingCommand());
    CHECK(controller.state() == PaDeviceState::Ready);
    CHECK(results.size() == resultsBeforeImmediateStop + 1);
    CHECK(results.last().command == PaProtocol::Command::StopTransfer);
    CHECK(results.last().success);
    CHECK(results.last().detail.contains(QStringLiteral("无需等待响应")));
    QEventLoop stoppedCommandTimeoutLoop;
    QTimer::singleShot(40, &stoppedCommandTimeoutLoop, &QEventLoop::quit);
    stoppedCommandTimeoutLoop.exec();
    CHECK(controller.state() == PaDeviceState::Ready);
    CHECK(results.size() == resultsBeforeImmediateStop + 1);

    CHECK(controller.sendCommand(PaProtocol::Command::Status, &error));
    transport.injectError(QStringLiteral("模拟链路故障"));
    CHECK(controller.state() == PaDeviceState::Error);
    CHECK(!controller.hasPendingCommand());
    CHECK(!results.last().success);
    CHECK(results.last().detail.contains(QStringLiteral("模拟链路故障")));

    controller.disconnectDevice();
    CHECK(!controller.isConnected());
    CHECK(controller.state() == PaDeviceState::Disconnected);

    FakeLineTransport failedTransport;
    failedTransport.failOpen = true;
    PaDeviceController failedController(&failedTransport);
    CHECK(!failedController.connectDevice(QStringLiteral("BAD"), 9600, &error));
    CHECK(error == QStringLiteral("模拟打开失败"));
    CHECK(failedController.state() == PaDeviceState::Error);
    CHECK(!errors.isEmpty());
    return true;
}
#endif

bool testPaDeviceBinaryBusinessCommands() {
    struct Result {
        quint16 command = 0;
        QMap<quint16, quint32> values;
        bool success = false;
    };
    FakeLineTransport transport;
    PaDeviceController controller(&transport);
    QVector<Result> results;
    QObject::connect(&controller, &PaDeviceController::binaryCommandFinished,
        [&results](quint16 command, const QMap<quint16, quint32>& values,
                   bool success, const QString&) {
            results.push_back({command, values, success});
        });
    QString error;
    CHECK(controller.connectDevice(QStringLiteral("COM_BINARY"), 115200, &error));
    CHECK(controller.beginOffsetCalibration(12, 8, 1, &error));
    CHECK(transport.sentBinaryFrames.last().command == 0x0300);
    CHECK(transport.sentBinaryFrames.last().payload.toHex()
          == QByteArrayLiteral("003004000c00000001300400080000000230010001"));
    PaBinaryProtocol::Frame done;
    done.messageType = PaBinaryProtocol::MessageType::Done;
    done.command = 0x0300;
    done.sequence = transport.sentBinaryFrames.last().sequence;
    appendTlvU32(&done.payload, 0x3000, 12);
    transport.injectBinaryFrame(done);
    CHECK(!controller.hasPendingCommand());
    CHECK(results.size() == 1);
    CHECK(results.last().success);
    CHECK(results.last().values.value(0x3000) == 12);

    CHECK(controller.beginGainCalibration({5000, 10000, 20000}, 4, 0.3f, &error));
    CHECK(transport.sentBinaryFrames.last().command == 0x0304);
    CHECK(transport.sentBinaryFrames.last().payload.contains(QByteArray::fromHex("10310c008813000010270000204e0000")));
    done = {};
    done.messageType = PaBinaryProtocol::MessageType::Done;
    done.command = 0x0304;
    done.sequence = transport.sentBinaryFrames.last().sequence;
    transport.injectBinaryFrame(done);
    CHECK(!controller.hasPendingCommand());

    CHECK(controller.configureTemplateUpload(true, 7680, 3072, &error));
    CHECK(transport.sentBinaryFrames.last().command == 0x0500);
    CHECK(transport.sentBinaryFrames.last().payload.toHex()
          == QByteArrayLiteral("005004000100000002500400001e000003500400000c00000450040000b40000"));
    done = {};
    done.messageType = PaBinaryProtocol::MessageType::Done;
    done.command = 0x0500;
    done.sequence = transport.sentBinaryFrames.last().sequence;
    appendTlvU32(&done.payload, 0x5001, 0x12340000);
    transport.injectBinaryFrame(done);
    CHECK(results.last().command == 0x0500);
    CHECK(results.last().values.value(0x5001) == 0x12340000);

    QMap<quint16, quint32> dynamicValues;
    for (quint16 i = 0; i < 18; ++i) dynamicValues.insert(static_cast<quint16>(0x2100 + i), i + 1);
    const int resultsBeforeChunks = results.size();
    const int framesBeforeChunks = transport.sentBinaryFrames.size();
    CHECK(controller.setConfigGroup(5, dynamicValues, &error));
    CHECK(transport.sentBinaryFrames.size() == framesBeforeChunks + 1);
    for (int chunk = 0; chunk < 3; ++chunk) {
        done = {};
        done.messageType = PaBinaryProtocol::MessageType::Done;
        done.command = 0x0104;
        done.sequence = transport.sentBinaryFrames.last().sequence;
        appendTlvU32(&done.payload, 0x0600, 3);
        transport.injectBinaryFrame(done);
        if (chunk < 2) CHECK(controller.hasPendingCommand());
    }
    CHECK(!controller.hasPendingCommand());
    CHECK(transport.sentBinaryFrames.size() == framesBeforeChunks + 3);
    CHECK(results.size() == resultsBeforeChunks + 1);

    CHECK(controller.queryDynamic(&error));
    CHECK(transport.sentBinaryFrames.last().command == 0x0212);
    done = {};
    done.messageType = PaBinaryProtocol::MessageType::Done;
    done.command = 0x0212;
    done.sequence = transport.sentBinaryFrames.last().sequence;
    appendTlvU32(&done.payload, 0x2003, 1);
    appendTlvU32(&done.payload, 0x2004, 0x26a00000);
    appendTlvU32(&done.payload, 0x2008, 0);
    transport.injectBinaryFrame(done);
    CHECK(results.last().values.value(0x2003) == 1);
    CHECK(results.last().values.value(0x2004) == 0x26a00000);
    return true;
}

bool testPaDeviceBinaryRetryPolicy() {
    FakeLineTransport transport;
    PaDeviceController controller(&transport);
    QString error;
    CHECK(controller.connectDevice(QStringLiteral("COM_RETRY"), 115200, &error));
    controller.setCommandTimeoutMs(10);
    controller.setMaxCommandRetries(2);

    CHECK(controller.sendCommand(PaProtocol::Command::Ping, &error));
    QEventLoop retryLoop;
    QTimer::singleShot(150, &retryLoop, &QEventLoop::quit);
    retryLoop.exec();
    CHECK(transport.sentBinaryFrames.size() == 3);
    CHECK(transport.sentBinaryFrames.at(0).sequence == transport.sentBinaryFrames.at(1).sequence);
    CHECK(transport.sentBinaryFrames.at(1).sequence == transport.sentBinaryFrames.at(2).sequence);
    CHECK(controller.state() == PaDeviceState::Error);
    CHECK(!controller.hasPendingCommand());

    controller.setMaxCommandRetries(0);
    const int sentBeforeNoRetry = transport.sentBinaryFrames.size();
    CHECK(controller.sendCommand(PaProtocol::Command::Ping, &error));
    QEventLoop noRetryLoop;
    QTimer::singleShot(80, &noRetryLoop, &QEventLoop::quit);
    noRetryLoop.exec();
    CHECK(transport.sentBinaryFrames.size() == sentBeforeNoRetry + 1);
    CHECK(controller.state() == PaDeviceState::Error);

    controller.setMaxCommandRetries(2);
    CHECK(controller.sendCommand(PaProtocol::Command::Ping, &error));
    const quint32 sequence = transport.sentBinaryFrames.last().sequence;
    QEventLoop oneRetryLoop;
    QTimer::singleShot(30, &oneRetryLoop, &QEventLoop::quit);
    oneRetryLoop.exec();
    CHECK(transport.sentBinaryFrames.size() >= sentBeforeNoRetry + 2);
    CHECK(transport.sentBinaryFrames.last().sequence == sequence);
    PaBinaryProtocol::Frame done;
    done.messageType = PaBinaryProtocol::MessageType::Done;
    done.command = 0x0002;
    done.sequence = sequence;
    transport.injectBinaryFrame(done);
    CHECK(controller.state() == PaDeviceState::Ready);
    CHECK(!controller.hasPendingCommand());
    return true;
}

#if 0 // 旧文本协议测试已移除；主机仅保留二进制协议。
bool testImageTransferWorkflowController() {
    FakeLineTransport transport;
    PaDeviceController deviceController(&transport);
    ImageTransferWorkflowController workflow(&deviceController);
    QStringList errors;
    QObject::connect(&workflow, &ImageTransferWorkflowController::errorOccurred,
        [&errors](const QString& message) {
            errors.push_back(message);
        });

    QString error;
    CHECK(workflow.state() == ImageTransferState::Disconnected);
    CHECK(workflow.mode() == ImageTransferMode::Manual);
    CHECK(!workflow.canSelectMode());
    CHECK(!workflow.canStart());
    CHECK(!workflow.canStop());

    CHECK(deviceController.connectDevice(QStringLiteral("COM_IMAGE"), 115200, &error));
    deviceController.setCommandTimeoutMs(15);
    CHECK(workflow.state() == ImageTransferState::Ready);
    CHECK(workflow.canSelectMode());
    CHECK(workflow.canStart());

    const int commandsBeforeModeChange = transport.sentLines.size();
    CHECK(workflow.setMode(ImageTransferMode::Continuous, &error));
    CHECK(workflow.mode() == ImageTransferMode::Continuous);
    CHECK(transport.sentLines.size() == commandsBeforeModeChange);

    CHECK(workflow.startTransfer(&error));
    CHECK(workflow.state() == ImageTransferState::StartingContinuous);
    CHECK(transport.sentLines.last() == QStringLiteral("START_CONTINUOUS"));
    CHECK(!workflow.canSelectMode());
    CHECK(!workflow.canStart());
    CHECK(workflow.canStop());
    const int commandsBeforePendingStop = transport.sentLines.size();
    CHECK(workflow.stopTransfer(&error));
    CHECK(transport.sentLines.size() == commandsBeforePendingStop + 1);
    CHECK(transport.sentLines.last() == QStringLiteral("STOP_TRANSFER"));
    CHECK(workflow.state() == ImageTransferState::Ready);
    CHECK(workflow.canSelectMode());
    CHECK(workflow.canStart());
    CHECK(!workflow.canStop());
    CHECK(workflow.mode() == ImageTransferMode::Continuous);

    // 原开始命令的本地超时已经取消，停止后不能再次把界面切回错误状态。
    QEventLoop stoppedCommandTimeoutLoop;
    QTimer::singleShot(40, &stoppedCommandTimeoutLoop, &QEventLoop::quit);
    stoppedCommandTimeoutLoop.exec();
    CHECK(workflow.state() == ImageTransferState::Ready);
    CHECK(workflow.canStart());

    CHECK(workflow.startTransfer(&error));
    transport.injectLine(QStringLiteral("OK START_CONTINUOUS"));
    CHECK(workflow.state() == ImageTransferState::ContinuousRunning);
    CHECK(workflow.canStop());
    CHECK(!workflow.setMode(ImageTransferMode::Manual, &error));
    CHECK(workflow.stopTransfer(&error));
    CHECK(transport.sentLines.last() == QStringLiteral("STOP_TRANSFER"));
    CHECK(workflow.state() == ImageTransferState::Ready);
    CHECK(workflow.canSelectMode());
    CHECK(workflow.canStart());
    CHECK(!workflow.canStop());

    CHECK(deviceController.sendCommand(PaProtocol::Command::StartContinuous, &error));
    transport.injectLine(QStringLiteral("OK START_CONTINUOUS"));
    CHECK(workflow.state() == ImageTransferState::ContinuousRunning);
    CHECK(workflow.canStop());
    CHECK(deviceController.sendCommand(PaProtocol::Command::StopTransfer, &error));
    CHECK(workflow.state() == ImageTransferState::Ready);
    CHECK(workflow.canStart());

    CHECK(deviceController.sendCommand(PaProtocol::Command::StartContinuous, &error));
    transport.injectLine(QStringLiteral("ERR TIMEOUT"));
    CHECK(workflow.state() == ImageTransferState::Error);
    CHECK(workflow.canStop());
    CHECK(workflow.stopTransfer(&error));
    CHECK(workflow.state() == ImageTransferState::Ready);

    CHECK(workflow.setMode(ImageTransferMode::Manual, &error));
    CHECK(workflow.startTransfer(&error));
    CHECK(workflow.state() == ImageTransferState::StartingSingle);
    CHECK(transport.sentLines.last() == QStringLiteral("SEND_SINGLE"));
    CHECK(workflow.canStop());
    const int commandsBeforeSingleStop = transport.sentLines.size();
    CHECK(workflow.stopTransfer(&error));
    CHECK(transport.sentLines.size() == commandsBeforeSingleStop + 1);
    CHECK(transport.sentLines.last() == QStringLiteral("STOP_TRANSFER"));
    CHECK(workflow.state() == ImageTransferState::Ready);
    CHECK(workflow.canSelectMode());
    CHECK(workflow.canStart());
    CHECK(!workflow.canStop());

    CHECK(workflow.startTransfer(&error));
    transport.injectLine(QStringLiteral("OK SEND_SINGLE"));
    CHECK(workflow.state() == ImageTransferState::Ready);

    CHECK(workflow.startTransfer(&error));
    transport.injectLine(QStringLiteral("ERR BUSY"));
    CHECK(workflow.state() == ImageTransferState::Error);
    CHECK(!workflow.canStart());
    CHECK(workflow.canStop());
    CHECK(workflow.stopTransfer(&error));
    CHECK(workflow.state() == ImageTransferState::Ready);
    CHECK(!errors.isEmpty());

    deviceController.disconnectDevice();
    CHECK(workflow.state() == ImageTransferState::Disconnected);
    CHECK(!workflow.canStart());
    return true;
}
#endif

bool testTirawParsingAndRoi() {
    QTemporaryDir directory;
    CHECK(directory.isValid());

    const QVector<quint16> pixels = {
        10, 20, 30, 40,
        50, 60, 70, 80,
        90, 100, 110, 120,
    };
    const QString path = directory.filePath(QStringLiteral("basic.tiraw"));
    CHECK(writeTiraw(path, 4, 3, pixels));

    TiRawImage image;
    QString error;
    CHECK(image.load(path, &error));
    CHECK(error.isEmpty());
    CHECK(image.isValid());
    CHECK(image.version() == 1);
    CHECK(image.bytesPerPixel() == 2);
    CHECK(image.width() == 4);
    CHECK(image.height() == 3);
    CHECK(image.minValue() == 10);
    CHECK(image.maxValue() == 120);

    quint16 value = 0;
    CHECK(image.pixelValue(2, 1, &value));
    CHECK(value == 70);
    CHECK(!image.pixelValue(-1, 0, &value));
    CHECK(!image.pixelValue(4, 0, &value));

    TiRawImage::RoiStats stats;
    CHECK(image.roiStats(QRect(1, 0, 2, 2), &stats));
    CHECK(stats.rect == QRect(1, 0, 2, 2));
    CHECK(stats.pixelCount == 4);
    CHECK(stats.min == 20);
    CHECK(stats.max == 70);
    CHECK(fuzzyEqual(stats.mean, 45.0));
    CHECK(fuzzyEqual(stats.stddev, std::sqrt(425.0)));
    CHECK(fuzzyEqual(stats.rowNoise, 40.0));
    CHECK(fuzzyEqual(stats.rowNoiseStddev, 20.0));
    CHECK(fuzzyEqual(stats.rowNoiseRatio, 2.0));

    const QImage display = image.toDisplayImage(false, 65, 110);
    CHECK(!display.isNull());
    CHECK(display.width() == 4);
    CHECK(display.height() == 3);
    CHECK(display.constScanLine(0)[0] == 0);
    CHECK(display.constScanLine(2)[3] == 255);
    const QImage thumbnail = image.toDisplayImage(false, 65, 110, QSize(2, 2));
    CHECK(thumbnail.size() == QSize(2, 2));
    CHECK(thumbnail.constScanLine(1)[1] == 255);

    QFile file(path);
    CHECK(file.open(QIODevice::ReadOnly));
    TiRawImage memoryImage;
    CHECK(memoryImage.loadData(file.readAll(), QStringLiteral("pcie-frame-1"), &error));
    CHECK(memoryImage.path() == QStringLiteral("pcie-frame-1"));
    CHECK(memoryImage.width() == 4);
    CHECK(memoryImage.height() == 3);
    CHECK(memoryImage.pixelValue(2, 1, &value));
    CHECK(value == 70);

    QByteArray raw16;
    for (const quint16 pixel : pixels) {
        appendLe16(&raw16, pixel);
    }
    TiRawImage rawImage;
    CHECK(rawImage.loadRaw16Data(raw16, 4, 3, QStringLiteral("pcie-raw16"), &error));
    CHECK(rawImage.path() == QStringLiteral("pcie-raw16"));
    CHECK(rawImage.bytesPerPixel() == 2);
    CHECK(rawImage.width() == 4);
    CHECK(rawImage.height() == 3);
    CHECK(rawImage.pixelValue(3, 2, &value));
    CHECK(value == 120);

    const QString exportedTirawPath = directory.filePath(QStringLiteral("exported.tiraw"));
    const QString exportedRawPath = directory.filePath(QStringLiteral("exported.raw"));
    CHECK(image.saveTiRaw(exportedTirawPath, &error));
    CHECK(image.saveRaw16(exportedRawPath, &error));
    CHECK(QFileInfo(exportedTirawPath).size() == 16 + pixels.size() * 2);
    CHECK(QFileInfo(exportedRawPath).size() == pixels.size() * 2);
    TiRawImage exportedImage;
    CHECK(exportedImage.load(exportedTirawPath, &error));
    CHECK(exportedImage.pixelValue(2, 1, &value));
    CHECK(value == 70);
    return true;
}

bool testAutoWindowLevel() {
    QTemporaryDir directory;
    CHECK(directory.isValid());

    QVector<quint16> pixels;
    pixels.reserve(1000);
    for (quint16 value = 0; value < 1000; ++value) {
        pixels.push_back(value);
    }

    const QString path = directory.filePath(QStringLiteral("percentile.tiraw"));
    CHECK(writeTiraw(path, 100, 10, pixels));

    TiRawImage image;
    QString error;
    CHECK(image.load(path, &error));
    CHECK(image.autoWindowLow() == 6);
    CHECK(image.autoWindowHigh() == 993);
    CHECK(image.autoWindowCenter() == 499);
    CHECK(image.autoWindowWidth() == 987);

    const QImage display = image.toDisplayImage(true, 0, 1);
    CHECK(display.constScanLine(0)[0] == 0);
    CHECK(display.constScanLine(9)[99] == 255);
    return true;
}

bool testImageAlgorithmBoundary() {
    QTemporaryDir directory;
    CHECK(directory.isValid());

    QVector<quint16> pixels;
    pixels.reserve(1000);
    for (quint16 value = 0; value < 1000; ++value) {
        pixels.push_back(value);
    }

    const QString path = directory.filePath(QStringLiteral("algorithm-boundary.tiraw"));
    CHECK(writeTiraw(path, 100, 10, pixels));

    TiRawImage image;
    QString error;
    CHECK(image.load(path, &error));

    BuiltinImageAlgorithms algorithms;
    const WindowLevelResult automatic = algorithms.autoWindowLevel(image);
    CHECK(automatic.valid);
    CHECK(automatic.low == 6);
    CHECK(automatic.high == 993);
    CHECK(automatic.center == 499);
    CHECK(automatic.width == 987);

    const WindowLevelResult roi = algorithms.roiWindowLevel(image, QRect(0, 0, 10, 2));
    CHECK(roi.valid);
    CHECK(roi.low == 0);
    CHECK(roi.high == 109);
    CHECK(roi.center == 54);
    CHECK(roi.width == 109);

    CHECK(!algorithms.autoWindowLevel(TiRawImage()).valid);
    CHECK(!algorithms.roiWindowLevel(image, QRect(-10, -10, 2, 2)).valid);
    return true;
}

bool testImageSession() {
    QTemporaryDir directory;
    CHECK(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("session.tiraw"));
    CHECK(writeTiraw(path, 2, 2, {10, 20, 30, 40}));

    ImageSession session(std::make_shared<BuiltinImageAlgorithms>());
    QString error;
    CHECK(session.loadFile(path, &error));
    CHECK(session.hasImage());
    CHECK(session.currentFrame().sourceName == QStringLiteral("session.tiraw"));
    CHECK(session.currentFrame().contentCacheKey == QFileInfo(path).absoluteFilePath());
    CHECK(session.autoWindowLevel().valid);
    CHECK(!session.render(25, 30).isNull());
    TiRawImage::RoiStats stats;
    CHECK(session.roiStats(QRect(0, 0, 2, 2), &stats));
    CHECK(stats.max == 40);
    session.clear();
    CHECK(!session.hasImage());
    return true;
}

bool testImageExportService() {
    QTemporaryDir directory;
    CHECK(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("export-source.tiraw"));
    CHECK(writeTiraw(sourcePath, 2, 2, {10, 20, 30, 40}));

    TiRawImage image;
    QString error;
    CHECK(image.load(sourcePath, &error));

    ImageExportFormat format;
    CHECK(ImageExportService::findFormat(QStringLiteral("tiraw"), &format));
    CHECK(format.suffix == QStringLiteral("tiraw"));
    CHECK(ImageExportService::isSupported(QStringLiteral("raw")));
    CHECK(ImageExportService::ensureFileSuffix(
              directory.filePath(QStringLiteral("raw-export")), QStringLiteral("raw"))
        .endsWith(QStringLiteral(".raw")));

    const QString rawPath = directory.filePath(QStringLiteral("service.raw"));
    CHECK(ImageExportService::exportImage(image, 25, 30, QStringLiteral("raw"), rawPath, &error));
    CHECK(QFileInfo(rawPath).size() == 8);

    if (ImageExportService::isSupported(QStringLiteral("png"))) {
        const QString pngPath = directory.filePath(QStringLiteral("service.png"));
        CHECK(ImageExportService::exportImage(image, 25, 30, QStringLiteral("png"), pngPath, &error));
        QImage exportedDisplay(pngPath);
        CHECK(exportedDisplay.size() == QSize(2, 2));
    }

    CHECK(!ImageExportService::exportImage(
        image, 25, 30, QStringLiteral("unknown"), directory.filePath(QStringLiteral("bad.bin")), &error));
    CHECK(!error.isEmpty());
    return true;
}

bool testReplayPresentationScheduler() {
    ReplayPresentationScheduler scheduler;
    scheduler.setTargetFps(60);
    scheduler.reset();
    CHECK(scheduler.targetFps() == 60);
    CHECK(scheduler.delayMs(0) == 0);

    scheduler.markPresented(0);
    CHECK(scheduler.delayMs(16000000) == 1);
    scheduler.markPresented(17000000);
    CHECK(scheduler.delayMs(32000000) == 2);
    scheduler.markPresented(34000000);
    CHECK(scheduler.delayMs(49000000) == 1);

    scheduler.markPresented(100000000);
    CHECK(scheduler.delayMs(100000000) == 17);
    scheduler.setTargetFps(0);
    CHECK(scheduler.targetFps() == 1);
    scheduler.setTargetFps(1000);
    CHECK(scheduler.targetFps() == 120);
    return true;
}

bool testFramePresentationController() {
    FramePresentationController controller;
    QVector<quint64> presentedSequences;
    QObject::connect(&controller, &FramePresentationController::framePresented,
        [&presentedSequences](const ImageFrame& frame) {
            presentedSequences.push_back(frame.sequence);
        });

    controller.start(30);
    for (quint64 sequence = 1; sequence <= 3; ++sequence) {
        ImageFrame frame;
        frame.sequence = sequence;
        controller.submitFrame(frame);
    }

    QEventLoop firstPresentationLoop;
    QTimer::singleShot(50, &firstPresentationLoop, &QEventLoop::quit);
    firstPresentationLoop.exec();

    FramePresentationStats stats = controller.stats();
    CHECK(presentedSequences == QVector<quint64>({3}));
    CHECK(stats.submittedFrames == 3);
    CHECK(stats.presentedFrames == 1);
    CHECK(stats.droppedFrames == 2);

    ImageFrame pendingFrame;
    pendingFrame.sequence = 4;
    controller.submitFrame(pendingFrame);
    controller.stop();
    stats = controller.stats();
    CHECK(!controller.isActive());
    CHECK(stats.submittedFrames == 4);
    CHECK(stats.presentedFrames == 1);
    CHECK(stats.droppedFrames == 3);

    // 每次开始回放都建立独立统计周期，避免上一次回放污染状态栏数据。
    controller.start(60);
    stats = controller.stats();
    CHECK(controller.isActive());
    CHECK(controller.targetFps() == 60);
    CHECK(stats.submittedFrames == 0);
    CHECK(stats.presentedFrames == 0);
    CHECK(stats.droppedFrames == 0);
    controller.stop();
    return true;
}

bool testImageAcquisitionController() {
    ImageAcquisitionController controller;
    QVector<ImageAcquisitionState> states;
    QVector<quint64> presentedSequences;
    QVector<ImageAcquisitionStats> finishedStats;
    QStringList errors;
    QVector<double> actualFpsValues;
    QObject::connect(&controller, &ImageAcquisitionController::stateChanged,
        [&states](ImageAcquisitionState state) {
            states.push_back(state);
        });
    QObject::connect(&controller, &ImageAcquisitionController::framePresented,
        [&presentedSequences](const ImageFrame& frame) {
            presentedSequences.push_back(frame.sequence);
        });
    QObject::connect(&controller, &ImageAcquisitionController::sessionFinished,
        [&finishedStats](const ImageAcquisitionStats& stats) {
            finishedStats.push_back(stats);
        });
    QObject::connect(&controller, &ImageAcquisitionController::errorOccurred,
        [&errors](const QString& message) {
            errors.push_back(message);
        });
    QObject::connect(&controller, &ImageAcquisitionController::fpsUpdated,
        [&actualFpsValues](double actualFps, int) {
            actualFpsValues.push_back(actualFps);
        });

    FakeImageSource source;
    QString error;
    CHECK(controller.start(&source, 60, &error));
    CHECK(controller.state() == ImageAcquisitionState::Running);
    CHECK(controller.isActive());
    CHECK(controller.isRunning());
    CHECK(controller.targetFps() == 60);
    CHECK(!actualFpsValues.isEmpty());
    CHECK(fuzzyEqual(actualFpsValues.first(), 0.0));

    for (quint64 sequence = 1; sequence <= 3; ++sequence) {
        ImageFrame frame;
        frame.sequence = sequence;
        source.deliverFrame(frame);
    }
    QEventLoop presentationLoop;
    QTimer::singleShot(30, &presentationLoop, &QEventLoop::quit);
    presentationLoop.exec();
    CHECK(presentedSequences == QVector<quint64>({3}));
    ImageAcquisitionStats stats = controller.stats();
    CHECK(stats.source.deliveredFrames == 3);
    CHECK(stats.presentation.submittedFrames == 3);
    CHECK(stats.presentation.presentedFrames == 1);
    CHECK(stats.presentation.droppedFrames == 2);

    source.injectError(QStringLiteral("模拟源警告"));
    CHECK(errors.last() == QStringLiteral("模拟源警告"));
    controller.stop();
    CHECK(controller.state() == ImageAcquisitionState::Idle);
    CHECK(!source.isRunning());
    CHECK(finishedStats.size() == 1);
    CHECK(finishedStats.last().source.deliveredFrames == 3);
    CHECK(finishedStats.last().presentation.presentedFrames == 1);
    controller.stop();
    CHECK(finishedStats.size() == 1);

    CHECK(!controller.start(nullptr, 30, &error));
    CHECK(controller.state() == ImageAcquisitionState::Error);
    CHECK(error == QStringLiteral("图像源未配置"));

    FakeImageSource failedSource;
    failedSource.failStart = true;
    CHECK(!controller.start(&failedSource, 30, &error));
    CHECK(controller.state() == ImageAcquisitionState::Error);
    CHECK(error == QStringLiteral("模拟图像源启动失败"));

    auto* destroyedSource = new FakeImageSource;
    CHECK(controller.start(destroyedSource, 30, &error));
    const int finishedBeforeDestroy = finishedStats.size();
    delete destroyedSource;
    CHECK(controller.state() == ImageAcquisitionState::Error);
    CHECK(errors.last() == QStringLiteral("图像源在会话期间被销毁"));
    CHECK(finishedStats.size() == finishedBeforeDestroy + 1);

    FakeImageSource synchronousSource;
    synchronousSource.emitFrameDuringStart = true;
    synchronousSource.startupFrame.sequence = 10;
    presentedSequences.clear();
    CHECK(controller.start(&synchronousSource, 30, &error));
    QEventLoop synchronousLoop;
    QTimer::singleShot(20, &synchronousLoop, &QEventLoop::quit);
    synchronousLoop.exec();
    CHECK(presentedSequences == QVector<quint64>({10}));
    controller.stop();

    FakeImageSource oldSource;
    FakeImageSource newSource;
    CHECK(controller.start(&oldSource, 30, &error));
    ImageFrame oldFrame;
    oldFrame.sequence = 99;
    oldSource.queueLateFrame(oldFrame);
    controller.stop();
    presentedSequences.clear();
    CHECK(controller.start(&newSource, 30, &error));
    ImageFrame newFrame;
    newFrame.sequence = 100;
    newSource.deliverFrame(newFrame);
    newSource.finish();
    CHECK(presentedSequences == QVector<quint64>({100}));
    CHECK(controller.state() == ImageAcquisitionState::Idle);
    CHECK(!finishedStats.isEmpty());
    CHECK(states.contains(ImageAcquisitionState::Starting));
    CHECK(states.contains(ImageAcquisitionState::Running));
    CHECK(states.contains(ImageAcquisitionState::Stopping));
    return true;
}

bool testInvalidTirawFiles() {
    QTemporaryDir directory;
    CHECK(directory.isValid());

    const QVector<quint16> pixels = {1, 2, 3, 4};
    TiRawImage image;
    QString error;

    const QString badMagicPath = directory.filePath(QStringLiteral("bad-magic.tiraw"));
    CHECK(writeTiraw(badMagicPath, 2, 2, pixels, QByteArrayLiteral("BadMagic")));
    CHECK(!image.load(badMagicPath, &error));
    CHECK(!error.isEmpty());

    const QString badBppPath = directory.filePath(QStringLiteral("bad-bpp.tiraw"));
    CHECK(writeTiraw(badBppPath, 2, 2, pixels, QByteArrayLiteral("TiRayRaw"), 1));
    CHECK(!image.load(badBppPath, &error));

    const QString truncatedPath = directory.filePath(QStringLiteral("truncated.tiraw"));
    CHECK(writeTiraw(truncatedPath, 3, 2, pixels));
    CHECK(!image.load(truncatedPath, &error));

    const QString shortPath = directory.filePath(QStringLiteral("short.tiraw"));
    QFile shortFile(shortPath);
    CHECK(shortFile.open(QIODevice::WriteOnly));
    CHECK(shortFile.write("short") == 5);
    shortFile.close();
    CHECK(!image.load(shortPath, &error));
    return true;
}

bool testMtfAnalysisAndExport() {
    QTemporaryDir directory;
    CHECK(directory.isValid());

    QVector<quint16> pixels;
    pixels.reserve(200);
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 100; ++x) {
            pixels.push_back(x < 50 ? 1000 : 4000);
        }
    }

    const QString imagePath = directory.filePath(QStringLiteral("edge.tiraw"));
    CHECK(writeTiraw(imagePath, 100, 2, pixels));

    TiRawImage image;
    QString error;
    CHECK(image.load(imagePath, &error));

    MtfAnalysisResult result;
    CHECK(MtfAnalysis::analyze(image, QRect(0, 0, 100, 2), &result));
    CHECK(result.esf.y.size() == 396);
    CHECK(result.lsf.y.size() == result.esf.y.size());
    CHECK(result.mtf.y.size() >= 100);
    CHECK(fuzzyEqual(result.esf.x.at(1), 0.025));
    CHECK(fuzzyEqual(result.mtf.y.first(), 1.0));

    const QString outputDirectory = directory.filePath(QStringLiteral("curves"));
    CHECK(MtfAnalysis::exportCsv(outputDirectory, result, &error));
    CHECK(QFile::exists(outputDirectory + QStringLiteral("/esf.csv")));
    CHECK(QFile::exists(outputDirectory + QStringLiteral("/lsf.csv")));
    CHECK(QFile::exists(outputDirectory + QStringLiteral("/mtf.csv")));
    return true;
}
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    const TestCase tests[] = {
        {"binary_protocol", testBinaryProtocol},
        {"app_settings", testAppSettings},
        {"app_log_service", testAppLogService},
        {"pa_device_binary_business_commands", testPaDeviceBinaryBusinessCommands},
        {"pa_device_binary_retry_policy", testPaDeviceBinaryRetryPolicy},
        {"tiraw_parsing_and_roi", testTirawParsingAndRoi},
        {"auto_window_level", testAutoWindowLevel},
        {"image_algorithm_boundary", testImageAlgorithmBoundary},
        {"image_session", testImageSession},
        {"image_export_service", testImageExportService},
        {"replay_presentation_scheduler", testReplayPresentationScheduler},
        {"frame_presentation_controller", testFramePresentationController},
        {"image_acquisition_controller", testImageAcquisitionController},
        {"invalid_tiraw_files", testInvalidTirawFiles},
        {"mtf_analysis_and_export", testMtfAnalysisAndExport},
    };

    int failures = 0;
    for (const TestCase& test : tests) {
        if (test.run()) {
            qInfo("PASS %s", test.name);
            std::fprintf(stderr, "PASS %s\n", test.name);
        } else {
            qCritical("FAIL %s", test.name);
            std::fprintf(stderr, "FAIL %s\n", test.name);
            ++failures;
        }
    }

    qInfo("测试完成: %d 通过, %d 失败", static_cast<int>(std::size(tests)) - failures, failures);
    std::fprintf(stderr, "测试完成: %d 通过, %d 失败\n",
        static_cast<int>(std::size(tests)) - failures, failures);
    return failures == 0 ? 0 : 1;
}
