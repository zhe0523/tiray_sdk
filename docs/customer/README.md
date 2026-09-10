# TiRay 43108 SDK 客户文档

版本：0.1.0  
平台：仅 Linux / 银河麒麟（Kylin）。不支持 Windows。  
语言：C99 公共接口 + C++17 RAII 封装。不依赖 Qt。

本目录随安装包发布。客户只需要：

- 头文件 `tiray_sdk.h` / `tiray_sdk.hpp`
- 静态库 `libtiray_sdk.a` 与 CMake 配置 `tiray::tiray_sdk`
- 本目录文档和 `examples/customer_demo.cpp`

不需要 SDK 源码、通信协议文本或 FPGA 寄存器说明。

## 阅读顺序

1. [快速开始.md](快速开始.md) — 安装、CMake 接入、编译示例  
2. [客户集成手册.md](客户集成手册.md) — 推荐业务流程与内存责任  
3. [API参考.md](API参考.md) — 每个公开接口的参数与约束  
4. [错误恢复说明.md](错误恢复说明.md) — 返回值处理  
5. [系统依赖与权限.md](系统依赖与权限.md) — 设备节点、udev  
6. [常见问题.md](常见问题.md) — 编译失败、ping 超时、有命令无图像  
7. `examples/customer_demo.cpp` — 打开 → ping → 收一帧（串口路径用命令行传入）  

## 已知限制

- 需要真实 43108 设备；编译成功不代表通信成功。  
- 设备端 `pa_controller` 由系统服务启动，SDK 不负责拉起下位机。  
- 异步接口目前仅 `ping_async`；采图、配置、模板均为同步请求。  
- 客户构建只允许 Static（组 1）和 Dynamic（组 5）配置；请使用高层 `get/set_*_config`。  
- 像素格式为小端 RAW16，默认回退尺寸 7680×3072（约 45 MiB/帧）。  

技术支持以合同约定渠道为准。
