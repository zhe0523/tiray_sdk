# 对外 API 参考

## 初始化

使用 `tiray_sdk_default_config`、`tiray_sdk_create`、`tiray_sdk_open` 初始化 RS422。默认参数为 115200、8N1、500 ms 响应超时和最多 2 次重试。

## 设备控制

`tiray_ping`、`tiray_get_status`、`tiray_reboot`、`tiray_start_static_capture`、`tiray_start_dynamic`、`tiray_stop_dynamic`、`tiray_query_dynamic`。

## 配置

对外程序优先使用 `tiray_get_static_config`、`tiray_set_static_config`、`tiray_get_dynamic_config` 和 `tiray_set_dynamic_config`。Dynamic 的 `cycle` 必须大于 0，图像起始地址不能大于结束地址。

## 模板

暗场使用 `tiray_cal_offset_begin/capture/status/build/cancel`；亮场使用 `tiray_cal_gain_begin/capture/status/build/cancel`。任务状态和进度必须通过 `tiray_cal_status` 轮询。

## PCIe

使用 `tiray_pcie_create/open/start/stop/close` 接收图像。SDK 不维护客户级图像队列；回调中的 `frame->data` 只在回调期间有效，客户必须复制到自己的缓存或队列。`image_type=0` 表示正常图像，`image_type=1` 表示模板上传图像。

## 错误文本

所有返回值都可以通过 `tiray_status_string` 转为中文错误描述。
