# 对外 API 参考

头文件：`tiray_sdk.h`（C99）、`tiray_sdk.hpp`（C++17）。链接目标：`tiray::tiray_sdk`。

下文给出 C 名称。C++ 对照见文末。未列出的符号请勿依赖。

---

## 状态码 `tiray_status_t`

| 值 | 宏 | 含义 |
|---|---|---|
| 0 | `TIRAY_STATUS_OK` | 成功 |
| 1 | `TIRAY_STATUS_INVALID_ARGUMENT` | 空指针、容量不足、非法 cycle/帧数等 |
| 2 | `TIRAY_STATUS_NOT_OPEN` | 未 `open` 或已 `close` |
| 3 | `TIRAY_STATUS_BUSY` | 设备或 PCIe 接收线程忙 |
| 4 | `TIRAY_STATUS_TIMEOUT` | RS422 无匹配响应，或 PCIe 等待事件超时 |
| 5 | `TIRAY_STATUS_PROTOCOL_ERROR` | 响应格式或图像元数据异常 |
| 6 | `TIRAY_STATUS_CRC_ERROR` | 链路校验失败 |
| 7 | `TIRAY_STATUS_IO_ERROR` | 串口/PCIe 节点打开或读写失败 |
| 8 | `TIRAY_STATUS_DEVICE_ERROR` | 下位机拒绝；或对外版访问了未开放配置组 |
| 9 | `TIRAY_STATUS_INTERNAL_ERROR` | 线程/内存等 SDK 内部失败 |

`const char* tiray_status_string(tiray_status_t status);` 返回中文短描述（静态字符串，不要 free）。

---

## 生命周期

```c
void tiray_sdk_default_config(tiray_sdk_config_t* config);
tiray_sdk_t* tiray_sdk_create(const tiray_sdk_config_t* config); /* 失败返回 NULL */
void tiray_sdk_destroy(tiray_sdk_t* sdk);
tiray_status_t tiray_sdk_open(tiray_sdk_t* sdk);
void tiray_sdk_close(tiray_sdk_t* sdk);
int tiray_sdk_is_open(const tiray_sdk_t* sdk);
uint32_t tiray_sdk_last_device_error(const tiray_sdk_t* sdk);
```

`tiray_sdk_config_t`：

| 字段 | 默认 | 说明 |
|---|---|---|
| `rs422_device` | `NULL` | 串口路径，`open` 前必填；指针在 SDK 存活期间有效 |
| `rs422_baudrate` | 115200 | 须为系统支持的标准波特率 |
| `retry.response_timeout_ms` | 500 | 单次等待响应 |
| `retry.max_retries` | 2 | 超时重发次数上限（最多 10） |
| `profile` | `TIRAY_SDK_PROFILE_EXTERNAL` | 客户必须保持对外版。传入 INTERNAL 时客户库 `create` 失败 |

阻塞：`open`/`命令` 会占用内部互斥。`create` 在配置非法时返回 NULL。

---

## 设备控制（均须已 open）

| C API | 作用 | 备注 |
|---|---|---|
| `tiray_ping` | 连通性 | 无 payload |
| `tiray_get_status` | 读工作状态 | 见下表 |
| `tiray_reboot` | 请求设备重启 | 随后链路会断开，需重新 open |
| `tiray_start_static_capture` | 一次静态采图 | 下位机同步完成采图后才返回，SDK 最多等约 30 s。须先 start PCIe |
| `tiray_start_dynamic` / `tiray_stop_dynamic` | Dynamic 启停 | 停机后再发其它会改状态的命令 |
| `tiray_query_dynamic` | Dynamic 状态 | |

`work_mode`（`tiray_work_mode_t`）：

| 值 | 宏 | 含义 |
|---|---|---|
| 0 | `TIRAY_WORK_MODE_IDLE` | 静态 Idle（当前正式静态采图） |
| 7 | `TIRAY_WORK_MODE_CONTINUOUS` | Dynamic / Continuous |

`work_state`（`tiray_work_state_t`）：

| 值 | 宏 |
|---|---|
| 0 | `TIRAY_WORK_STATE_STOPPED` |
| 1 | `TIRAY_WORK_STATE_IDLE_WAIT` |
| 2 | `TIRAY_WORK_STATE_IDLE_CLEANING` |
| 3 | `TIRAY_WORK_STATE_EXPOSURE_WINDOW` |
| 4 | `TIRAY_WORK_STATE_BRIGHT_CAPTURE` |
| 5 | `TIRAY_WORK_STATE_DARK_WINDOW` |
| 6 | `TIRAY_WORK_STATE_DARK_CAPTURE` |
| 7–10 | Dynamic 启动/运行/停止/完成 |
| 11 | `TIRAY_WORK_STATE_ERROR` |

`last_error` 是设备工作线程记录的 errno 风格现场（如超时），与协议 ERROR 帧不是同一套编号。

协议 ERROR 帧 TLV `0x0002` 由 `tiray_sdk_last_device_error` 读取，并映射为：

| 设备码 | SDK 返回 |
|---|---|
| 0x0002 | `INVALID_ARGUMENT` |
| 0x0005 | `TIMEOUT` |
| 0x0008 | `BUSY` |
| 0x0001 / 0x000E 及其它 | `DEVICE_ERROR` |

`tiray_device_status_t` 其它字段：

| 字段 | 说明 |
|---|---|
| `capture_id` / `frame_count` | 采集序号与帧计数 |
| `output_addr` / `offset_addr` | 输出图 / offset 模板地址（诊断） |
| `write_state` / `write_end` | 写通道状态 |
| `correction_state` / `correction_end` | 校正通道状态 |

`tiray_dynamic_status_t`：`state`、`end`（完成标志）、`debug_out`、`final_image_addr`、`frame_count`。

---

## 配置（客户请用高层接口）

```c
tiray_status_t tiray_get_static_config(tiray_sdk_t*, tiray_static_config_t*);
tiray_status_t tiray_set_static_config(tiray_sdk_t*, const tiray_static_config_t*);
tiray_status_t tiray_get_dynamic_config(tiray_sdk_t*, tiray_dynamic_config_t*);
tiray_status_t tiray_set_dynamic_config(tiray_sdk_t*, const tiray_dynamic_config_t*);
```

Static：`idle_clean_interval_ms`、`exposure_window_ms`、`dark_window_ms`。

Dynamic：

| 字段 | 约束 |
|---|---|
| `cycle` | 必须 `> 0` |
| `image_start_addr` / `image_end_addr` | start ≤ end |
| `start_timeout_ms` / `state_poll_interval_ms` / `stop_timeout_ms` | 设备侧超时与轮询 |
| `step_high[10]` / `step_low[10]` | 10 组步进；不明含义时保持读回值 |

下发后应再 `get_*` 核对。

`tiray_get_config_group` / `tiray_set_config_group`：对外版仅组 **1** 和 **5**。其它组号返回 `TIRAY_STATUS_INVALID_ARGUMENT`。新代码不要调用它们。

---

## 模板

暗场：`tiray_cal_offset_begin(sdk, total_frames, valid_frames, mode)`  
→ `tiray_cal_offset_capture` → 轮询 `tiray_cal_status` → `tiray_cal_offset_build` / `tiray_cal_offset_cancel`。

亮场：`tiray_cal_gain_begin(sdk, levels, level_count, frames_per_level, defect_threshold)`  
`level_count` 1–16，`frames_per_level > 0`。然后按级 `tiray_cal_gain_capture(sdk, level)`，再 `build`/`cancel`。

`tiray_cal_status_t::task_state`：

| 值 | 含义 |
|---|---|
| 0 | 空闲 |
| 1 | 运行 |
| 2 | 停止中 |
| 3 | 成功 |
| 4 | 失败 |
| 5 | 取消 |

进度用 `progress_current / progress_total`。`last_error`、`bad_pixel_count` 用于结果展示。

上传：`tiray_img_upload_config(kind, addr, rows, columns, package_count)`  
`kind`：0 暗场模板，1 亮场模板。然后 `start` + `query`。

---

## PCIe

```c
void tiray_pcie_default_config(tiray_pcie_config_t* config);
tiray_pcie_receiver_t* tiray_pcie_create(const tiray_pcie_config_t* config);
void tiray_pcie_destroy(tiray_pcie_receiver_t*);
tiray_status_t tiray_pcie_open(tiray_pcie_receiver_t*);
void tiray_pcie_close(tiray_pcie_receiver_t*);
tiray_status_t tiray_pcie_wait_frame(tiray_pcie_receiver_t*, tiray_image_frame_t* frame);
tiray_status_t tiray_pcie_start(tiray_pcie_receiver_t*, tiray_pcie_frame_callback_t, void* user_data);
void tiray_pcie_stop(tiray_pcie_receiver_t*);
int tiray_pcie_is_running(const tiray_pcie_receiver_t*);
```

`tiray_pcie_config_t` 默认：`event_device=/dev/idma0_event_0`，`c2h_device=/dev/idma0_c2h_0`，`bar0_resource=NULL`（自动发现），`wait_timeout_ms=1000`，`fallback_rows=7680`，`fallback_columns=3072`。

`tiray_image_frame_t`：

| 字段 | 说明 |
|---|---|
| `data` / `data_capacity` / `data_length` | 像素缓冲；wait_frame 由调用者提供 |
| `rows` / `columns` | 图像尺寸 |
| `image_id` | 帧序号 |
| `image_type` | 0 正常图，1 模板上传图 |
| `final_image_address` | 诊断用地址，业务层可忽略 |

`create` 在自动发现映射资源失败时返回 NULL，后续 `open` 为 `INVALID_ARGUMENT`，按《系统依赖与权限》排查。

`wait_frame`：调用前设置 `frame->data` 与足够的 `data_capacity`；函数把一帧 RAW16 写入该缓冲并填写 `rows/columns/data_length`。与 `start` 互斥。

`start` 回调禁止调用任何 RS422 API，禁止保存 `frame` 指针。

---

## 异步（仅 PING）

```c
tiray_status_t tiray_sdk_ping_async(tiray_sdk_t*, tiray_async_callback_t, void* user_data, tiray_async_job_t** job);
tiray_status_t tiray_async_job_wait(tiray_async_job_t* job);
void tiray_async_job_destroy(tiray_async_job_t* job);
```

回调在后台线程执行。销毁 SDK 前必须 wait + destroy。其它命令没有 async 版本。

---

## C++ 对照

| C | C++ |
|---|---|
| `tiray_sdk_create/open/close/destroy` | `tiray::Sdk` 构造 / `open` / `close` / 析构 |
| `tiray_ping` | `Sdk::ping` |
| `tiray_get_status` | `Sdk::status` |
| `tiray_start_static_capture` | `Sdk::start_static` |
| `tiray_start_dynamic` / `stop` / `query_dynamic` | `start_dynamic` / `stop_dynamic` / `dynamic_status` |
| `tiray_get/set_static_config` | `get/set_static_config` |
| `tiray_cal_offset_*` | `offset_begin/capture/build/cancel` |
| `tiray_cal_gain_*` | `gain_begin/capture/build/cancel` |
| `tiray_cal_status` | `calibration_status` |
| `tiray_sdk_ping_async` | `Sdk::ping_async` → `AsyncJob` |
| `tiray_pcie_*` | `tiray::PcieReceiver` |

`PcieReceiver::start` 仍使用 C 回调函数指针（`void(*)(const tiray_image_frame_t*, void*)`）。
