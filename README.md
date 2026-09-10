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

客户交付包：

```bash
./scripts/package_sdk.sh 0.1.0
```

协议原文与 FPGA 细节不进入本仓库的客户文档。平台仅 Linux/Kylin。
