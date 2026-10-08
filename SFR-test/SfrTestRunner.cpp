/**
 * @file    SfrTestRunner.cpp
 * @brief   SFR 测试主控类实现
 * @details 实现相机清晰度自动化测试的完整流程:
 *          - SFR 计算模式: 批量加载图片 → AOI 裁剪 → 斜边 ROI 识别 → SFR 计算 → 判定结果
 *          - 绘图模式: 批量加载图片 → 绘制 TV line / SFR 标定线 → 显示或保存
 *
 *          关键优化:
 *          - 使用 std::async 并行化多 AOI 的 SFR 计算
 *          - 硬编码魔数改为配置参数 (SfrAngle, DrawingLineLength)
 *          - 委托 AoiManager / ResultLogger 分担职责
 */

#include "SfrTestRunner.h"
#include "sfr.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <windows.h>   // MoveFile, system
#include <direct.h>    // _mkdir, _getcwd
#include <conio.h>     // _kbhit, _getch (绘图模式键盘检测)
#include <future>      // std::async, std::future
#include <algorithm>   // std::replace

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// 构造函数
// ============================================================================

SfrTestRunner::SfrTestRunner(rr::RrConfig& config, const std::string& originPath)
    : m_config(config)
    , m_originPath(originPath)
    , m_isZjProject(false)
{
    // 读取项目名称
    m_projectName = m_config.ReadString("PROJECT", "Name", "");
    if (m_projectName.empty()) {
        throw std::runtime_error("配置错误: [PROJECT] 中的 Name 项不能为空。");
    }

    // 判断是否为整机 (ZJ) 项目，影响图像处理算法选择
    if (m_projectName.find("ZJ") != std::string::npos) {
        m_isZjProject = true;
        m_imageProcessSelect = 2;  // 数字图像处理参数
    } else {
        m_imageProcessSelect = 1;  // 模拟图像处理参数
    }

    // 初始化路径
    setupPaths();

    // 加载项目测试参数
    loadConfig();

    // 加载 SFR 阈值
    loadSfrThreads();

    // 初始化结果日志记录器
    m_logger = std::make_unique<ResultLogger>(
        m_sfrLogPath, m_testFrequency, m_testAreaNum, m_sfrThreads);
}

// ============================================================================
// 初始化
// ============================================================================

void SfrTestRunner::setupPaths()
{
    // 检查是否存在 "SFR_test_demo" 子目录，有则优先使用
    std::string cameraPath = m_originPath + "/SFR_test_demo";
    if (_access(cameraPath.c_str(), 0) != -1) {
        m_originPath = cameraPath;
    }

    // 读取路径配置，未提供则使用默认值
    std::string defaultPicturesPath = m_originPath + "/Pictures";
    std::string defaultResultsPath  = m_originPath + "/Results";
    m_configPath = m_originPath + "/Config";

    m_picturesPath = m_config.ReadString("PATH", "DrawingPath", defaultPicturesPath.c_str());
    m_resultsPath  = m_config.ReadString("PATH", "ResultsPath",  defaultResultsPath.c_str());

    // 检查自定义路径是否有效
    if (_access(m_picturesPath.c_str(), 0) == -1) {
        std::cout << "自定义读图路径无效，使用默认路径: " << defaultPicturesPath << std::endl;
        m_picturesPath = defaultPicturesPath;
    }
    if (_access(m_resultsPath.c_str(), 0) == -1) {
        std::cout << "自定义结果路径无效，使用默认路径: " << defaultResultsPath << std::endl;
        m_resultsPath = defaultResultsPath;
    }

    // 统一路径分隔符为正斜杠
    std::replace(m_picturesPath.begin(), m_picturesPath.end(), '\\', '/');
    std::replace(m_resultsPath.begin(),  m_resultsPath.end(),  '\\', '/');
    std::replace(m_configPath.begin(),   m_configPath.end(),   '\\', '/');

    m_picturesFailPath = m_picturesPath + "/Fail";

    // 确保所需目录存在
    _mkdir(m_picturesPath.c_str());
    _mkdir(m_resultsPath.c_str());
    _mkdir(m_configPath.c_str());
    _mkdir(m_picturesFailPath.c_str());

    // 构建各配置/输出文件路径
    m_aoiConfigPath = m_configPath + "/" + m_projectName + "_SFR_AOI.csv";
    m_sfrLogPath    = m_resultsPath + "/SFR.csv";
    m_testTxtPath   = m_configPath + "/test.txt";
}

void SfrTestRunner::loadConfig()
{
    m_testAreaNum    = m_config.ReadInt(m_projectName.c_str(),    "AreaNum",           1);
    m_pixelSize      = m_config.ReadFloat(m_projectName.c_str(),  "PixelSize",         1.0);
    m_testFrequency  = m_config.ReadFloat(m_projectName.c_str(),  "TestFreq",          110.0);
    m_gamma          = m_config.ReadFloat(m_projectName.c_str(),  "Gamma",             1.0);
    m_aoiSave        = m_config.ReadInt(m_projectName.c_str(),    "AoiSave",           0) != 0;
    m_debug          = m_config.ReadInt(m_projectName.c_str(),    "Debug",             0) != 0;
    m_deleteOriginPic = m_config.ReadInt(m_projectName.c_str(),   "DeleteOriginPic",   0) != 0;

    // 可配置参数: 从 INI 读取，未配置则使用默认值
    m_sfrAngle         = m_config.ReadFloat(m_projectName.c_str(), "SfrAngle",          DEFAULT_SFR_ANGLE);
    m_drawingLineLength = m_config.ReadInt("DRAWING_F",           "LineLength",         300);

    // 图片后缀
    int suffixId = m_config.ReadInt(m_projectName.c_str(), "Suffix", 1);
    m_imageSuffix = (suffixId == 2) ? "jpg" : "bmp";
}

void SfrTestRunner::loadSfrThreads()
{
    std::string threadSection = m_projectName + "_SfrThread";
    m_sfrThreads.resize(m_testAreaNum * 2);  // [H0, V0, H1, V1, ...]

    for (int i = 0; i < m_testAreaNum; ++i) {
        std::string keyH = "MTFH_" + std::to_string(i);
        std::string keyV = "MTFV_" + std::to_string(i);
        m_sfrThreads[2 * i]     = m_config.ReadFloat(threadSection.c_str(), keyH.c_str(), 0.0);
        m_sfrThreads[2 * i + 1] = m_config.ReadFloat(threadSection.c_str(), keyV.c_str(), 0.0);
    }
}

// ============================================================================
// 运行入口
// ============================================================================

void SfrTestRunner::run()
{
    int drawingMode = m_config.ReadInt("DRAWING_F", "Enable", 0);
    if (drawingMode) {
        runDrawingMode();
    } else {
        runSfrMode();
    }
}

// ============================================================================
// 绘图模式
// ============================================================================

void SfrTestRunner::runDrawingMode()
{
    std::cout << "执行绘图功能:" << std::endl;

    // 读取绘图配置
    int RS_EN            = m_config.ReadInt("DRAWING_F", "RS_EN",        0);
    int DrawingSuffixId  = m_config.ReadInt("DRAWING_F", "Suffix",       1);
    int LineTV_En        = m_config.ReadInt("DRAWING_F", "LineTV_En",    0);
    int LineTVRadius     = m_config.ReadInt("DRAWING_F", "LineTVRadius", 100);
    int LineSFR_En       = m_config.ReadInt("DRAWING_F", "LineSFR_En",   0);
    int LineSFRRadius    = m_config.ReadInt("DRAWING_F", "LineSFRRadius",100);
    double LineCrossTVAngle = m_config.ReadFloat("DRAWING_F", "LineCrossTVAngle", 0);

    // 解析后缀
    const char* suffix = (DrawingSuffixId == 2) ? "jpg" : "bmp";

    bool running = true;
    while (running) {
        auto files = findImages(m_picturesPath, suffix);
        int fileCount = static_cast<int>(files.size());

        if (fileCount == 0) {
            if (RS_EN) {
                std::cout << "请将图片放入读图文件夹 (或按 ESC 退出循环)..." << std::endl;
                cv::waitKey(100);
                if (_kbhit() && _getch() == 0x1b) {  // ESC 键退出
                    running = false;
                }
            } else {
                std::cout << "请将图片放入读图文件夹。" << std::endl;
                running = false;
            }
            continue;
        }

        // 逐张处理
        for (int f = 0; f < fileCount; ++f) {
            std::string imgPath  = files[f].string();
            std::string fileName = files[f].filename().string();
            cv::Mat img = cv::imread(imgPath, cv::IMREAD_COLOR);

            if (img.empty()) {
                std::cerr << "读取图片失败: " << fileName << std::endl;
                continue;
            }

            cv::Mat dst = img.clone();
            int width  = img.rows;
            int height = img.cols;

            // 计算垂向偏移 (原硬编码 250 → 基于图像高度动态计算)
            int y_shift = static_cast<int>(height * 0.15);

            // --- 绘制 TV Line 标定十字线 ---
            if (LineTV_En) {
                std::vector<cv::Point> Points;  // TV Line 十字图卡标记点
                int lineLen = m_drawingLineLength;
                double angle = 45;

                // X 形 delta
                int delta   = static_cast<int>(LineTVRadius * std::sin(angle / 180.0 * M_PI));
                int delta_x = static_cast<int>(lineLen / 2 * std::sin(angle / 180.0 * M_PI));
                int delta_y = static_cast<int>(lineLen / 2 * std::cos(angle / 180.0 * M_PI));

                // 十字形 delta
                int crossDelta_x = static_cast<int>(LineTVRadius * std::sin(LineCrossTVAngle / 180.0 * M_PI));
                int crossDelta_y = static_cast<int>(LineTVRadius * std::cos(LineCrossTVAngle / 180.0 * M_PI));
                int crossLineX   = static_cast<int>(lineLen / 2 * std::sin(LineCrossTVAngle / 180.0 * M_PI));
                int crossLineY   = static_cast<int>(lineLen / 2 * std::cos(LineCrossTVAngle / 180.0 * M_PI));

                // 中心 + 十字4端点 + X形4端点
                Points.push_back(cv::Point(height / 2, width / 2 + y_shift));
                Points.push_back(cv::Point(height / 2 - crossDelta_y, width / 2 - crossDelta_x));
                Points.push_back(cv::Point(height / 2 - crossDelta_x, width / 2 + crossDelta_y));
                Points.push_back(cv::Point(height / 2 + crossDelta_x, width / 2 - crossDelta_y));
                Points.push_back(cv::Point(height / 2 + crossDelta_y, width / 2 + crossDelta_x));
                Points.push_back(cv::Point(height / 2 - delta, width / 2 - delta));
                Points.push_back(cv::Point(height / 2 - delta, width / 2 + delta));
                Points.push_back(cv::Point(height / 2 + delta, width / 2 - delta));
                Points.push_back(cv::Point(height / 2 + delta, width / 2 + delta));

                for (size_t i = 0; i < Points.size(); ++i) {
                    if (i < 1) {
                        // 中心十字线 (水平 + 垂直)
                        cv::Point h1(Points[i].x + lineLen / 2, Points[i].y);
                        cv::Point h2(Points[i].x - lineLen / 2, Points[i].y);
                        cv::Point v1(Points[i].x, Points[i].y - lineLen / 2);
                        cv::Point v2(Points[i].x, Points[i].y + lineLen / 2);
                        cv::line(dst, h1, h2, cv::Scalar(0, 0, 255), 3, cv::LINE_8);
                        cv::line(dst, v1, v2, cv::Scalar(0, 0, 255), 3, cv::LINE_8);
                    } else if (i < 5 && i > 0) {
                        // 旋转十字线
                        cv::Point p1(Points[i].x + crossLineX, Points[i].y - crossLineY);
                        cv::Point p2(Points[i].x - crossLineX, Points[i].y + crossLineY);
                        cv::Point p3(Points[i].x - crossLineY, Points[i].y - crossLineX);
                        cv::Point p4(Points[i].x + crossLineY, Points[i].y + crossLineX);
                        cv::line(dst, p1, p2, cv::Scalar(0, 0, 255), 3, cv::LINE_8);
                        cv::line(dst, p3, p4, cv::Scalar(0, 0, 255), 3, cv::LINE_8);
                    } else {
                        // X 形线
                        cv::Point p1(Points[i].x + delta_x, Points[i].y - delta_y);
                        cv::Point p2(Points[i].x - delta_x, Points[i].y + delta_y);
                        cv::Point p3(Points[i].x - delta_y, Points[i].y - delta_x);
                        cv::Point p4(Points[i].x + delta_y, Points[i].y + delta_x);
                        cv::line(dst, p1, p2, cv::Scalar(0, 0, 255), 3, cv::LINE_8);
                        cv::line(dst, p3, p4, cv::Scalar(0, 0, 255), 3, cv::LINE_8);
                    }
                }
            }

            // --- 绘制 SFR 标定线 ---
            if (LineSFR_En) {
                std::vector<cv::Point> Points;
                double SFR_angle = 10;  // SFR 图卡角度
                int lineLen = m_drawingLineLength;

                Points.push_back(cv::Point(height / 2, width / 2));
                Points.push_back(cv::Point(height / 2 - LineSFRRadius, width / 2));
                Points.push_back(cv::Point(height / 2, width / 2 - LineSFRRadius));
                Points.push_back(cv::Point(height / 2 + LineSFRRadius, width / 2));
                Points.push_back(cv::Point(height / 2, width / 2 + LineSFRRadius));

                int delta_x = static_cast<int>(lineLen / 2 * std::sin(SFR_angle / 180.0 * M_PI));
                int delta_y = static_cast<int>(lineLen / 2 * std::cos(SFR_angle / 180.0 * M_PI));

                for (size_t i = 0; i < Points.size(); ++i) {
                    cv::Point p1(Points[i].x + delta_x, Points[i].y - delta_y);
                    cv::Point p2(Points[i].x - delta_x, Points[i].y + delta_y);
                    cv::Point p3(Points[i].x - delta_y, Points[i].y - delta_x);
                    cv::Point p4(Points[i].x + delta_y, Points[i].y + delta_x);
                    cv::line(dst, p1, p2, cv::Scalar(255, 0, 0), 3, cv::LINE_8);
                    cv::line(dst, p3, p4, cv::Scalar(255, 0, 0), 3, cv::LINE_8);
                }
            }

            // 显示或保存
            if (RS_EN) {
                cv::namedWindow("Picture", cv::WINDOW_NORMAL);
                cv::resizeWindow("Picture", dst.cols, dst.rows);
                cv::imshow("Picture", dst);
                if (fileCount - f > 1) cv::waitKey(1000);
            } else {
                std::string resultPath = m_resultsPath + "/" + fileName + ".jpg";
                cv::imwrite(resultPath, dst);
                cv::namedWindow("Picture", cv::WINDOW_NORMAL);
                cv::resizeWindow("Picture", dst.cols, dst.rows);
                cv::imshow("Picture", dst);
                cv::waitKey(10000);
            }
        }

        if (!RS_EN) running = false;
    }

    cv::destroyWindow("Picture");
}

// ============================================================================
// SFR 计算模式
// ============================================================================

void SfrTestRunner::runSfrMode()
{
    std::cout << "执行 SFR 计算功能:" << std::endl;
    std::cout << "当前测试配置为: " << m_projectName << std::endl;

    // 整机模式: 通过 ADB 拉取测试图片
    if (m_isZjProject) {
        std::string batPath = m_configPath + "/pullimage_SFR.bat";
        std::replace(batPath.begin(), batPath.end(), '/', '\\');
        system(batPath.c_str());

        m_picturesPath += "/Camera";
        _mkdir(m_picturesPath.c_str());
        std::cout << "整机模式: 已执行 pullimage_SFR.bat, 图片路径更新为: "
                  << m_picturesPath << std::endl;
    }

    // 扫描图片文件
    auto files = findImages(m_picturesPath, m_imageSuffix);
    int fileCount = static_cast<int>(files.size());

    // 最多等待 3 次用户放入图片
    int retries = 0;
    while (fileCount == 0 && retries < 3) {
        std::cout << "未找到指定格式(" << m_imageSuffix
                  << ")的图片，请放入图片后按任意键继续..." << std::endl;
        system("pause");
        files = findImages(m_picturesPath, m_imageSuffix);
        fileCount = static_cast<int>(files.size());
        retries++;
    }

    if (fileCount == 0) {
        std::cerr << "未找到图片，程序退出。" << std::endl;
        return;
    }

    // 交互式 AOI 选取 (若配置启用)
    if (m_aoiSave) {
        cv::Mat img = cv::imread(files[0].string(), cv::IMREAD_GRAYSCALE);
        if (!img.empty()) {
            m_aoiManager.saveAoiData(img, m_testAreaNum, m_aoiConfigPath);
        } else {
            std::cerr << "无法读取首张图片 " << files[0].string()
                      << " 以进行 AOI 选取。" << std::endl;
        }
    }

    // 加载 AOI 数据
    try {
        m_aoiManager.loadAoiData(m_aoiConfigPath, m_testAreaNum);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        system("pause");
        return;
    }

    // 逐张处理图片
    bool anyTestFailed  = false;
    bool anyTestSuccess = false;

    for (int f = 0; f < fileCount; ++f) {
        const auto& imgPath = files[f];
        std::string filename  = imgPath.filename().string();
        std::string imgPathStr = imgPath.string();

        cv::Mat img = cv::imread(imgPathStr, cv::IMREAD_GRAYSCALE);
        if (img.empty()) {
            std::cerr << "读取图片失败: " << filename << std::endl;
            continue;
        }

        std::cout << "\n--- 正在处理: " << filename << " (" << (f + 1)
                  << "/" << fileCount << ") ---" << std::endl;

        // 结果容器
        std::vector<double> mtf_H(m_testAreaNum, 0.0);
        std::vector<double> mtf_V(m_testAreaNum, 0.0);

        // -------- 并行化: 每个 AOI 独立在线程中计算 --------
        std::mutex printMutex;
        std::vector<std::future<void>> futures;

        for (int i = 0; i < m_testAreaNum; ++i) {
            if (i >= m_aoiManager.getCount()) {
                std::cout << "区域" << i << " : 缺少 AOI 数据，跳过。" << std::endl;
                continue;
            }

            // 使用 std::async 启动异步任务
            futures.push_back(std::async(std::launch::async,
                &SfrTestRunner::processAoiParallel, this,
                std::cref(img), i, std::cref(filename), f,
                std::ref(mtf_H), std::ref(mtf_V), std::ref(printMutex)));
        }

        // 等待所有 AOI 计算完成
        for (auto& fut : futures) {
            fut.get();
        }

        // 判定结果并记录日志
        TestResult result = m_logger->judgeResult(mtf_H, mtf_V);
        m_logger->logResultToCsv(f + 1, filename, mtf_H, mtf_V, result);

        // 处理图片 (删除或移动)
        if (result == TestResult::PASS) {
            anyTestSuccess = true;
            std::cout << filename << " 判定通过" << std::endl;
            if (m_deleteOriginPic) {
                std::remove(imgPathStr.c_str());
            }
        } else {
            anyTestFailed = true;
            if (result == TestResult::FAIL) {
                std::cout << filename << " 判定失败" << std::endl;
            } else {
                std::cout << filename << " 需要重新测试" << std::endl;
            }
            if (m_deleteOriginPic) {
                std::string destPath = m_picturesFailPath + "/" + filename;
                std::replace(destPath.begin(), destPath.end(), '/', '\\');
                std::string srcPath = imgPathStr;
                std::replace(srcPath.begin(), srcPath.end(), '/', '\\');
                if (!MoveFile(srcPath.c_str(), destPath.c_str())) {
                    std::remove(srcPath.c_str());
                }
            }
        }
    }

    // 整机模式的后处理
    if (m_isZjProject) {
        m_logger->updateTestTxt(m_testTxtPath, anyTestFailed, anyTestSuccess);

        std::string batPath = m_configPath + "/pushSFRresult.bat";
        std::replace(batPath.begin(), batPath.end(), '/', '\\');
        system(batPath.c_str());

        cv::waitKey(1000);

        if (!anyTestFailed && anyTestSuccess) {
            std::cout << "全部镜头清晰度测试通过" << std::endl;
        } else {
            std::cout << "镜头清晰度测试不通过" << std::endl;
        }

        std::remove(m_testTxtPath.c_str());
    }
}

// ============================================================================
// 并行化: 单个 AOI 的 SFR 计算
// ============================================================================

void SfrTestRunner::processAoiParallel(
    const cv::Mat& img, int aoiIdx,
    const std::string& filename, int fileIdx,
    std::vector<double>& mtf_H, std::vector<double>& mtf_V,
    std::mutex& printMutex)
{
    cv::Rect roiBox = m_aoiManager.getRects()[aoiIdx];

    // 线程安全输出
    {
        std::lock_guard<std::mutex> lock(printMutex);
        std::cout << "[区域" << aoiIdx << "] AOI: X=" << roiBox.x
                  << ", Y=" << roiBox.y << ", W=" << roiBox.width
                  << ", H=" << roiBox.height << std::endl;
    }

    if (!AreaCutJudge(img, roiBox)) {
        std::lock_guard<std::mutex> lock(printMutex);
        std::cout << filename << "<区域" << aoiIdx
                  << " >AOI 超出图像范围。" << std::endl;
        return;
    }

    cv::Mat roi = img(roiBox);

    // 自动识别 4 个斜边子 ROI
    cv::Mat roi_H1, roi_H2, roi_V1, roi_V2;
    std::string debugPath = m_resultsPath + "/N" + std::to_string(fileIdx + 1)
                          + "_area" + std::to_string(aoiIdx);

    bool roisFound = findSfrRois(roi, roi_H1, roi_H2, roi_V1, roi_V2,
                                  debugPath, m_debug, m_imageProcessSelect);

    if (!roisFound) {
        std::lock_guard<std::mutex> lock(printMutex);
        std::cout << "N" << (fileIdx + 1) << "区域" << aoiIdx
                  << " : MTF_H/V 识别失败" << std::endl;
        return;
    }

    // 保存调试中间图像
    if (m_debug) {
        cv::imwrite(debugPath + " roi_H1.jpg", roi_H1);
        cv::imwrite(debugPath + " roi_H2.jpg", roi_H2);
        cv::imwrite(debugPath + " roi_V1.jpg", roi_V1);
        cv::imwrite(debugPath + " roi_V2.jpg", roi_V2);
    }

    // 并行计算 4 个子 ROI 的 SFR (进一步并行化)
    double mtf_h1 = 0, mtf_h2 = 0, mtf_v1 = 0, mtf_v2 = 0;

    auto calcH1 = std::async(std::launch::async, SFRCalculation,
        std::ref(roi_H1), m_gamma, m_pixelSize, m_testFrequency,
        std::ref(mtf_h1), debugPath + "_H1_mtf.csv", m_debug ? 1 : 0);

    auto calcH2 = std::async(std::launch::async, SFRCalculation,
        std::ref(roi_H2), m_gamma, m_pixelSize, m_testFrequency,
        std::ref(mtf_h2), debugPath + "_H2_mtf.csv", m_debug ? 1 : 0);

    auto calcV1 = std::async(std::launch::async, SFRCalculation,
        std::ref(roi_V1), m_gamma, m_pixelSize, m_testFrequency,
        std::ref(mtf_v1), debugPath + "_V1_mtf.csv", m_debug ? 1 : 0);

    auto calcV2 = std::async(std::launch::async, SFRCalculation,
        std::ref(roi_V2), m_gamma, m_pixelSize, m_testFrequency,
        std::ref(mtf_v2), debugPath + "_V2_mtf.csv", m_debug ? 1 : 0);

    calcH1.get(); calcH2.get(); calcV1.get(); calcV2.get();

    // 取水平和垂直方向的平均值
    double mtfHAvg = ResultLogger::getMtfAverage(mtf_h1, mtf_h2);
    double mtfVAvg = ResultLogger::getMtfAverage(mtf_v1, mtf_v2);

    {
        std::lock_guard<std::mutex> lock(printMutex);
        std::cout << "N" << (fileIdx + 1) << "区域" << aoiIdx
                  << " : MTF_H=" << mtfHAvg << "(" << m_testFrequency << "lp/mm)" << std::endl;
        std::cout << "N" << (fileIdx + 1) << "区域" << aoiIdx
                  << " : MTF_V=" << mtfVAvg << "(" << m_testFrequency << "lp/mm)" << std::endl;
    }

    mtf_H[aoiIdx] = mtfHAvg;
    mtf_V[aoiIdx] = mtfVAvg;
}

// ============================================================================
// 图片文件扫描
// ============================================================================

std::vector<std::filesystem::path> SfrTestRunner::findImages(
    const std::string& path, const std::string& suffix)
{
    std::vector<std::filesystem::path> files;
    std::string ext = "." + suffix;

    try {
        if (!std::filesystem::exists(path)) {
            std::cerr << "警告: 目录不存在 " << path << std::endl;
            return files;
        }

        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            if (entry.is_regular_file() && entry.path().extension() == ext) {
                files.push_back(entry.path());
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "文件系统错误: " << e.what() << std::endl;
    }

    return files;
}

// ============================================================================
// 辅助函数: 矩形边界检查
// ============================================================================

bool SfrTestRunner::AreaCutJudge(cv::Mat image, cv::Rect area)
{
    if (area.x >= 0 && area.y >= 0 && area.width > 0 && area.height > 0) {
        return (image.cols >= area.x + area.width &&
                image.rows >= area.y + area.height);
    }
    return false;
}

// ============================================================================
// 辅助函数: 直线质心精调
// ============================================================================

cv::Point SfrTestRunner::LineCentroidFind(cv::Mat Src, int height, int width,
                                           cv::Point LineCenter, cv::Point Center, bool HV)
{
    cv::Mat dst = Src.clone();

    if (HV) {
        // 水平方向 (H): 在 X 方向搜索质心
        int roiStart = LineCenter.x - width / 4;
        int roiEnd   = LineCenter.x + width / 4;

        int sum = 0, count = 0;
        for (int i = roiStart; i <= roiEnd; ++i) {
            if (i < 0 || i >= dst.cols) continue;
            if (dst.ptr<uchar>(LineCenter.y)[i] > 0) {
                sum += i;
                count++;
            }
        }
        if (count != 0) LineCenter.x = sum / count;
    } else {
        // 垂直方向 (V): 在 Y 方向搜索质心
        int roiStart = LineCenter.y - height / 4;
        int roiEnd   = LineCenter.y + height / 4;

        int sum = 0, count = 0;
        for (int i = roiStart; i <= roiEnd; ++i) {
            if (i < 0 || i >= dst.rows) continue;
            if (dst.ptr<uchar>(i)[LineCenter.x] > 0) {
                sum += i;
                count++;
            }
        }
        if (count != 0) LineCenter.y = sum / count;
    }

    return LineCenter;
}

// ============================================================================
// 辅助函数: 点到椭圆归一化距离
// ============================================================================

double SfrTestRunner::Distance_DotToEllipse(cv::Point center,
                                             double majorAxis, double minorAxis,
                                             cv::Point Dot)
{
    double result = std::pow(center.x - Dot.x, 2) / std::pow(majorAxis / 2.0, 2)
                  + std::pow(center.y - Dot.y, 2) / std::pow(minorAxis / 2.0, 2);
    return result;
}

// ============================================================================
// 核心算法: 斜边 ROI 自动识别 (椭圆拟合 + 四区域分割)
// ============================================================================

bool SfrTestRunner::findSfrRois(cv::Mat roi,
                                 cv::Mat& roi_H1, cv::Mat& roi_H2,
                                 cv::Mat& roi_V1, cv::Mat& roi_V2,
                                 const std::string& debugPath,
                                 bool debug, int imageProcessSelect)
{
    cv::Mat edge, roiH2Copy, roiV1Copy, roiV2Copy;
    cv::RNG rng(12345);
    cv::Scalar color(rng.uniform(0, 255), rng.uniform(0, 255), rng.uniform(0, 255));

    // 预处理: 平滑滤波
    cv::blur(roi, edge, cv::Size(3, 3));
    cv::GaussianBlur(edge, edge, cv::Size(5, 5), 0);

    // 根据图像来源选择不同的边缘检测参数
    switch (imageProcessSelect) {
        case 1:  // 模拟图像 (如 P223 模组 BMP)
            cv::Canny(edge, edge, 10, 70);
            cv::adaptiveThreshold(edge, edge, 255, cv::ADAPTIVE_THRESH_MEAN_C,
                                  cv::THRESH_BINARY_INV, 3, 10);
            break;
        case 2:  // 数字图像 (如 P223 整机 JPG)
            cv::Canny(edge, edge, 10, 50);
            cv::adaptiveThreshold(edge, edge, 255, cv::ADAPTIVE_THRESH_MEAN_C,
                                  cv::THRESH_BINARY_INV, 5, 10);
            break;
        default:
            cv::Canny(edge, edge, 10, 70);
            cv::adaptiveThreshold(edge, edge, 255, cv::ADAPTIVE_THRESH_MEAN_C,
                                  cv::THRESH_BINARY_INV, 3, 10);
            break;
    }

    if (debug) {
        cv::imwrite(debugPath + " edge0.jpg", edge);
    }

    // 查找轮廓
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(edge, contours, hierarchy, cv::RETR_LIST, cv::CHAIN_APPROX_NONE);

    // 计算每个轮廓的矩和质心
    std::vector<cv::Moments> mu(contours.size());
    std::vector<cv::Point2f>  mc(contours.size());

    for (size_t i = 0; i < contours.size(); ++i) {
        mu[i] = cv::moments(contours[i], false);
        mc[i] = cv::Point2f(static_cast<float>(mu[i].m10 / mu[i].m00),
                            static_cast<float>(mu[i].m01 / mu[i].m00));
    }

    // 找到面积最大的轮廓 (目标斜边圆)
    double area_max = 0;
    size_t i_max = 0;
    for (size_t i = 0; i < contours.size(); ++i) {
        double area = mu[i].m00;
        if (area > area_max) {
            area_max = area;
            i_max = i;
        }
    }

    if (area_max <= 10) {
        return false;  // 未找到有效轮廓
    }

    // 调试: 绘制找到的轮廓
    if (debug) {
        cv::Mat drawing = cv::Mat::zeros(edge.size(), CV_8UC3);
        cv::drawContours(drawing, contours, static_cast<int>(i_max), color, 0, 8, hierarchy, 0);
        cv::circle(drawing, mc[i_max], 4, color, -1, 8, 0);
        cv::imwrite(debugPath + " drawing.jpg", drawing);
    }

    // 椭圆拟合
    cv::Mat pointsf;
    cv::Mat(contours[i_max]).convertTo(pointsf, CV_32F);
    cv::RotatedRect ellipse = cv::fitEllipse(pointsf);

    cv::Point2f center = ellipse.center;
    int roiHeight = static_cast<int>(ellipse.size.height / 4.0);
    int roiWidth  = static_cast<int>(ellipse.size.width  / 4.0);

    // 判断椭圆的主轴方向 (约 90° = X 方向, 约 0° = Y 方向)
    int ellipse_x, ellipse_y;
    if (std::abs(ellipse.angle - 90) < 10) {
        ellipse_x = static_cast<int>(ellipse.size.height);
        ellipse_y = static_cast<int>(ellipse.size.width);
    } else {
        ellipse_x = static_cast<int>(ellipse.size.width);
        ellipse_y = static_cast<int>(ellipse.size.height);
    }

    // 使用可配置的斜边角度 (原硬编码 theta=8°)
    double thetaRad = m_sfrAngle / 180.0 * M_PI;

    // 计算 4 个子 ROI 的初始中心位置
    double offsetX = ellipse_y / 4.0 * std::tan(thetaRad);
    double offsetY = ellipse_y / 4.0;

    cv::Point H1center(static_cast<int>(center.x + offsetX),
                        static_cast<int>(center.y - offsetY));
    cv::Point H2center(static_cast<int>(center.x - offsetX),
                        static_cast<int>(center.y + offsetY));
    cv::Point V1center(static_cast<int>(center.x - ellipse_x / 4.0),
                        static_cast<int>(center.y - ellipse_x / 4.0 * std::tan(thetaRad)));
    cv::Point V2center(static_cast<int>(center.x + ellipse_x / 4.0),
                        static_cast<int>(center.y + ellipse_x / 4.0 * std::tan(thetaRad)));

    // 质心精调: 沿边缘方向微调使 ROI 更精确对准斜边中心
    H1center = LineCentroidFind(edge, roiHeight, roiWidth, H1center, center, true);   // H方向
    H2center = LineCentroidFind(edge, roiHeight, roiWidth, H2center, center, true);
    V1center = LineCentroidFind(edge, roiHeight, roiWidth, V1center, center, false);  // V方向
    V2center = LineCentroidFind(edge, roiHeight, roiWidth, V2center, center, false);

    // 构建 4 个 ROI 矩形
    cv::Point Dot_H1(H1center.x - roiWidth / 2,  H1center.y - roiHeight / 2);
    cv::Point Dot_H2(H2center.x - roiWidth / 2,  H2center.y - roiHeight / 2);
    cv::Point Dot_V1(V1center.x - roiHeight / 2, V1center.y - roiWidth / 2);
    cv::Point Dot_V2(V2center.x - roiHeight / 2, V2center.y - roiWidth / 2);

    cv::Rect H1(Dot_H1.x, Dot_H1.y, roiWidth,  roiHeight);
    cv::Rect H2(Dot_H2.x, Dot_H2.y, roiWidth,  roiHeight);
    cv::Rect V1(Dot_V1.x, Dot_V1.y, roiHeight, roiWidth);
    cv::Rect V2(Dot_V2.x, Dot_V2.y, roiHeight, roiWidth);

    // 边界检查: 若 ROI 角点超出椭圆范围，用椭圆尺寸修正
    // (修复: 原代码此处错误地修改了 H1 而非 H2/V1/V2)
    if (Distance_DotToEllipse(center, ellipse_x, ellipse_y, Dot_H1) > ELLIPSE_BOUNDARY_RATIO) {
        H1.height = static_cast<int>(ellipse_y / 4.0);
        H1.y = H1center.y - roiHeight / 2;
    }
    if (Distance_DotToEllipse(center, ellipse_x, ellipse_y, Dot_H2) > ELLIPSE_BOUNDARY_RATIO) {
        H2.height = static_cast<int>(ellipse_y / 4.0);
        H2.y = H2center.y - roiHeight / 2;
    }
    if (Distance_DotToEllipse(center, ellipse_x, ellipse_y, Dot_V1) > ELLIPSE_BOUNDARY_RATIO) {
        V1.width = static_cast<int>(ellipse_x / 4.0);
        V1.x = V1center.x - roiWidth / 2;
    }
    if (Distance_DotToEllipse(center, ellipse_x, ellipse_y, Dot_V2) > ELLIPSE_BOUNDARY_RATIO) {
        V2.width = static_cast<int>(ellipse_x / 4.0);
        V2.x = V2center.x - roiWidth / 2;
    }

    // 验证所有 AOI 都在图像范围内，然后裁剪
    if (AreaCutJudge(roi, H1) && AreaCutJudge(roi, H2) &&
        AreaCutJudge(roi, V1) && AreaCutJudge(roi, V2))
    {
        roi_H1 = roi(H1);

        cv::Mat roiH2Copy = roi.clone();
        roi_H2 = roiH2Copy(H2);

        // V 方向 ROI 需要转置使斜边变为水平方向 (SFR 算法要求)
        cv::transpose(roi(V1), roiV1Copy);
        cv::flip(roiV1Copy, roi_V1, 0);   // 顺时针 90°

        cv::transpose(roi(V2), roiV2Copy);
        cv::flip(roiV2Copy, roi_V2, 1);   // 逆时针 90°

        if (debug) {
            cv::Mat dst = roi.clone();
            cv::circle(dst, center, 0, cv::Scalar(255, 0, 0), 3, 8, 0);
            cv::ellipse(dst, ellipse, cv::Scalar(255, 0, 0), 1, cv::LINE_AA);
            cv::rectangle(dst, H1, cv::Scalar(0, 0, 255), 2);
            cv::rectangle(dst, H2, cv::Scalar(0, 0, 255), 2);
            cv::rectangle(dst, V1, cv::Scalar(0, 0, 255), 2);
            cv::rectangle(dst, V2, cv::Scalar(0, 0, 255), 2);
            cv::imwrite(debugPath + " dst.jpg", dst);
        }

        return true;
    }

    return false;
}
