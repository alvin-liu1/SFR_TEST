/**
 * @file    ResultLogger.h
 * @brief   SFR 测试结果判定与日志记录模块
 * @details 负责将 MTF 实测值与阈值比较，输出 PASS/FAIL/RETEST 判定，
 *          并将结果写入 CSV 日志和 test.txt 状态文件。
 */

#pragma once
#include <string>
#include <vector>
#include <fstream>

/**
 * @enum TestResult
 * @brief SFR 测试判定结果
 */
enum class TestResult {
    PASS,    ///< 所有区域 MTF 均 ≥ 阈值，测试通过
    FAIL,    ///< 存在区域 MTF < 阈值，测试失败
    RETEST   ///< 数据异常 (MTF=0 或 ≥1)，需要重新测试
};

/**
 * @class ResultLogger
 * @brief 测试结果判定与 CSV 日志记录
 */
class ResultLogger
{
public:
    /**
     * @brief 构造函数
     * @param logPath       CSV 日志文件路径
     * @param testFrequency 测试频率 (lp/mm)，写入 CSV 表头
     * @param areaCount     测试区域数量
     * @param thresholds    各区域 MTF 阈值数组 [H0, V0, H1, V1, ...]
     */
    ResultLogger(const std::string& logPath, double testFrequency,
                 int areaCount, const std::vector<double>& thresholds);

    /**
     * @brief 判定测试结果
     * @param mtf_H  各区域水平方向 MTF 值
     * @param mtf_V  各区域垂直方向 MTF 值
     * @return TestResult::PASS / FAIL / RETEST
     */
    TestResult judgeResult(const std::vector<double>& mtf_H,
                           const std::vector<double>& mtf_V);

    /**
     * @brief 将单张图片的测试结果写入 CSV 日志
     * @param index    图片序号
     * @param filename 图片文件名
     * @param mtf_H    各区域水平 MTF 值
     * @param mtf_V    各区域垂直 MTF 值
     * @param result   判定结果
     */
    void logResultToCsv(int index, const std::string& filename,
                        const std::vector<double>& mtf_H,
                        const std::vector<double>& mtf_V,
                        TestResult result);

    /**
     * @brief 更新整机 (ZJ) 项目的 test.txt 状态文件
     * @param txtPath     test.txt 文件路径
     * @param testFailed  是否存在失败
     * @param testSuccess 是否存在成功
     */
    void updateTestTxt(const std::string& txtPath, bool testFailed, bool testSuccess);

    /**
     * @brief 计算两个 MTF 值的平均 (若其一为 0 则只取非零值)
     */
    static double getMtfAverage(double v1, double v2);

private:
    std::string m_logPath;              ///< CSV 日志文件路径
    double m_testFrequency;             ///< 测试频率
    int m_areaCount;                    ///< 区域数量
    std::vector<double> m_thresholds;   ///< 阈值数组 [H0, V0, H1, V1, ...]
};
