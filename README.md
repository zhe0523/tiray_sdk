# TiRay 43108 SDK

当前仓库是 43108 平板项目的新 SDK，目标是为 C/C++ 等上层应用提供不依赖 Qt 的稳定接口。

当前第一阶段已完成：

- 冻结协议的 C 数据类型和命令号定义；
- 二进制帧编码、解码和 CRC16-CCITT-FALSE；
- 默认设备配置：RS422 115200、响应超时 500 ms、最大重试 2 次；
- 基础协议单元测试。
- RS422 同步请求层：设备打开/关闭、响应匹配、超时重试；
- PING、STATUS、Dynamic、Static、重启和配置组基础接口；
- Offset/Gain 模板制作任务、进度查询和模板上传查询接口；
- Linux PCIe 图像接收接口，解析 BAR0 的图像尺寸、图像地址、图像 ID 和
  `ADDR_SRC_IMAGE_TYPE`（0=正常图片，1=模板上传），并按 DMA 地址表读取图像。
- 轻量 C++17 RAII 封装：`tiray::Sdk` 和 `tiray::PcieReceiver`，不引入 Qt。
- 异步请求接口：后台线程执行单个 RS422 请求，完成后回调，支持等待和安全释放。

后续按以下顺序实现：

1. 扩展 C++ 异步封装和事件分发；
2. 示例程序和 Linux 打包；
3. 在目标设备上进行 RS422/PCIe 联调测试。

构建示例：

```bash
cmake -S . -B build -DTIRAY_SDK_BUILD_EXAMPLES=ON -DBUILD_TESTING=ON
cmake --build build
cd build && ctest --output-on-failure
```

正式客户版本默认只开放公共配置组；研发/产线内部版本使用 `-DTIRAY_SDK_INTERNAL_BUILD=ON` 构建，允许访问全部硬件配置组。

协议依据：`D:\Tiray\43108\pa_controller\pa_controller\doc\PA_Controller_正式通信协议_冻结版V1.0.md`。
