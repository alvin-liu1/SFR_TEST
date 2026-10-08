/**
 * @file    ResultLogger.cpp
 * @brief   SFR 测试结果判定与日志记录实现
 */

#include "ResultLogger.h"
#include <iostream>
#include <iomanip>
#include <ctime>
#include <fstream>
#include <sstream>
#include <filesystem>

ResultLogger::ResultLogger(const std::string& logPath, double testFrequency,
                           int areaCount, const std::vector<double>& thresholds)
    : m_logPath(logPath)
    , m_testFrequency(testFrequency)
    , m_areaCount(areaCount)
    , m_thresholds(thresholds)
{
    // 如果是新文件，写入 CSV 表头
    if (!std::filesystem::exists(m_logPath)) {
        std::ofstream file(m_logPath);
        if (file) {
            file << "序号,测试时间,文件名,频率";
            for (int i = 0; i < m_areaCount; ++i) {
                file << ",MTF_H_" << i << ",MTF_V_" << i;
            }
            file << ",状态\n";
            file.close();
        }
    }
}

TestResult ResultLogger::judgeResult(const std::vector<double>& mtf_H,
                                      const std::vector<double>& mtf_V)
{
    TestResult result = TestResult::PASS;

    for (int i = 0; i < m_areaCount; ++i) {
        double hVal = mtf_H[i];
        double vVal = mtf_V[i];

        // 数据异常检查: MTF 为 0 或 ≥ 1 需要重测
        if (hVal == 0.0 || hVal >= 1.0 || vVal == 0.0 || vVal >= 1.0) {
            return TestResult::RETEST;
        }

        // 阈值检查 (m_thresholds 格式: [H0, V0, H1, V1, ...])
        size_t hIdx = 2 * i;
        size_t vIdx = 2 * i + 1;
        double hThreshold = (hIdx < m_thresholds.size()) ? m_thresholds[hIdx] : 0.0;
        double vThreshold = (vIdx < m_thresholds.size()) ? m_thresholds[vIdx] : 0.0;

        if (hVal < hThreshold || vVal < vThreshold) {
            result = TestResult::FAIL;  // 任一区域不达标即为失败
        }
    }

    return result;
}

void ResultLogger::logResultToCsv(int index, const std::string& filename,
                                   const std::vector<double>& mtf_H,
                                   const std::vector<double>& mtf_V,
                                   TestResult result)
{
    std::ofstream file(m_logPath, std::ios::app);
    if (!file) {
        std::cerr << "错误: 无法写入日志文件 " << m_logPath << std::endl;
        return;
    }

    // 获取当前时间
    char timeBuf[100] = { 0 };
    time_t now = time(nullptr);
    struct tm timeInfo;
    localtime_s(&timeInfo, &now);
    std::sprintf(timeBuf, "%d/%d/%d %02d:%02d:%02d",
                 1900 + timeInfo.tm_year, 1 + timeInfo.tm_mon, timeInfo.tm_mday,
                 timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec);

    file << index << "," << timeBuf << "," << filename << "," << m_testFrequency;

    for (int i = 0; i < m_areaCount; ++i) {
        file << "," << std::fixed << std::setprecision(3) << mtf_H[i]
             << "," << std::fixed << std::setprecision(3) << mtf_V[i];
    }

    switch (result) {
        case TestResult::PASS:   file << ",success\n"; break;
        case TestResult::FAIL:   file << ",fail\n";    break;
        case TestResult::RETEST: file << ",retest\n";  break;
    }

    file.close();
}

void ResultLogger::updateTestTxt(const std::string& txtPath, bool testFailed, bool testSuccess)
{
    bool shouldPass = !testFailed && testSuccess;

    // 读取现有内容
    std::string content;
    bool found = false;

    std::ifstream inFile(txtPath);
    if (inFile) {
        std::string line;
        while (std::getline(inFile, line)) {
            if (line.find("TEST_LENS_DEFINITION") != std::string::npos) {
                found = true;
                content += "TEST_LENS_DEFINITION=";
                content += shouldPass ? "pass\n" : "unpass\n";
            } else {
                content += line + "\n";
            }
        }
        inFile.close();
    }

    // 未找到则追加
    if (!found) {
        content += "TEST_LENS_DEFINITION=";
        content += shouldPass ? "pass\n" : "unpass\n";
    }

    // 写回文件
    std::ofstream outFile(txtPath, std::ios::out | std::ios::trunc);
    outFile << content;
    outFile.close();
}

double ResultLogger::getMtfAverage(double v1, double v2)
{
    if (v1 != 0.0 && v2 != 0.0) return (v1 + v2) / 2.0;
    if (v1 != 0.0) return v1;
    if (v2 != 0.0) return v2;
    return 0.0;
}
