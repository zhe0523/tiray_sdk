# TiRay 43108 SDK（研发仓库）

本仓库同时包含客户可交付部分与内部实现。

- **发给客户的内容**：安装后的 `include/`、`lib/`、以及 `docs/customer/`（由 `scripts/package_sdk.sh` 打包）。
- **不要发给客户**：`src/`、`docs/SDK*.md`、`docs/internal/`、本 README 中的研发路线、内部 CMake 开关说明。

客户文档入口：`docs/customer/README.md`。

## 研发构建

```bash
cmake -S . -B build -DTIRAY_SDK_BUILD_EXAMPLES=ON -DBUILD_TESTING=ON
cmake --build build
cd build && ctest --output-on-failure
```

内部版（全部配置组，仅产线/研发）：

```bash
cmake -S . -B build -DTIRAY_SDK_INTERNAL_BUILD=ON -DTIRAY_SDK_BUILD_EXAMPLES=ON
```

维护上位机 `tiray_host`（依赖 SDK 公共 API 的维护/回归工具，默认关闭，不进客户包）：

```bash
# 命令行版（回归脚本用）
cmake -S . -B build -DTIRAY_SDK_BUILD_HOST=ON

# Qt 界面版（维护主用，Kylin 上 Qt5/Qt6 均可，见 tools/tiray_host/README.md）
cmake -S . -B build -DTIRAY_SDK_BUILD_HOST=ON -DTIRAY_SDK_BUILD_HOST_GUI=ON
cmake --build build   # 目标 tiray_host / tiray_host_gui
```

研发自有协议栈工具 `pa_host`（见 `pa_host/` 目录）/ `pa_controller` 不并入本工具；`tiray_host` 只用于维护和 SDK 回归，其界面布局参考 `pa_host`。

客户交付包：

```bash
./scripts/package_sdk.sh 0.1.0
```

协议原文与 FPGA 细节不进入本仓库的客户文档。平台仅 Linux/Kylin。
