#include "PaBinaryProtocol.h"

namespace {
quint16 readLe16(const QByteArray& data, int offset) {
    return static_cast<quint16>(static_cast<quint8>(data.at(offset)))
        | static_cast<quint16>(static_cast<quint8>(data.at(offset + 1))) << 8;
}

quint32 readLe32(const QByteArray& data, int offset) {
    return static_cast<quint32>(static_cast<quint8>(data.at(offset)))
        | static_cast<quint32>(static_cast<quint8>(data.at(offset + 1))) << 8
        | static_cast<quint32>(static_cast<quint8>(data.at(offset + 2))) << 16
        | static_cast<quint32>(static_cast<quint8>(data.at(offset + 3))) << 24;
}

void appendLe16(QByteArray* data, quint16 value) {
    data->append(static_cast<char>(value & 0xff));
    data->append(static_cast<char>((value >> 8) & 0xff));
}

void appendLe32(QByteArray* data, quint32 value) {
    data->append(static_cast<char>(value & 0xff));
    data->append(static_cast<char>((value >> 8) & 0xff));
    data->append(static_cast<char>((value >> 16) & 0xff));
    data->append(static_cast<char>((value >> 24) & 0xff));
}

void setError(QString* errorMessage, const QString& message) {
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}
}

quint16 PaBinaryProtocol::crc16(const QByteArray& data) {
    quint16 crc = 0xFFFF;
    for (const char byte : data) {
        crc ^= static_cast<quint16>(static_cast<quint8>(byte)) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000u) != 0
                ? static_cast<quint16>((crc << 1) ^ 0x1021u)
                : static_cast<quint16>(crc << 1);
        }
    }
    return crc;
}

bool PaBinaryProtocol::encode(
    const Frame& frame,
    QByteArray* encoded,
    QString* errorMessage) {
    if (encoded == nullptr) {
        setError(errorMessage, QStringLiteral("输出缓冲为空"));
        return false;
    }
    if (frame.payload.size() > MaxPayloadSize) {
        setError(errorMessage, QStringLiteral("payload 超过 2048 字节"));
        return false;
    }
    if (frame.version != Version || frame.headerSize != HeaderSize) {
        setError(errorMessage, QStringLiteral("不支持的协议版本或帧头长度"));
        return false;
    }

    QByteArray body;
    body.reserve(HeaderSize + frame.payload.size());
    appendLe16(&body, Magic);
    body.append(static_cast<char>(frame.version));
    body.append(static_cast<char>(frame.headerSize));
    body.append(static_cast<char>(frame.messageType));
    body.append(static_cast<char>(frame.flags));
    appendLe16(&body, frame.command);
    appendLe32(&body, frame.sequence);
    appendLe16(&body, static_cast<quint16>(frame.payload.size()));
    appendLe16(&body, 0);
    body.append(frame.payload);

    appendLe16(&body, crc16(body));
    *encoded = body;
    return true;
}

bool PaBinaryProtocol::decode(
    const QByteArray& encoded,
    Frame* frame,
    QString* errorMessage) {
    if (frame == nullptr) {
        setError(errorMessage, QStringLiteral("输出帧为空"));
        return false;
    }
    if (encoded.size() < HeaderSize + CrcSize) {
        setError(errorMessage, QStringLiteral("帧长度不足"));
        return false;
    }
    if (readLe16(encoded, 0) != Magic) {
        setError(errorMessage, QStringLiteral("帧头错误"));
        return false;
    }
    if (static_cast<quint8>(encoded.at(2)) != Version
        || static_cast<quint8>(encoded.at(3)) != HeaderSize) {
        setError(errorMessage, QStringLiteral("协议版本或帧头长度错误"));
        return false;
    }

    const quint16 payloadSize = readLe16(encoded, 12);
    const int expectedSize = HeaderSize + static_cast<int>(payloadSize) + CrcSize;
    if (payloadSize > MaxPayloadSize || encoded.size() != expectedSize) {
        setError(errorMessage, QStringLiteral("payload 长度错误"));
        return false;
    }
    if (readLe16(encoded, 14) != 0) {
        setError(errorMessage, QStringLiteral("暂不支持非零 header_crc"));
        return false;
    }

    const quint16 actualCrc = readLe16(encoded, expectedSize - CrcSize);
    const quint16 expectedCrc = crc16(encoded.left(expectedSize - CrcSize));
    if (actualCrc != expectedCrc) {
        setError(errorMessage, QStringLiteral("CRC 校验失败"));
        return false;
    }

    frame->version = static_cast<quint8>(encoded.at(2));
    frame->headerSize = static_cast<quint8>(encoded.at(3));
    frame->messageType = static_cast<MessageType>(static_cast<quint8>(encoded.at(4)));
    frame->flags = static_cast<quint8>(encoded.at(5));
    frame->command = readLe16(encoded, 6);
    frame->sequence = readLe32(encoded, 8);
    frame->payload = encoded.mid(HeaderSize, payloadSize);
    return true;
}

QList<PaBinaryProtocol::Frame> PaBinaryProtocol::StreamParser::feed(
    const QByteArray& data,
    QString* errorMessage) {
    QList<Frame> frames;
    buffer_.append(data);

    while (true) {
        if (buffer_.size() < 2) {
            break;
        }

        const int firstMagicByte = buffer_.indexOf(static_cast<char>(0xAA));
        if (firstMagicByte < 0) {
            buffer_.clear();
            break;
        }
        if (firstMagicByte > 0) {
            buffer_.remove(0, firstMagicByte);
        }
        if (buffer_.size() < 2) {
            break;
        }
        if (static_cast<quint8>(buffer_.at(1)) != 0x55) {
            buffer_.remove(0, 1);
            continue;
        }
        if (buffer_.size() < HeaderSize) {
            break;
        }

        const quint16 payloadSize = readLe16(buffer_, 12);
        const int frameSize = HeaderSize + static_cast<int>(payloadSize) + CrcSize;
        if (payloadSize > MaxPayloadSize || frameSize > MaxFrameSize) {
            setError(errorMessage, QStringLiteral("丢弃超长二进制帧"));
            buffer_.remove(0, 1);
            continue;
        }
        if (buffer_.size() < frameSize) {
            break;
        }

        Frame frame;
        QString decodeError;
        if (!decode(buffer_.left(frameSize), &frame, &decodeError)) {
            setError(errorMessage, decodeError);
            buffer_.remove(0, 1);
            continue;
        }
        frames.append(frame);
        buffer_.remove(0, frameSize);
    }
    return frames;
}

void PaBinaryProtocol::StreamParser::reset() {
    buffer_.clear();
}

int PaBinaryProtocol::StreamParser::bufferedBytes() const {
    return buffer_.size();
}
