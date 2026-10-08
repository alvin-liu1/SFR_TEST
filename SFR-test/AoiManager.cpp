/**
 * @file    AoiManager.cpp
 * @brief   AOI 区域管理器实现
 */

#include "AoiManager.h"
#include <fstream>
#include <sstream>
#include <iostream>

int AoiManager::loadAoiData(const std::string& csvPath, int maxCount)
{
    m_aoiRects.clear();

    std::ifstream aoiFile(csvPath);
    if (!aoiFile) {
        // 文件不存在时抛出异常 (上层可捕获并决定是否触发交互式选择)
        throw std::runtime_error("错误: 无法打开 AOI 文件: " + csvPath);
    }

    std::string line;
    std::getline(aoiFile, line);  // 跳过 CSV 表头

    while (std::getline(aoiFile, line) && static_cast<int>(m_aoiRects.size()) < maxCount) {
        std::stringstream ss(line);
        std::string segment;
        std::vector<int> data;

        while (std::getline(ss, segment, ',')) {
            data.push_back(std::atoi(segment.c_str()));
        }

        // CSV 格式: 序号, X, Y, Width, Height
        if (data.size() >= 5) {
            m_aoiRects.emplace_back(data[1], data[2], data[3], data[4]);
        }
    }

    aoiFile.close();

    if (static_cast<int>(m_aoiRects.size()) != maxCount) {
        std::cerr << "警告: AOI 文件中读取到 " << m_aoiRects.size()
                  << " 个区域，但配置要求 " << maxCount << " 个，可能不匹配。" << std::endl;
    }

    return static_cast<int>(m_aoiRects.size());
}

void AoiManager::saveAoiData(const cv::Mat& sampleImage, int count, const std::string& csvPath)
{
    std::ofstream aoiFile(csvPath);
    if (!aoiFile) {
        std::cerr << "错误: 无法创建 AOI 文件: " << csvPath << std::endl;
        return;
    }

    aoiFile << "序号,REC_X,REC_Y,Width,Height\n";
    std::cout << "--- AOI 选取模式 ---" << std::endl;
    std::cout << "请按顺序框选 " << count << " 个区域 (Enter 确认, Esc 取消)。" << std::endl;

    m_aoiRects.clear();

    for (int i = 0; i < count; ++i) {
        std::string windowName = "选取 AOI 区域 " + std::to_string(i);

        cv::Rect roiBox = cv::selectROI(windowName, sampleImage, true, false);
        cv::destroyWindow(windowName);

        if (roiBox.width > 0 && roiBox.height > 0) {
            std::cout << "[保存] AOI " << i << ": X=" << roiBox.x << ", Y=" << roiBox.y
                      << ", W=" << roiBox.width << ", H=" << roiBox.height << std::endl;

            aoiFile << i << "," << roiBox.x << "," << roiBox.y << ","
                    << roiBox.width << "," << roiBox.height << "\n";
            m_aoiRects.push_back(roiBox);
        } else {
            std::cout << "选取无效或取消，AOI 选择终止。" << std::endl;
            break;
        }
    }

    aoiFile.close();
    std::cout << "--- AOI 选取完成 ---" << std::endl;
}
