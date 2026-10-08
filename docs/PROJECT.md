# Project

本文件记录稳定事实；实现索引见 ARCHITECTURE.md，当前问题见 PROJECT_STATE.md，修改约束见 CODING_RULES.md，协作流程见 AI_WORKFLOW.md。

## Project Overview

SFR-test 是 Windows C++ 控制台图像清晰度测试工具，利用斜边 ROI 计算指定频率的 MTF，支持相机模组和整机测试及图卡标定线绘制。源码位于 SFR-test/。

## Project Goals

支持配置驱动的批量图像测量、可重复的 AOI 定位和结果记录。商业目标、标准认证及精度验收要求：NEED USER CONFIRMATION。源码描述采用 ISO 12233 斜边法；不代表已完成标准符合性验证。

## Current Development Stage

已有可执行流程与模块拆分；正式发布成熟度 UNKNOWN。详细进度只记录在 PROJECT_STATE.md。

## Platform

Windows；源码依赖 windows.h、direct.h、conio.h 和 Windows 批处理。工程提供 Win32/x64 配置，各配置可构建性需验证。

## Languages

C++；INI/CSV 配置；Windows BAT 外部设备脚本。

## Frameworks / Libraries

OpenCV（图像 IO、处理、窗口、DFT、矩阵运算）；C++ 标准库 filesystem、future、mutex、容器与文件流。CMake 要求 C++17；Visual Studio 工程未显式设置 LanguageStandard。

## Build Environment

CMake >= 3.15；Windows C++17 编译器及匹配 ABI 的 OpenCV。
Visual Studio 工程声明 v141 工具集和 Windows SDK 10.0.26100.0，条件导入 D:/Code/opencv4.5.2.props。
CMakeLists.txt 硬编码 OpenCV_DIR=D:/SoftWare/opencvInstall/opencv/build/x64/mingw/lib；这是现有机器配置，不是可移植依赖声明。

## Build Method

两套入口：根目录 SFR-test.sln/MSBuild；SFR-test/CMakeLists.txt/CMake。共同编译六个 .cpp 文件并生成 SFR-test 可执行文件。

## Runtime Environment

工作目录必须包含 Config/sfr_config.ini；按配置提供图像和 AOI CSV，OpenCV 运行库须可被加载。交互选区和绘图需要桌面窗口环境。ZJ 模式需要 adb、设备及外部脚本。

## Core Features

- 批量处理小写 .bmp/.jpg；目录扫描不递归。
- AOI CSV 加载、交互选区保存。
- 椭圆拟合定位四个子 ROI；区域及子 ROI 异步计算。
- de-Gamma、质心、回归、4 倍超采样、LSF、Hamming 窗、OpenCV DFT、自然三次样条插值。
- H/V MTF 合并、PASS/FAIL/RETEST 判定和 SFR.csv。
- TV line/SFR 标定线绘制；ZJ test.txt 与脚本集成入口。

## Directory Structure

| 路径 | 用途 |
| --- | --- |
| SFR-test.sln | Visual Studio 解决方案 |
| SFR-test/*.cpp、*.h | 六个实现单元及五个公共头文件 |
| SFR-test/CMakeLists.txt、*.vcxproj | 构建定义 |
| SFR-test/Config/ | INI、项目 AOI CSV、设备拉图脚本 |
| SFR-test/Read me.txt | 旧版操作说明，行为以代码为准 |
| photo/ | 本地图像资料 |
| x64/、SFR-test/x64/、SFR-test/build/、.vs/ | 本地输出/缓存；不作为源码入口 |
| docs/ | AI Project Baseline |

## External Dependencies

OpenCV、Windows 工具链；ZJ 额外依赖 adb 与 Config/pullimage_SFR.bat、Config/pushSFRresult.bat。目录中存在 LCE_Demosaic.dll 和输出目录中的头文件，但当前六个编译单元没有调用它，不据此声明为必需依赖。

## Important Configuration

入口 INI 的 [PROJECT] Name 选择同名项目节；名称含 ZJ 决定整机分支和边缘处理参数。
[PATH] DrawingPath/ResultsPath；[DRAWING_F] Enable、Suffix、RS_EN、绘图参数。
项目节 AreaNum、PixelSize（um/pixel）、TestFreq（lp/mm）、Gamma、Suffix（1=bmp，2=jpg）、AoiSave、Debug、DeleteOriginPic、SfrAngle。
阈值节 <Name>_SfrThread，MTFH_i/MTFV_i；名称 Thread 实际表示阈值，不表示线程。
AOI 文件 <Name>_SFR_AOI.csv：序号,REC_X,REC_Y,Width,Height。
解析器仅处理 # 注释；字符串值不要附加 // 注释。工作目录下存在 SFR_test_demo 时，runner 将后续路径基准切换到该目录，main 的 INI 加载仍发生在切换前。
DeleteOriginPic 会删除通过图片，失败/重测图片移动失败时也会删除原图。ZJ 拉图脚本包含删除设备照片的命令，运行需使用已批准的数据和设备。

## How to Build

在根目录、匹配依赖的开发环境中：
```powershell
cmake -S SFR-test -B SFR-test/build-baseline -G "MinGW Makefiles"
cmake --build SFR-test/build-baseline
# 或在 Visual Studio Developer 命令提示符中：

msbuild SFR-test.sln /p:Configuration=Debug /p:Platform=x64
```
CMake 硬编码路径需按实际环境核对；MSBuild 的 C++17 设置、字符集和依赖兼容性需先验证。命令是操作说明，不是已通过的构建证据。

## How to Run

在包含 Config 的 SFR-test 工作目录启动生成的可执行文件，例如 ./build-baseline/SFR-test.exe。命令行参数当前未被使用。
先用图片副本，设置正确项目名、路径和区域数；首次选区设置 AoiSave=1，保存后改为 0。正常测量关闭 DRAWING_F.Enable；绘图时开启。实际窗口及暂停等待按代码执行。

## How to Test

没有发现独立自动测试工程或 CTest 注册；程序名中的 test 指测量工具。
人工验收应使用固定图片副本与正确 AOI：验证已知 MTF、阈值边界、RETEST、无图片、缺 AOI、越界区域、绘图输出；算法修改还需合成斜边、参考曲线和重复运行比较。正式数据集、参考实现和容差 UNKNOWN。每次记录真实命令、退出码、输出及未执行项；文档检查不能代替构建或数值测试。
