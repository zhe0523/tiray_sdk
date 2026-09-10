# TiRay 43108 SDK 接口与参数说明（内部）

本文档含内部配置组明细，不随客户安装包发布。客户 API 以 `docs/customer/API参考.md` 为准。

## 1. 头文件和链接

C 接口头文件为 `tiray_sdk.h`，C++17 封装头文件为 `tiray_sdk.hpp`。安装后 CMake 工程使用：

```cmake
find_package(tiray_sdk REQUIRED)
target_link_libraries(app PRIVATE tiray::tiray_sdk)
```

## 2. 状态码

| 状态码 | 含义 |
|---|---|
| `TIRAY_STATUS_OK` | 成功 |
| `TIRAY_STATUS_INVALID_ARGUMENT` | 参数或缓冲区错误 |
| `TIRAY_STATUS_NOT_OPEN` | 设备未打开 |
| `TIRAY_STATUS_BUSY` | 设备或任务忙 |
| `TIRAY_STATUS_TIMEOUT` | 响应超时 |
| `TIRAY_STATUS_PROTOCOL_ERROR` | 协议格式错误 |
| `TIRAY_STATUS_CRC_ERROR` | CRC 校验失败 |
| `TIRAY_STATUS_IO_ERROR` | 串口、PCIe 或文件读写失败 |
| `TIRAY_STATUS_DEVICE_ERROR` | 下位机拒绝执行并返回错误 |
| `TIRAY_STATUS_INTERNAL_ERROR` | SDK 内部资源或线程错误 |

## 3. RS422 配置和生命周期

`tiray_sdk_default_config` 默认设置为波特率 115200、响应超时 500 ms、最大重试 2 次。调用者设置 `rs422_device` 后，依次调用 `tiray_sdk_create`、`tiray_sdk_open`。使用结束后调用 `tiray_sdk_close`、`tiray_sdk_destroy`。

`rs422_device` 指针必须在 SDK 整个生命周期内有效。所有接口都必须检查返回值。

## 4. 基础设备接口

- `tiray_ping`：测试 RS422 通信；
- `tiray_reboot`：发送设备重启命令；
- `tiray_get_status`：读取工作模式、工作状态、错误码、帧计数和校正状态；
- `tiray_start_static_capture`：启动一次静态采集；
- `tiray_start_dynamic` / `tiray_stop_dynamic`：启动或停止 Dynamic；
- `tiray_query_dynamic`：读取 Dynamic 状态、完成标志、最终地址和帧计数。

设备返回错误时 SDK 返回 `TIRAY_STATUS_DEVICE_ERROR`，应结合 `last_error` 和下位机日志判断原因。

## 5. 配置组接口

`tiray_get_config_group` 读取配置组，`tiray_set_config_group` 下发配置组。配置项结构为：

```text
item_id : uint16_t
value   : uint32_t
```

配置值统一按 `uint32_t` 传递；寄存器和地址建议使用十六进制显示。下发后应重新读取确认。

### 配置组和权限

| 组号 | 名称 | 主要参数 | 对外版 | 内部版 |
|---:|---|---|---|---|
| 1 | Static Idle | 自清空周期、曝光窗口、暗窗口 | 读写 | 读写 |
| 2 | Correction | offset/gain/defect 开关、模板地址、行列和裁剪参数 | 禁止 | 读写 |
| 3 | GIC | 请求码、DOUT、行时序、起止行、binning、OE 时序 | 禁止 | 读写 |
| 4 | ROIC | 起止列、binning、ROIC 寄存器参数 | 禁止 | 读写 |
| 5 | Dynamic | cycle、图像起止地址、超时、10 组 Step High/Low | 读写 | 读写 |

对外版是默认版本，只开放组 1 和组 5，避免客户直接修改 ROIC、GIC 和校正硬件寄存器。内部版通过 CMake 选项 `-DTIRAY_SDK_INTERNAL_BUILD=ON` 构建，再设置 `config.profile = TIRAY_SDK_PROFILE_INTERNAL`，供研发工具和产线调试使用。外部版即使传入 INTERNAL profile 也会拒绝创建 SDK。组 5 中 `cycle` 必须根据实际采集帧数设置，不能使用 0。

配置项明细：

- 组 1 Static Idle：`0x1000 idle_clean_interval_ms`、`0x1001 exposure_window_ms`、`0x1002 dark_window_ms`；
- 组 2 Correction：`0x0300 offset_en`、`0x0301 gain_en`、`0x0302 defect_en`、`0x0303 offset_adder_value`、`0x0304 gain_clipping_value`、`0x0305 pkg_num`、`0x0306 row_num`、`0x0307 col_num`、`0x0308 offset_template_addr`、`0x0309 offset_corr_mode`、`0x030A gain_template_addr`；
- 组 3 GIC：`0x0400 req_code`、`0x0401 dout_en`、`0x0402 line_time_ns`、`0x0403 start_row`、`0x0404 end_row`、`0x0405 binning`、`0x0406 oe_rise_ns`、`0x0407 oe_fall_ns`；
- 组 4 ROIC：`0x0500 start_col`、`0x0501 end_col`、`0x0502 binning`，以及 `0x0503`~`0x0515` 对应的 ROIC 寄存器；
- 组 5 Dynamic：`0x0600 cycle`、`0x0601 image_start_addr`、`0x0602 image_end_addr`、`0x0603 start_timeout_ms`、`0x0604 state_poll_interval_ms`、`0x0605 stop_timeout_ms`，以及 `0x2100/0x2101` 到 `0x2190/0x2191` 的 Step High/Low。

## 6. 暗场模板接口

调用顺序：`tiray_cal_offset_begin` → `tiray_cal_offset_capture` → 轮询 `tiray_cal_status` → `tiray_cal_offset_build`。

`total_frames` 是总帧数，`valid_frames` 是参与均值的有效帧数，必须满足 `0 < valid_frames <= total_frames`。`mode` 为 `0=静态`、`1=动态`。

## 7. 亮场模板接口

调用顺序：`tiray_cal_gain_begin` → 对每个灰度级调用 `tiray_cal_gain_capture` → 轮询 `tiray_cal_status` → `tiray_cal_gain_build`。

`levels` 是灰度级数组，当前最多 16 个；`frames_per_level` 是每级采集帧数；`defect_threshold` 是坏点阈值。

`tiray_cal_status_t` 的 `task_state`：`0=空闲`、`1=运行`、`2=停止中`、`3=成功`、`4=失败`、`5=取消`。进度使用 `progress_current/progress_total`。

## 8. 模板上传

`tiray_img_upload_config` 设置模板类型、地址、行列和包数；`tiray_img_upload_start` 启动上传；`tiray_img_upload_query` 查询上传状态。

`template_kind`：`0=暗场模板`，`1=亮场模板`。

## 9. PCIe 图像接收

通过 `tiray_pcie_default_config`、`tiray_pcie_create`、`tiray_pcie_open` 初始化，再调用 `tiray_pcie_wait_frame` 接收一帧。

Linux 默认会自动在 `/sys/bus/pci/devices` 中查找 vendor `0x1b4d`、device `0x6667` 的 `resource0`，不要求调用者写死 PCI BDF。若产品使用不同 PCI 设备，可在 `tiray_pcie_config_t::bar0_resource` 中显式指定路径。

`tiray_image_frame_t` 字段：

| 字段 | 含义 |
|---|---|
| `rows` / `columns` | 图像尺寸 |
| `image_id` | FPGA 图像 ID |
| `image_type` | `0=正常图像`，`1=模板上传` |
| `final_image_address` | FPGA 最终图像地址 |
| `data_length` | 实际接收字节数 |

调用者提供的 `data` 容量必须至少为 `rows * columns * 2` 字节。

## 10. 异步请求

`tiray_sdk_ping_async` 在后台线程执行 PING，完成后调用 `tiray_async_callback_t(status, user_data)`。使用 `tiray_async_job_wait` 等待，最后使用 `tiray_async_job_destroy` 释放任务。协议命令号、帧格式和响应帧结构不属于对外头文件。

销毁 SDK 前必须先等待并销毁所有异步任务。

PCIe 可使用 `tiray_pcie_start` 注册异步帧回调，使用 `tiray_pcie_stop` 停止。回调中的图像缓存只保证在回调期间有效，客户必须复制数据后再返回。

## 11. 测试程序

```text
./tiray_sdk_hardware_test --port /dev/ttyWCH0
./tiray_sdk_hardware_test --port /dev/ttyWCH0 --config 5
./tiray_sdk_hardware_test --port /dev/ttyWCH0 --dynamic
./tiray_sdk_hardware_test --port /dev/ttyWCH0 --offset
./tiray_sdk_hardware_test --port /dev/ttyWCH0 --gain
./tiray_sdk_hardware_test --port /dev/ttyWCH0 --save /tmp/image.raw
```

默认只执行 PING 和状态读取；Dynamic、模板制作、配置读取和 PCIe 接收均需显式启用。

### 交互模式

仅指定串口时，程序会自动进入交互模式：

```text
sudo ./tiray_sdk_hardware_test --port /dev/ttyWCH0
```

常用命令：

```text
status
static
config_get 5
config_set 5 0x600=10 0x601=0x26a00000
dynamic_start
dynamic_query
dynamic_stop
offset_begin 2 2 0
offset_capture
cal_status
offset_build
gain_begin 5000,10000,20000 2 0.3
gain_capture 5000
gain_capture 10000
gain_capture 20000
gain_build
upload_query
quit
```

对外版配置写入建议只使用组 1 和组 5。组 2~4 只有内部版 SDK 才允许访问。`static` 是一次手动上图命令；模板流程中的 `cal_status` 应在采集或生成阶段重复执行，直到状态变为成功或失败。PCIe 由程序启动的后台线程自动监听，不提供交互式 `pcie` 命令；收到图像后会直接打印 `[PCIe]` 日志。
