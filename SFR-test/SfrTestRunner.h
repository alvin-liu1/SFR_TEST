/**
 * @file    SfrTestRunner.h
 * @brief   SFR 测试主控类
 * @details 作为整个清晰度测试流程的编排器，负责:
 *          1. 配置加载与路径管理
 *          2. 图片文件扫描
 *          3. 斜边 ROI 自动识别 (椭圆拟合 + 四区域分割)
 *          4. SFR 计算调度 (支持并行化)
 *          5. 绘图模式 (标定线绘制)
 *          6. 委托 AoiManager 管理 AOI 数据
 *          7. 委托 ResultLogger 进行结果判定和日志记录
 */

#pragma once

#include "RrConfig.h"
#include "AoiManager.h"
#include "ResultLogger.h"
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <memory>

// ============================================================================
// 常量定义 (原硬编码魔数)
// ============================================================================

/** @brief 椭圆边界检查比例阈值: 若 ROI 角点到椭圆中心的归一化距离 > 此值，视为超出椭圆 */
const double ELLIPSE_BOUNDARY_RATIO = 0.9;

/** @brief 默认斜边倾斜角度 (度)，用于 ROI 定位偏移计算 */
const double DEFAULT_SFR_ANGLE = 8.0;

/**
 * @class SfrTestRunner
 * @brief SFR 测试流程编排器
 *
 * 支持两种运行模式:
 *   - SFR 计算模式: 批量处理图片，计算 MTF 值并与阈值比对
 *   - 绘图模式: 在图片上绘制 TV line / SFR 标定线用于目视检查
 */
class SfrTestRunner
{
public:
    /**
     * @brief 构造函数: 加载配置并初始化所有子系统
     * @param config     已加载的配置对象
     * @param originPath 应用程序根目录
     */
    SfrTestRunner(rr::RrConfig& config, const std::string& originPath);

    /** @brief 启动测试流程 (根据配置自动选择 SFR 模式或绘图模式) */
    void run();

private:
    // ========================================================================
    // 初始化
    // ========================================================================

    /** @brief 设置所有路径 (图片/结果/配置/AOI/日志) */
    void setupPaths();

    /** @brief 从配置加载项目参数 (AreaNum, PixelSize, TestFreq, Gamma 等) */
    void loadConfig();

    /** @brief 从配置加载 SFR 阈值数组 */
    void loadSfrThreads();

    // ========================================================================
    // 执行模式
    // ========================================================================

    /** @brief 绘图模式: 在图片上绘制 TV line 和 SFR 标定线 */
    void runDrawingMode();

    /** @brief SFR 计算模式: 批量处理图片，计算 MTF 并判定结果 */
    void runSfrMode();

    // ========================================================================
    // 图像处理与 AOI
    // ========================================================================

    /** @brief 扫描指定目录下指定后缀的图片文件 */
    std::vector<std::filesystem::path> findImages(const std::string& path,
                                                    const std::string& suffix);

    /** @brief 判断矩形区域是否在图像范围内 */
    bool AreaCutJudge(cv::Mat image, cv::Rect area);

    /** @brief 在 AOI 内识别斜边 ROI (椭圆拟合 + 四区域分割) */
    bool findSfrRois(cv::Mat roi,
                     cv::Mat& roi_H1, cv::Mat& roi_H2,
                     cv::Mat& roi_V1, cv::Mat& roi_V2,
                     const std::string& debugPath,
                     bool debug = false,
                     int imageProcessSelect = 1);

    /** @brief 直线质心精调 (沿边缘方向微调中心位置) */
    cv::Point LineCentroidFind(cv::Mat Src, int height, int width,
                               cv::Point LineCenter, cv::Point Center, bool HV);

    /** @brief 计算点到椭圆的归一化距离 (用于边界检查) */
    double Distance_DotToEllipse(cv::Point center, double majorAxis,
                                  double minorAxis, cv::Point Dot);

    // ========================================================================
    // 成员变量
    // ========================================================================

    // --- 配置与依赖 ---
    rr::RrConfig& m_config;           ///< 配置文件解析器引用
    AoiManager    m_aoiManager;       ///< AOI 区域管理器
    std::unique_ptr<ResultLogger> m_logger;  ///< 结果判定与日志记录器

    // --- 路径 ---
    std::string m_originPath;         ///< 应用程序根目录
    std::string m_projectName;        ///< 项目名称 (如 P223_MZ)
    std::string m_imageSuffix;        ///< 图片后缀 (bmp / jpg)
    std::string m_resultsPath;        ///< 结果输出路径
    std::string m_picturesPath;       ///< 图片读取路径
    std::string m_configPath;         ///< 配置文件路径
    std::string m_picturesFailPath;   ///< 失败图片转移路径
    std::string m_aoiConfigPath;      ///< AOI 配置文件路径
    std::string m_sfrLogPath;         ///< SFR 结果日志路径
    std::string m_testTxtPath;        ///< 整机测试状态文件路径

    // --- 测试参数 ---
    int    m_testAreaNum;             ///< 测试区域数量
    double m_pixelSize;               ///< 像素尺寸 (um)
    double m_testFrequency;           ///< 测试频率 (lp/mm)
    double m_gamma;                   ///< Gamma 值
    bool   m_aoiSave;                 ///< 是否需要交互式 AOI 选取
    bool   m_debug;                   ///< 是否输出调试中间文件
    bool   m_deleteOriginPic;         ///< 是否删除/移动原始图片
    bool   m_isZjProject;             ///< 是否为整机项目 (ZJ)
    int    m_imageProcessSelect;      ///< 图像处理算法选择 (1=模拟, 2=数字)

    // --- 可配置参数 (原硬编码魔数) ---
    double m_sfrAngle;                ///< SFR 斜边角度 (度), 默认 8°
    int    m_drawingLineLength;       ///< 绘图模式标定线长度 (像素), 默认 300

    // --- 阈值 ---
    std::vector<double> m_sfrThreads; ///< 各区域 MTF 阈值 [H0,V0, H1,V1, ...]

    // ========================================================================
    // 并行化支持
    // ========================================================================

    /** @brief 并行计算单个 AOI 内 4 个子 ROI 的 MTF */
    void processAoiParallel(const cv::Mat& img, int aoiIdx,
                            const std::string& filename, int fileIdx,
                            std::vector<double>& mtf_H, std::vector<double>& mtf_V,
                            std::mutex& printMutex);
};
