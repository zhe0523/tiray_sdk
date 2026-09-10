# 内部研发资料

本目录仅供 TiRay 内部研发、产线和硬件调试使用，不随默认客户版本安装。

客户文档在 `docs/customer/`。完整接口细节（含内部配置组 item）见 `docs/SDK接口与参数说明.md`。

## 维护上位机

`tools/tiray_host` 是依赖 SDK 公共 API 的维护/回归上位机：

```bash
cmake -S . -B build -DTIRAY_SDK_BUILD_HOST=ON
cmake --build build
sudo ./build/tiray_host --port /dev/ttyWCH0
```

- 只调用 `tiray_sdk` 公共 C/C++ API，不实现协议；开关默认关闭，不进客户安装包。
- 研发自有协议栈工具 `pa_host` / `pa_controller` 仍独立维护，不并入本仓库；`tiray_host` 仅用于维护和 SDK 回归。
- 需要内部配置组（2~4）时用 `-DTIRAY_SDK_INTERNAL_BUILD=ON` 构建 SDK 并加 `--internal`。
