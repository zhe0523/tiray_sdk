#pragma once

#include <QObject>
#include <QString>

#include "PaBinaryProtocol.h"

/* 上层控制器只依赖完整二进制帧，不感知 QSerialPort。 */
class ILineTransport : public QObject {
    Q_OBJECT

public:
    explicit ILineTransport(QObject* parent = nullptr);
    ~ILineTransport() override = default;

    virtual bool open(const QString& portName, int baudRate, QString* errorMessage) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual QString portName() const = 0;
    virtual bool sendBinaryFrame(const PaBinaryProtocol::Frame& frame, QString* errorMessage);

signals:
    void errorOccurred(const QString& message);
    void connectionChanged(bool connected);
    void binaryFrameReceived(const PaBinaryProtocol::Frame& frame);
};
