#include "ILineTransport.h"

ILineTransport::ILineTransport(QObject* parent)
    : QObject(parent) {
}

bool ILineTransport::sendBinaryFrame(
    const PaBinaryProtocol::Frame&,
    QString* errorMessage) {
    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("当前传输不支持二进制协议");
    }
    return false;
}
