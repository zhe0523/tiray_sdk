#include "PaProtocol.h"

QString PaProtocol::commandName(Command command) {
    switch (command) {
    case Command::Ping:
        return QStringLiteral("心跳");
    case Command::Status:
        return QStringLiteral("读取状态");
    case Command::LoadTemplate:
        return QStringLiteral("加载模板");
    case Command::MakeOffset:
        return QStringLiteral("生成 Offset");
    case Command::MakeGain:
        return QStringLiteral("生成 Gain");
    case Command::ConfigTemplate:
        return QStringLiteral("配置模板");
    case Command::StartCorrection:
        return QStringLiteral("启动校正");
    case Command::SendSingle:
        return QStringLiteral("手动上图");
    case Command::StartContinuous:
        return QStringLiteral("开始持续上图");
    case Command::StopTransfer:
        return QStringLiteral("停止上图");
    case Command::StopDynamic:
        return QStringLiteral("停止动态模式");
    }
    return QStringLiteral("未知命令");
}
