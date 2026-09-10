# TiRay 43108 SDK 使用说明

## 1. SDK 范围

SDK 为 43108 平板提供不依赖 Qt 的 C99 API 和 C++17 RAII 封装，包含 RS422 二进制协议、Static/Dynamic、重启、配置组、暗场/亮场模板、模板上传、PCIe 图像接收以及同步/异步请求。对外头文件只提供业务接口，不暴露 RS422 帧格式、CRC 和命令号。

## 2. 构建

```bash
cmake -S . -B build -DBUILD_TESTING=ON -DTIRAY_SDK_BUILD_EXAMPLES=ON
cmake --build build -j4
cd build
ctest --output-on-failure
sudo cmake --install .
```

安装后可以使用：

```cmake
find_package(tiray_sdk REQUIRED)
target_link_libraries(app PRIVATE tiray::tiray_sdk)
```

## 3. C++ 基本流程

```cpp
tiray_sdk_config_t config;
tiray_sdk_default_config(&config);
tiray::Sdk sdk(&config);
if (sdk.open() != TIRAY_STATUS_OK) {
    // 处理串口打开失败
}
tiray_device_status_t status{};
sdk.status(status);
```

`tiray::Sdk` 不可复制但支持移动。销毁 SDK 前必须释放所有 `AsyncJob`。

PCIe 接收器只负责接收并复制一帧数据，不会长期持有调用者的图像缓冲区；调用者拥有并负责释放 `tiray_image_frame_t::data`。

## 4. 异步请求

```cpp
auto job = sdk.ping_async(
    [](tiray_status_t result) {
        // 处理 PING 结果
    });
job.wait();
```

异步任务仍使用同步层的互斥、序列号、CRC 校验、响应超时和重试机制。

## 5. 模板制作

暗场制作顺序：

```text
offset_begin -> offset_capture -> cal_status（轮询） -> offset_build
```

亮场制作顺序：

```text
gain_begin -> gain_capture(level1) -> ... -> cal_status -> gain_build
```

`tiray_cal_status_t` 中 `progress_current/progress_total` 用于显示进度，Build 阶段应持续轮询。

## 6. PCIe 图像和图像类型

`tiray_pcie_wait_frame` 返回图像尺寸、图像 ID、最终图像地址以及 BAR0 的 `ADDR_SRC_IMAGE_TYPE`：

- `0`：正常采图；
- `1`：模板上传图像。

调用者需要提供容量至少为 `rows * columns * 2` 字节的 `data` 缓冲区。

## 7. 生命周期约束

1. `tiray_sdk_open` 成功后才能发送命令；
2. 异步任务完成前不能销毁或关闭所属 SDK；
3. PCIe 接收器关闭前不能并发调用 `tiray_pcie_wait_frame`；
4. 所有接口返回 `tiray_status_t`，必须检查返回值；
5. SDK 不负责启动设备端 `pa_controller`，设备端服务由系统启动脚本管理。

## 8. 示例程序

- `examples/basic.cpp`：同步 PING；
- `examples/async.cpp`：异步 PING 回调。
- `examples/hardware_test.cpp`：交互式硬件测试工具，支持手动上图、配置、Dynamic、模板和 PCIe。

示例需要真实 RS422 设备，编译成功不代表硬件通信成功。
