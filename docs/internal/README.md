# 内部研发资料

本目录仅供 TiRay 内部研发、产线和硬件调试使用，不随默认客户版本安装。

客户文档在 `docs/customer/`。完整接口细节（含内部配置组 item）见 `docs/SDK接口与参数说明.md`。

## 维护上位机

`tools/tiray_host` 是依赖 SDK 公共 API 的维护/回归上位机，含命令行版（目标 `tiray_host`）与 Qt 界面版（目标 `tiray_host_gui`，面向 Kylin，Qt5/Qt6）：

```bash
cmake -S . -B build -DTIRAY_SDK_BUILD_HOST=ON -DTIRAY_SDK_BUILD_HOST_GUI=ON
cmake --build build
sudo ./build/tiray_host_gui                     # 界面版（维护主用）
sudo ./build/tiray_host --port /dev/ttyWCH0     # 命令行版（脚本化回归）
```

- 只调用 `tiray_sdk` 公共 C/C++ API，不实现协议；开关默认关闭，不进客户安装包。
- 界面版布局参考 `pa_host/`；`pa_host` / `pa_controller` 作为研发自有协议栈继续独立维护，不并入本工具；`tiray_host` 仅用于维护和 SDK 回归。
- 需要内部配置组（2~4）时用 `-DTIRAY_SDK_INTERNAL_BUILD=ON` 构建 SDK：命令行版加 `--internal`，界面版勾选“内部版配置”。
