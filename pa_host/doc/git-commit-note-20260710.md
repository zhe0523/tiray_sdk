# 2026-07-10 提交说明

## 提交目的

整理当前 `PA Host` 阶段性开发成果，补齐图像查看交互、ROI 分析、自动窗宽窗位和 Kylin 开发文档，并提交到 git。

## 本次代码变更

### 1. 图像查看交互

```text
滚轮、Ctrl+滚轮：缩放
左键拖动：平移图像
Ctrl+左键拖框：ROI 分析
Shift+左键拖框：按 ROI 重算窗位和窗宽
再次普通左键点击：清除当前 ROI 框
```

### 2. 图像信息与 ROI

```text
右侧实时显示鼠标所在像素坐标和值
右侧显示全图或 ROI 的统计摘要
支持 ROI 均值、最小值、最大值、标准差、行噪声等统计
```

### 3. 分析测试弹窗

当前 Ctrl+ROI 会弹出“分析测试”窗口，窗口结构改成：

```text
左侧：整张图预览并标出 ROI
右侧：ESF / LSF / MTF 三张曲线
```

说明：当前曲线算法已具备基本结构，但是否与旧 Windows 软件完全一致，后续仍需用同一 ROI 做数值对比校准。

### 4. 自动窗宽窗位

自动窗宽窗位已从简单 `min/max` 拉伸改为接近旧上位机行为的直方图分位算法。

当前采用：

```text
低端丢弃 0.6%
高端丢弃 0.6%
即 0.6% ~ 99.4% 分位
```

对测试图：

```text
/home/zhe/app/windows/tidetector/CollectImage/20260707-1035/20260707-104636-873-00000001.tiraw
```

当前程序计算结果：

```text
窗位 = 3937
窗宽 = 1948
```

旧软件记录：

```text
窗位 = 3939
窗宽 = 1948
```

当前结果已基本对齐。

### 5. 界面整理

```text
顶部模式条和菜单样式整理
RS422 和 PA/FPGA 控制入口移到菜单栏
右侧只保留图像相关操作
主窗口最小尺寸调整为 1180 x 820
```

## 文档补充

本次同时更新：

```text
README.md
doc/kylin-handover.md
```

内容包括：

```text
当前功能边界
.tiraw 解析方式
自动窗宽窗位算法
Qt Creator / shadow build 说明
Kylin 开发与测试方式
后续 PCIe 接入建议
```

## 验证结果

本次提交前已完成：

```text
cmake --build build -j
ctest --output-on-failure
QT_QPA_PLATFORM=offscreen ./build/pa_host 启动烟测
5 张 .tiraw 样例文件头检查
```

说明：

```text
当前工程还没有注册 ctest 用例，所以 ctest 输出为 No tests were found
```

## 后续建议

下一阶段优先级建议：

```text
1. 用旧软件同一 ROI 对齐 ESF / LSF / MTF 公式
2. 明确光口转 PCIe 的驱动/SDK 接口
3. 先做命令行采集测试程序，再接入 GUI
```
