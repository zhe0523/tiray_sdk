#pragma once

#include <QSerialPort>
#include <QString>

#include "ILineTransport.h"

/*
 * RS422 串口客户端。
 *
 * RS422 在 Linux 上通常表现为普通 tty 设备，例如 /dev/ttyS1、/dev/ttyUSB0。
 * 这个类只处理串口打开、关闭和二进制帧收发。
 */
class SerialClient final : public ILineTransport {
    Q_OBJECT

public:
    explicit SerialClient(QObject* parent = nullptr);

    bool open(const QString& portName, int baudRate, QString* errorMessage) override;
    void close() override;
    bool isOpen() const override;
    QString portName() const override;

    bool sendBinaryFrame(const PaBinaryProtocol::Frame& frame, QString* errorMessage) override;

private slots:
    void handleReadyRead();
    void handleSerialError(QSerialPort::SerialPortError error);

private:
    QSerialPort serial_;
    PaBinaryProtocol::StreamParser binaryParser_;
};
