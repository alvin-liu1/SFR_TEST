# Architecture

以当前六个编译单元为准；本文件描述现有实现，不提出重构。问题清单见 PROJECT_STATE.md。

## Architecture Overview

单个 Windows 控制台程序。main 加载配置并创建 SfrTestRunner；runner 同时承担路径、流程编排、绘图及自动 ROI 定位。RrConfig、AoiManager、ResultLogger 提供配置、AOI 持久化和结果输出；sfr.cpp 实现数值计算。

## Module Map / Module Responsibilities / Module Dependencies

| 模块 | 入口 → 核心函数 → 下游 → 输出 |
| --- | --- |
| main.cpp | main → ReadConfig、runner.run → RrConfig/SfrTestRunner → 进程返回码、控制台 |
| RrConfig.h/.cpp | ReadConfig → AnalyseLine/Trim → 文件流/settings_ → ReadString/ReadInt/ReadFloat |
| AoiManager.h/.cpp | loadAoiData/saveAoiData → CSV 解析/selectROI → OpenCV/文件流 → vector<Rect>、AOI CSV |
| SfrTestRunner.h/.cpp | 构造/run → setupPaths/loadConfig/loadSfrThreads、runSfrMode/runDrawingMode → 四个下游模块及 Win32/脚本 → 批量结果、图像、状态 |
| sfr.h/.cpp | SFRCalculation → de_Gamma/CentroidFind/SLR/ReduceRows/OverSampling/HammingWindows/DFT/BSpline → OpenCV → MTF 引用、0/1 返回码、调试 CSV |
| ResultLogger.h/.cpp | judgeResult/logResultToCsv/updateTestTxt → 阈值比较、追加/覆盖文件 → 标准库 → TestResult、SFR.csv、test.txt |

依赖方向：main → RrConfig + runner；runner → RrConfig + AoiManager + ResultLogger + sfr；AoiManager/sfr → OpenCV；ResultLogger 不依赖 runner。没有插件接口、服务层或独立算法库目标。

## Main Data Flow

INI → 项目名、参数、路径、交错 H/V 阈值 → 图片路径列表 → 灰度图 → AOI Rect → 四个 H1/H2/V1/V2 图像 → 四个 MTF → H/V 合并 → 判定 → CSV → 可选图片删除/移动 → ZJ 状态推送。

绘图分支：彩色图片 → clone → TV/SFR 线 → 显示；RS_EN=0 时保存 <原文件名>.jpg，RS_EN=1 时循环扫描和显示。当前循环没有删除输入图片，旧配置注释与实现不同。

## Important Classes / Important Interfaces

- rr::RrConfig：ReadConfig 返回 bool；ReadString/ReadInt/ReadFloat 提供默认值。数值读取使用 atoi/atof，不是严格类型校验。
- SfrTestRunner：公开构造函数接收配置引用与路径；run 返回 void。其他流程/ROI 方法为 private。
- AoiManager：loadAoiData 返回实际区域数；saveAoiData 返回 void；getRects 返回只读引用。
- ResultLogger：judgeResult 返回 TestResult；日志与状态写入接口返回 void。
- SFRCalculation(cv::Mat&, double gamma, double PixelSize, double TestFrequency, double&, std::string path, int debug)：输入预期 CV_8UC1，原地修改；0=失败、1=成功。runner 等待 future，但未使用其 int 成功码。

## Important Functions

- setupPaths：可切换 SFR_test_demo，检查自定义路径，回退 Pictures/Results，创建目录，派生 Fail/AOI/SFR.csv/test.txt。
- findImages：单层、扩展名精确匹配、无排序；错误输出后返回当前列表。
- findSfrRois：blur → GaussianBlur → Canny/自适应阈值 → contours/moments → 最大面积轮廓 → fitEllipse → 角度偏移 → LineCentroidFind → 椭圆角点修正 → AreaCutJudge → 裁剪/旋转。H/V 是程序命名，调整方向前必须用实测验证。
- LineCentroidFind：沿搜索轴计算非零点平均；只检查遍历轴，固定轴坐标没有显式边界检查。
- SFRCalculation：de-Gamma 使用 exponent=1/gamma；质心滤波会修改 ROI；回归模型 y=a+b*x；按斜率裁行后 clone；4x ESF binning，取中间 80% 并微分；峰值居中/Hamming；cv::dft 幅值/DC 归一化；频率点使用 (i/width)*(1000/PixelSize)；BSpline 插值。
- BSpline 名称保留；实现为自然边界三次样条，用密集矩阵 A.inv()*B，范围外使用首/末段多项式外推，不是钳制到端点。
- getMtfAverage：两个非零值平均，仅一个非零则取该值，均为零返回零。
- judgeResult：任一区域 H/V 等于零或 >=1 即 RETEST；否则任意低于阈值 FAIL；其余 PASS。没有 NaN/负值专项校验。

## Important Data Structures

settings_：map<string,map<string,string>>；AOI：vector<cv::Rect>。
m_sfrThreads/m_thresholds：[H0,V0,H1,V1,...]，长度预期 2*AreaNum。
mtf_H/mtf_V：长度 AreaNum、初始零；每个异步任务写唯一索引。
TestResult：PASS/FAIL/RETEST；CSV 状态分别 success/fail/retest。
算法使用 vector<double> 质心、ESF/LSF、采样计数和 vector<Point2f> 频率曲线；单位见 PROJECT.md。

## Thread Model

图片串行；每张图片每个可用 AOI 启动 std::async(launch::async)，每个 AOI 再启动四个 SFR 异步任务。没有线程池或并发上限。
父流程 fut.get 后判定和写日志；子流程四个 get 后合并。printMutex 只保护部分 runner 输出，不保护算法输出、图像内存或文件 IO。future 异常可传播至 main。不要把 const Mat& 等同于像素只读。

## Resource / Memory Ownership

main 中 config 生命周期覆盖 runner；runner 借用配置引用，拥有 AoiManager 和 unique_ptr<ResultLogger>。logger 复制阈值；vector/文件流/Mat 使用 RAII。
img(roiBox) 是共享像素视图；H1=roi(H1) 继续共享原图；H2 来自 AOI clone；V1/V2 经 transpose/flip 获得输出缓冲区。Mat 引用计数保持局部 clone 的缓冲区存活。
SFR 原地 de-Gamma/GaussianBlur；重叠 AOI 时共享 H1 写入可能影响其他任务。禁止在未分析 alias、AOI 重叠及 future 生命周期前调整 clone 或并发。

## Configuration Flow

main 的 cwd/Config/sfr_config.ini → ReadConfig → runner 构造读取 PROJECT.Name → ZJ 子串决定处理参数 → setupPaths → 项目参数 → <Name>_SfrThread → logger → run 读取 DRAWING_F.Enable。
缺键使用默认值；重复键后值覆盖前值；仅 # 注释被截断。即使绘图也先执行项目配置/日志初始化。

## Error Handling Flow

配置文件打开失败 main 返回 -1；构造或 future 异常到 main 捕获、暂停、返回 -1。
缺 AOI 文件在 runSfrMode 内捕获并 return；无图片重试三次后 return；读取失败图片 continue。这些业务路径可能让 main 最终返回 0。
ROI 识别失败/越界保持对应 MTF=0 → RETEST；数值函数失败返回 0 并通常保持 MTF=0。
部分日志写入仅打印错误，目录创建/状态写入/脚本返回值未检查；不能以进程退出码 0 证明测量或推送成功。

## Important Call Chains

- 测量：main → runner.run → runSfrMode → processAoiParallel → findSfrRois → SFRCalculation → getMtfAverage → judgeResult → logResultToCsv。
- 数值：SFRCalculation → de_Gamma → CentroidFind → SLR → ReduceRows → OverSampling → HammingWindows → DFT → BSpline。
- 选区：runSfrMode → imread 首张图 → saveAoiData → loadAoiData。
- 绘图：run → runDrawingMode → findImages → imread → line → imshow/imwrite。
- ZJ：runSfrMode → system(pullimage_SFR.bat) → 图片路径追加 /Camera → 测量 → updateTestTxt → system(pushSFRresult.bat) → remove(test.txt)。

## Cross-module Dependencies

AreaNum 同时约束 AOI 数、结果 vector、阈值长度及 CSV 列数；修改任一端须核对全部。
H/V 方向、图像旋转、零值容错和阈值顺序构成完整判定契约。
名称 ZJ 控制算法参数、设备脚本、路径及状态输出；路径/INI 解析变化会影响所有流程。
CSV 既有表头只在文件不存在时创建；改变区域数后继续追加可能造成不同列数混用。

## High-risk Modification Areas

数值频率标定/超采样/样条外推、Gamma 定义；ROI 几何与边界检查；共享 Mat 与嵌套异步；缺失/异常值的判定；文件删除/MoveFile 失败回退；ZJ 设备脚本与状态文件删除；构建工具链/字符集/OpenCV ABI。修改前需最小调用链与回归证据；具体当前风险见 PROJECT_STATE.md。
