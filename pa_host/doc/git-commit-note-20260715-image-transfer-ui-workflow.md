# 2026-07-15 上图工作流与界面状态修正提交说明

## 提交主题

```text
完善上图工作流并修正界面状态逻辑
```

## 阶段目标

本阶段把主界面顶部的手动/持续上图按钮从测试占位入口调整为正式业务工作流，并集中
修正按钮等待、禁用外观、状态栏文案、图像列表、ROI、回放 FPS 和导出操作中的界面状态
不一致问题。

当前仍不接入真实 PCIe 图像源，也不提前实现尚未冻结的 V1 事务协议。RS422 命令继续
使用内存传输测试验证，真实 ARM/FPGA 版本信息在设备未提供时显示为未获取。

## 主要改动

### 1. 拆分正式上图工作流

新增 `ImageTransferWorkflowController`，将正式客户入口与研发原始命令菜单分离：

```text
Idle + 手动上图        -> SEND_SINGLE
Continuous + 开始上图  -> START_CONTINUOUS
任一上图过程 + 停止上图 -> STOP_TRANSFER
```

Idle 和 Continuous 只选择本地模式，不直接发送命令。客户必须点击开始按钮确认操作，持续
上图由 FPGA 自行连续发送，不由上位机逐帧触发。

工作流统一管理：

```text
Disconnected
Ready
StartingSingle
StartingContinuous
ContinuousRunning
Stopping
Error
```

顶部模式、开始和停止按钮只根据工作流状态启用。不可执行的按钮显示为灰色；保持蓝色或
绿色的按钮必须能够响应点击。

### 2. 停止命令立即恢复界面

`STOP_TRANSFER` 调整为无需响应的即时控制命令：

```text
点击停止
    -> 取消上位机当前命令等待
    -> 停止原命令超时计时器
    -> 写出 STOP_TRANSFER
    -> 写出成功后立即恢复 Ready
```

停止不再排队等待 `SEND_SINGLE` 或 `START_CONTINUOUS` 完成，也不等待 ARM/FPGA 返回
`OK STOP_TRANSFER`。只有串口写出失败时进入可重试错误状态。

V0 没有事务号，停止后旧响应迟到可能与紧接着发送的新命令混淆。当前为保证客户操作不
阻塞而接受该基础联调边界；正式产品化前必须在 V1 中通过事务号和迟到响应规则解决。

### 3. 调整上图协议命令

原来的模糊命令 `SEND_IMAGE` 拆分为：

```text
SEND_SINGLE
START_CONTINUOUS
STOP_TRANSFER
```

不需要的 `QUIT` / “退出 ARM”已从上位机菜单、协议枚举、命令列表和当前协议草案中删除，
避免远程终止 ARM 控制程序的误操作入口。

### 4. 增加软件与设备版本信息

“帮助 -> 关于 PA Host”现在显示：

```text
软件版本
编译时间
ARM 程序版本
FPGA 版本
```

CMake 在构建时注入软件版本和构建时间。`STATUS` 响应新增向后兼容的可选字段：

```text
model
serial
arm_version
fpga_version
```

型号和序列号用于状态栏，ARM/FPGA 版本用于关于窗口。旧 ARM 不返回这些字段时仍可正常
解析原有状态，只显示占位值；断开设备后清除上一台设备的信息。

### 5. 统一状态栏和图像信息文案

状态栏字段固定为：

```text
型号 / 序列号 / RS422 状态 / 工作模式 / 图像尺寸 / 缩放 / 显示 FPS
```

修正了“当前图像: --”在打开文件后突然变成无前缀分辨率的问题，统一为：

```text
图像尺寸: --
图像尺寸: 宽 x 高
```

静态图像或停止回放时显示 `显示 FPS: --`；回放期间显示实际 FPS 和目标 FPS。ROI 信息在
没有图像时显示占位，打开图像后默认显示全图统计。

### 6. 修正图像控件可用状态

没有图像时禁用：

```text
保存显示图像
图像最大化
旋转、翻转、缩放、适应和重置
自动及手动窗宽窗位控件
```

自动窗宽窗位开启时，手动滑块和输入框禁用；Shift+ROI 切换到手动窗宽窗位后立即恢复
手动控件。图像列表没有选中项时，“移除选中图像”和 Delete 动作禁用。

空的“校准”菜单已移除。文件菜单中的“停止图像回放”只在采集会话活动期间可用，保存和
最大化入口只在存在图像时可用。

### 7. 修正图像列表与导出状态污染

右键导出非当前列表项时，改为使用独立临时 `TiRawImage` 加载和编码，不再悄悄替换
`ImageSession` 中的当前图像，避免画面、ROI 和后续窗宽窗位操作引用不同文件。

回放启动成功后才替换图像列表；启动失败时保留原列表和当前图像。切换到已经删除或损坏
的文件失败时，尽量恢复到仍在显示的列表项。

“保存显示图像”会应用当前旋转和翻转方向；缩放和平移仍只属于查看状态，不写入文件。

### 8. 修正 ROI 连续帧行为

主窗口保存当前有效 ROI。连续回放同尺寸图像时，ROI 框保留，下方统计继续按该 ROI 更新，
不再出现框仍可见但文字自动变回全图的问题。

普通左键清框、切换图像或图像尺寸变化后恢复全图统计。拖框过小或在图像外结束时自动
删除无效框并恢复全图信息。

### 9. 扩展自动测试

新增和更新测试覆盖：

```text
三条上图命令文本和命令数量
手动/持续模式选择不发送命令
开始命令等待期间立即停止
持续运行后立即停止
STOP_TRANSFER 不等待响应
停止取消原命令超时计时器
研发菜单命令与正式工作流状态同步
错误状态下停止恢复
STATUS 可选型号、序列号、ARM 和 FPGA 版本字段
旧 STATUS 不带可选字段仍可解析
```

## 文档更新

```text
README.md
    补充正式上图入口、即时停止、关于窗口和界面状态规则

doc/architecture.md
    增加 ImageTransferWorkflowController 及其与设备控制层的边界

doc/communication-data-link-draft.md
    草案升级到 0.4，记录三条上图命令、可选设备信息字段和 V0 风险

doc/kylin-handover.md
    增加顶部业务按钮和停止无需回包的人工验证步骤
```

## 验证记录

执行命令：

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j
cd build
ctest --output-on-failure
cd ..
git diff --check
xvfb-run -a timeout 2 ./build/pa_host
```

验证结果：

```text
本机 Qt 5 全部目标构建成功
CTest：1/1 测试通过
git diff --check：通过
Xvfb 无界面启动正常，程序保持运行后由 timeout 终止
```

## 当前边界

```text
真实 ARM/FPGA 尚未返回 model、serial、arm_version、fpga_version 字段
真实 PCIe 图像接收仍未接入
V0 没有事务号，STOP_TRANSFER 后的迟到响应风险留待 V1 解决
PA/FPGA 原始命令菜单和运行日志仍属于研发维护入口
窗宽窗位和 MTF 当前算法仍是可替换的内置实现
```
