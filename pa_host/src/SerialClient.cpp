#include "SerialClient.h"

#include <QSerialPortInfo>

SerialClient::SerialClient(QObject* parent)
    : ILineTransport(parent) {
    connect(&serial_, &QSerialPort::readyRead, this, &SerialClient::handleReadyRead);
    connect(&serial_, &QSerialPort::errorOccurred, this, &SerialClient::handleSerialError);
}

bool SerialClient::open(const QString& portName, int baudRate, QString* errorMessage) {
    if (serial_.isOpen()) {
        serial_.close();
    }

    binaryParser_.reset();
    serial_.setPortName(portName);
    serial_.setBaudRate(baudRate);
    serial_.setDataBits(QSerialPort::Data8);
    serial_.setParity(QSerialPort::NoParity);
    serial_.setStopBits(QSerialPort::OneStop);
    serial_.setFlowControl(QSerialPort::NoFlowControl);

    if (!serial_.open(QIODevice::ReadWrite)) {
        if (errorMessage != nullptr) {
            *errorMessage = serial_.errorString();
        }
        emit connectionChanged(false);
        return false;
    }

    emit connectionChanged(true);
    return true;
}

void SerialClient::close() {
    if (serial_.isOpen()) {
        serial_.close();
    }
    emit connectionChanged(false);
}

bool SerialClient::isOpen() const {
    return serial_.isOpen();
}

QString SerialClient::portName() const {
    return serial_.portName();
}

bool SerialClient::sendBinaryFrame(
    const PaBinaryProtocol::Frame& frame,
    QString* errorMessage) {
    if (!serial_.isOpen()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("串口未打开");
        }
        return false;
    }
    QByteArray encoded;
    QString encodeError;
    if (!PaBinaryProtocol::encode(frame, &encoded, &encodeError)) {
        if (errorMessage != nullptr) {
            *errorMessage = encodeError;
        }
        return false;
    }
    const qint64 written = serial_.write(encoded);
    if (written != encoded.size()) {
        if (errorMessage != nullptr) {
            *errorMessage = serial_.errorString();
        }
        return false;
    }
    return true;
}

void SerialClient::handleReadyRead() {
    QString parserError;
    const auto frames = binaryParser_.feed(serial_.readAll(), &parserError);
    for (const auto& frame : frames) {
        emit binaryFrameReceived(frame);
    }
    if (!parserError.isEmpty()) {
        emit errorOccurred(QStringLiteral("二进制协议: %1").arg(parserError));
    }
}

void SerialClient::handleSerialError(QSerialPort::SerialPortError error) {
    if (error == QSerialPort::NoError) {
        return;
    }
    emit errorOccurred(serial_.errorString());
}
