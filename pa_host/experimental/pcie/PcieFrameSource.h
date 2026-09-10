#pragma once

// EXPERIMENTAL ONLY: intentionally excluded from CMakeLists.txt.
// Do not include this header from pa_host production sources yet.

#include <QByteArray>
#include <QObject>
#include <QSize>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include <atomic>
#include <thread>

enum class Raw12Packing {
    // Byte0=P0[11:4], Byte1=P1[11:8]:P0[3:0], Byte2=P1[7:0].
    MsbFirstTwoPixelsThreeBytes,
};

struct PcieFrameLayout {
    QByteArray syncWord;
    int headerSize = 0;
    int frameNumberOffset = 0;
    int payloadLengthOffset = 0;
    int timestampOffset = 0;
    QSize imageSize;
    int bitsPerPixel = 12;
    Raw12Packing raw12Packing = Raw12Packing::MsbFirstTwoPixelsThreeBytes;
    quint32 expectedPayloadBytes = 0;
    quint32 maximumPayloadBytes = 0;

    bool isValid() const;
};

struct PcieFrameMetadata {
    quint32 frameNumber = 0;
    quint64 timestampUs = 0;
    quint64 receivedTimestampUs = 0;
    quint64 droppedBeforeThisFrame = 0;
};

struct PcieRawFrame {
    PcieFrameMetadata metadata;
    QByteArray payload;
};

class PcieFrameParser {
public:
    explicit PcieFrameParser(PcieFrameLayout layout);

    void append(const QByteArray& bytes);
    bool takeNext(PcieRawFrame* frame, QString* errorMessage);
    void reset();

    quint64 syncErrorCount() const;
    quint64 protocolDropCount() const;

private:
    bool parseHeader(PcieFrameMetadata* metadata, quint32* payloadLength, QString* errorMessage) const;
    void discardBeforeSync();

    PcieFrameLayout layout_;
    QByteArray buffer_;
    quint32 lastFrameNumber_ = 0;
    bool hasLastFrameNumber_ = false;
    quint64 syncErrors_ = 0;
    quint64 protocolDrops_ = 0;
};

class Raw12Unpacker {
public:
    static bool unpack(
        const QByteArray& payload,
        const QSize& imageSize,
        Raw12Packing packing,
        QVector<quint16>* pixels,
        QString* errorMessage);
};

class PcieFrameSource final : public QObject {
    Q_OBJECT

public:
    explicit PcieFrameSource(QObject* parent = nullptr);
    ~PcieFrameSource() override;

    bool start(const QString& devicePath, const PcieFrameLayout& layout, QString* errorMessage);
    void stop();
    bool isRunning() const;

signals:
    void frameReady(const PcieRawFrame& frame);
    void sourceError(const QString& message);
    void sourceStopped();

private:
    void readLoop(QString devicePath, PcieFrameLayout layout);

    std::atomic_bool running_{false};
    std::thread readerThread_;
};

Q_DECLARE_METATYPE(PcieRawFrame)
