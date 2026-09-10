# tiray_host —— 依赖 SDK 的维护上位机

`tiray_host` 是 TiRay 43108 的维护与回归测试上位机，**只调用 `tiray_sdk` 公共 C/C++ API**（`tiray_sdk.h` / `tiray_sdk.hpp`），不实现、不拼接任何私有协议细节。包含两部分：

| 目标 | 内容 | 说明 |
|---|---|---|
| `tiray_host` | 命令行版（`main.cpp`） | 无界面，交互/一次性回归模式，脚本化回归用；可视为 GUI 的 demo |
| `tiray_host_gui` | Qt 界面版（`gui/`） | 维护主用工具，界面布局参考 `pa_host`，实际运行环境为 Kylin |

## 定位与边界

- **用途**：产线/现场维护、SDK 升级后的回归验证。
- **不是** `pa_host` / `pa_controller`：研发自有协议栈工具在 `pa_host/` 目录独立维护，本工具不合并、不替代它们；`pa_host` 仅作为界面参考。
- 控制链路只调 `tiray_sdk` 公共 API：RS422 命令通过 `SdkWorker` 工作线程执行（不阻塞界面），图像通过 SDK 的 `tiray_pcie_*` 接口接收。
- 默认对外版配置组（组 1/5）；以 `-DTIRAY_SDK_INTERNAL_BUILD=ON` 构建 SDK 并勾选“内部版配置”时，可访问组 2~4，供研发调试。

## 构建

Qt 界面版依赖 Qt5/Qt6 Widgets（优先 Qt6，自动回退 Qt5；Kylin V10 SP1 装 Qt5 即可）：

```bash
sudo apt install -y qtbase5-dev qtbase5-dev-tools   # Kylin/apt 环境；dnf/yum 用对应包名
cmake -S . -B build -DTIRAY_SDK_BUILD_HOST=ON -DTIRAY_SDK_BUILD_HOST_GUI=ON
cmake --build build
# 目标：build/tiray_host（命令行）、build/tiray_host_gui（界面）
```

如需内部配置组访问，SDK 本体也要用内部版构建：

```bash
cmake -S . -B build -DTIRAY_SDK_INTERNAL_BUILD=ON -DTIRAY_SDK_BUILD_HOST=ON -DTIRAY_SDK_BUILD_HOST_GUI=ON
```

两个开关默认关闭，不影响客户交付包（`scripts/package_sdk.sh` 不开启）。

## GUI 使用说明

```bash
sudo ./build/tiray_host_gui   # 串口访问通常需要 root 或 dialout 组
```

- **顶部连接栏**：串口路径（默认 `/dev/ttyWCH0`）、波特率、响应超时、内部版开关、连接/断开；
- **设备页**：PING、状态读取（可自动轮询）、静态采集触发、Dynamic 启停查询、重启设备（二次确认）；
- **配置页**：配置组读取/下发（表格可增删行，值支持 `0x` 十六进制）、Static/Dynamic 常用参数快速读写；下发 Dynamic 时自动保留设备当前的 Step 高/低电平；
- **校准页**：暗场/亮场模板全流程（begin/capture/build/cancel）、`cal_status` 自动轮询、模板上传 config/start/query；
- **图像/PCIe 页**：启动/停止 SDK PCIe 帧监听（设备路径留空用默认，BAR0 自动发现），实时显示 16-bit 帧（自动/手动窗宽窗位、显示宽度降采样）、保存显示 PNG 与原始 `.raw`；
- **图像交互**：滚轮缩放、左键平移、右键框选计算区域均值/最值/标准差，状态栏显示鼠标像素值；
- **底部日志**：所有命令结果（`[PASS]/[FAIL]`）带时间戳。

命令行版用法见 `tiray_host --help`；一次性模式下任一命令失败即返回非零，便于脚本化回归。

## 约定

- 静态采集为一次 `static` 触发命令，图像经 PCIe 接收；
- `cal_status` 需轮询到成功/失败（GUI 可自动轮询）；
- 高帧率持续采集时每帧会在 SDK 线程复制一次，维护验证建议按需启停；
- 客户侧使用方式以 `docs/customer/` 为准，本 README 不随客户安装包发布。
