/*
 * tiray_host_gui 入口。
 *
 * 维护上位机 GUI：控制链路只调用 tiray_sdk 公共 C API（SdkWorker 工作线程），
 * 图像来自 SDK PCIe 接收接口（PcieMonitor），界面参考 pa_host 的布局习惯。
 * 私有协议栈工具（pa_host / pa_controller）不在此程序中实现。
 */

#include <QApplication>
#include <QMetaType>
#include <QVector>

#include "MainWindow.h"
#include "PcieMonitor.h"

#include "tiray_sdk.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("tiray_host_gui"));
    QApplication::setOrganizationName(QStringLiteral("TiRay"));

    /* 跨线程队列信号需要的类型注册。 */
    qRegisterMetaType<tiray_device_status_t>("tiray_device_status_t");
    qRegisterMetaType<tiray_dynamic_status_t>("tiray_dynamic_status_t");
    qRegisterMetaType<tiray_static_config_t>("tiray_static_config_t");
    qRegisterMetaType<tiray_dynamic_config_t>("tiray_dynamic_config_t");
    qRegisterMetaType<tiray_cal_status_t>("tiray_cal_status_t");
    qRegisterMetaType<tiray_image_upload_status_t>("tiray_image_upload_status_t");
    qRegisterMetaType<tiray_config_item_t>("tiray_config_item_t");
    qRegisterMetaType<QVector<tiray_config_item_t>>("QVector<tiray_config_item_t>");
    qRegisterMetaType<QVector<quint32>>("QVector<quint32>");
    qRegisterMetaType<QSharedPointer<PcieFrame>>("QSharedPointer<PcieFrame>");

    MainWindow window;
    window.show();
    return app.exec();
}
