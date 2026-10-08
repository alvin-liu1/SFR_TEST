# Project State

快照日期：2026-10-08。依据当前源码静态核对；未执行构建或测量验收。长期事实与调用链分别见 PROJECT.md、ARCHITECTURE.md。

## Current Stage

已有模块化 C++ 测量工具；正式发布/生产使用状态 UNKNOWN。本次任务建立 AI Project Baseline，不实施架构或算法改动。

## Completed Features

代码已具备 INI 读取、批量 BMP/JPG、AOI 加载/保存、自动四 ROI 定位、SFR 数值流程、嵌套 async、结果 CSV 和绘图。此处“已具备”仅表示实现存在，不等同验收 PASS。

## Partially Completed Features

ZJ 有拉图/状态/推送调用链，但 Config/pushSFRresult.bat 缺失；拉图脚本使用固定输出路径，与 runner 的 DrawingPath/Camera 不保证一致。设备集成未验证。

## Current Work

五份基线已完成事实核对和一致性检查；首次本地提交 02ef734 已完成。当前等待 GitHub 连接恢复后推送 main；下一功能 TASK UNKNOWN。

## Known Bugs

以下为静态证据确认的问题，运行复现未执行：
- INI 使用 // 注释，解析器只截断 #；Name=P223_MZ 当前无行内注释，但字符串路径或未来 Name 附加 // 会被保留，无法按注释语义解析。
- LineCentroidFind 在固定行/列坐标未验证前访问像素；极端 ROI 定位可能越界。
- judgeResult 未检查非有限值；NaN 可绕过零值和大小比较。
- 缺 AOI/无图片的流程局部 return，main 可能返回 0；日志/状态/脚本失败不能可靠反映为进程失败。

## Known Technical Debt

硬编码依赖路径；MSBuild 未显式配置 C++17；嵌套 async 无并发上限；数值输入校验不完整；样条使用密集矩阵求逆；无独立自动测试入口和可确认的参考测量集。OpenCV 数值优化、资源管理变更的历史效果 UNKNOWN。

## Important Design Decisions

保持当前五模块边界与数值逻辑；交错 H/V 阈值；零值对平均的容错；任一区域异常优先 RETEST；图片串行、区域及四子 ROI 并行。记录的是现存实现，其产品合理性尚未确认。

## Current Limitations

依赖 Windows 与桌面交互；图像单层扫描、后缀区分大小写、未排序。H1 与原图共享写入：重叠 AOI 存在并发风险，实际配置是否重叠 UNKNOWN。
DeleteOriginPic 下失败移动不成功会删除输入；ZJ 脚本删除设备照片，推送后删除本地 test.txt。这些是现存行为，保留策略 NEED USER CONFIRMATION。
同一 SFR.csv 追加不同 AreaNum 的结果可能使列数与原表头不符。

## Pending Tasks

已在根目录初始化 main，配置 origin=https://github.com/alvin-liu1/SFR_TEST.git 和 Human 提供的提交身份。首次提交已完成；git push -u origin main 因 GitHub 443 连接超时失败，远端状态尚未核实。
NEED USER CONFIRMATION：正式构建入口；精度参考数据、容差及 Gamma 定义；ZJ 路径/推送脚本和数据保留策略。当前构建、运行、设备验收结果 UNKNOWN。

## Recent Important Changes

当前代码有模块分离、cv::dft、vector/Mat 资源管理、async 和可配置 SfrAngle/LineLength；基线建立前无本地 Git 历史，无法确认修改时间、作者、回归结果，不视为已验证优化。
首次提交 02ef734 纳入现有源码、配置和五份基线文档；未修改源码逻辑，未纳入图片、DLL、缓存或构建产物。文档 git diff --cached --check 通过；原配置存在空白格式告警，保持原样。

## Next Development Priorities

1. 建立可重复构建和受控图片数值基准。
2. 单独 TASK 处理配置解析、边界/非有限值、失败退出与共享 ROI 风险。
3. 确认设备文件契约和保留策略，再验证 ZJ 集成。
