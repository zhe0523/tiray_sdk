# tiray_host —— 依赖 SDK 的维护上位机

`tiray_host` 是 TiRay 43108 的维护与回归测试工具，**只调用 `tiray_sdk` 公共 C/C++ API**（`tiray_sdk.h` / `tiray_sdk.hpp`），不实现、不拼接任何私有协议细节。

## 定位与边界

- **用途**：产线/现场维护、SDK 升级后的回归验证（一条命令即可跑完 PING、状态、配置、模板流程）。
- **不是** `pa_host` / `pa_controller`：研发自有协议栈工具不在本仓库，继续按原方式独立维护；本仓库不合并、不替代它们。
- 不依赖 Qt；仅链接本仓库的 `tiray_sdk` 静态库。
- 默认使用对外版配置组（组 1/5）；以 `-DTIRAY_SDK_INTERNAL_BUILD=ON` 构建 SDK 并加 `--internal` 时，可访问组 2~4，供研发调试。

## 构建

```bash
cmake -S . -B build -DTIRAY_SDK_BUILD_HOST=ON
cmake --build build
# 目标：build/tiray_host
```

如需内部版配置组访问，SDK 本体也要用内部版构建：

```bash
cmake -S . -B build -DTIRAY_SDK_INTERNAL_BUILD=ON -DTIRAY_SDK_BUILD_HOST=ON
```

`TIRAY_SDK_BUILD_HOST` 默认关闭，不影响客户交付包（`scripts/package_sdk.sh` 不开启该开关）。

## 使用

串口访问通常需要 root 或 `dialout` 组：

```bash
sudo ./build/tiray_host --port /dev/ttyWCH0          # 交互模式
sudo ./build/tiray_host --port /dev/ttyWCH0 ping status   # 一次性回归检查
```

常用选项：

| 选项 | 说明 |
|---|---|
| `--port PATH` | RS422 串口，默认 `/dev/ttyWCH0` |
| `--baud RATE` | 波特率，默认 115200 |
| `--timeout MS` | 响应超时，默认 500（重试 2 次，重试超时加倍） |
| `--internal` | 内部版配置（需内部版 SDK 构建） |
| `--pcie` / `--save PATH` | 启动 PCIe 帧监听/保存原始帧（仅维护诊断，生产请用 SDK PCIe 接口） |

## 命令

交互模式与一次性模式命令一致（`help` 输出为准）：

```text
ping / status / reboot
static
dynamic_start / dynamic_query / dynamic_stop
config_get GROUP
config_set GROUP ID=VALUE [ID=VALUE...]
static_cfg_get / static_cfg_set IDLE_MS EXPOSURE_MS DARK_MS
dynamic_cfg_get / dynamic_cfg_set CYCLE [START_ADDR END_ADDR ...]
offset_begin TOTAL VALID MODE / offset_capture / offset_build / offset_cancel
gain_begin 5000,10000,20000 2 0.3 / gain_capture LEVEL / gain_build / gain_cancel
cal_status
upload_config KIND ADDR ROWS COLS PKGS / upload_start / upload_query
```

约定：

- 一次性模式下，任一命令失败进程返回非零，便于脚本化回归；
- `cal_status` 需轮询到 `成功/失败`；
- 对外版配置组仅组 1 和组 5 可写，组 2~4 需内部版；
- 静态采集为一次 `static` 触发命令，图像经 PCIe 接收，不在本工具中做模板流程编排；
- 客户侧使用方式以 `docs/customer/` 为准，本 README 不随客户安装包发布。
