# Windows 上位机参考记录

参考目录：

```text
截图：/home/zhe/sdk/app/windows/screenshot
安装目录：/home/zhe/sdk/app/windows/tidetector
```

## 程序形态

```text
TiRayDetector.exe：WPF/.NET Framework 4.8
TiRayLib.dll：Windows 原生 x86_64 DLL
依赖：HandyControl、ScottPlot、Serilog、OpenCV、fo-dicom 等
```

旧程序大量包含：

```text
WiFi 扫描和配置
TCP 服务
多型号探测器兼容
历史图像
DICOM/报告
```

这些不是新项目第一阶段目标。

## UI 可参考点

```text
顶部菜单：文件、校准、工具、开发者、帮助
顶部模式：Idle、Continuous、手动上图、停止上图
左侧：图像列表
中间：棋盘背景图像画布
右侧：旋转、翻转、缩放、保存、叠加、锐化、窗宽窗位
底部：型号、分辨率、序列号、连接状态、工作模式、图像编号、接收进度、fps、温湿度等
```

## .tiraw 结论

样例头：

```text
5469526179526177 0100 0200 e80b f409
```

解释：

```text
"TiRayRaw"
version = 1
bytes_per_pixel = 2
height = 3048
width = 2548
```

另一个样例：

```text
height = 3052
width = 2500
```

文件大小与 `16 + width * height * 2` 匹配。

