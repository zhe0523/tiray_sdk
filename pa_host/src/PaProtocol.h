#pragma once

#include <QMetaType>
#include <QString>

/*
 * PA/ARM 串口协议定义。
 *
 * 保留业务命令枚举和界面显示名称；线上串口收发统一由 PaBinaryProtocol 处理。
 */
class PaProtocol {
public:
    enum class Command {
        Ping,
        Status,
        LoadTemplate,
        MakeOffset,
        MakeGain,
        ConfigTemplate,
        StartCorrection,
        SendSingle,
        StartContinuous,
        StopTransfer,
        StopDynamic
    };

    static QString commandName(Command command);
};

Q_DECLARE_METATYPE(PaProtocol::Command)
