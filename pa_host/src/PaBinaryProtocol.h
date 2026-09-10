#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

/*
 * PA Controller 正式二进制协议基础层。
 *
 * 提供与 ARM 端一致的帧编解码和串口字节流重同步。
 */
class PaBinaryProtocol final {
public:
    static constexpr quint16 Magic = 0x55AA;
    static constexpr quint8 Version = 1;
    static constexpr quint8 HeaderSize = 16;
    static constexpr int MaxPayloadSize = 2048;
    static constexpr int CrcSize = 2;
    static constexpr int MaxFrameSize = HeaderSize + MaxPayloadSize + CrcSize;

    enum class MessageType : quint8 {
        Request = 0x01,
        Ack = 0x02,
        Done = 0x03,
        Error = 0x04,
        Event = 0x05,
    };

    struct Frame {
        quint8 version = Version;
        quint8 headerSize = HeaderSize;
        MessageType messageType = MessageType::Request;
        quint8 flags = 0;
        quint16 command = 0;
        quint32 sequence = 0;
        QByteArray payload;
    };

    static quint16 crc16(const QByteArray& data);
    static bool encode(const Frame& frame, QByteArray* encoded, QString* errorMessage = nullptr);
    static bool decode(const QByteArray& encoded, Frame* frame, QString* errorMessage = nullptr);

    class StreamParser final {
    public:
        QList<Frame> feed(const QByteArray& data, QString* errorMessage = nullptr);
        void reset();
        int bufferedBytes() const;

    private:
        QByteArray buffer_;
    };
};
