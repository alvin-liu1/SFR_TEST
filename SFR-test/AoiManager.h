/**
 * @file    AoiManager.h
 * @brief   AOI (Area of Interest) 区域管理器
 * @details 负责测试感兴趣区域的加载、交互式选择和保存。
 *          AOI 数据以 CSV 格式持久化，每行格式: 序号, X, Y, Width, Height
 */

#pragma once
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

/**
 * @class AoiManager
 * @brief AOI 区域的加载/保存/交互式选择
 *
 * CSV 文件格式:
 *   序号,REC_X,REC_Y,Width,Height
 *   0,100,200,50,50
 *   1,300,400,50,50
 */
class AoiManager
{
public:
    AoiManager() = default;

    /**
     * @brief 从 CSV 文件加载 AOI 矩形数据
     * @param csvPath   CSV 文件完整路径
     * @param maxCount  期望加载的最大 AOI 数量 (与配置中的 AreaNum 对应)
     * @return 加载到的 AOI 矩形数量
     * @throws std::runtime_error 当文件不存在且非首次创建场景时抛出
     */
    int loadAoiData(const std::string& csvPath, int maxCount);

    /**
     * @brief 交互式选择并保存 AOI 区域 (使用 OpenCV selectROI)
     * @param sampleImage 用于选择 AOI 的示例图像
     * @param count       需要选择的区域数量
     * @param csvPath     保存的 CSV 文件路径
     */
    void saveAoiData(const cv::Mat& sampleImage, int count, const std::string& csvPath);

    /**
     * @brief 获取已加载的 AOI 矩形列表
     */
    const std::vector<cv::Rect>& getRects() const { return m_aoiRects; }

    /**
     * @brief 获取 AOI 矩形数量
     */
    int getCount() const { return static_cast<int>(m_aoiRects.size()); }

    /**
     * @brief 清空所有 AOI 数据
     */
    void clear() { m_aoiRects.clear(); }

private:
    std::vector<cv::Rect> m_aoiRects;  ///< AOI 矩形区域列表
};
