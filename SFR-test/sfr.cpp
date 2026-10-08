/**
 * @file    sfr.cpp
 * @brief   SFR (Spatial Frequency Response) 算法实现
 * @details 基于 ISO 12233 标准的斜边法 MTF 测量算法。
 *          完整流程:
 *          1. de-Gamma:  图像从 gamma 空间线性化到线性空间
 *          2. CentroidFind:  逐行用一阶导数加权求质心，定位斜边中心
 *          3. SLR (线性回归):  拟合斜边的倾斜角度 (slope)
 *          4. ReduceRows:  按斜边周期裁剪行数，减少边缘效应
 *          5. OverSampling:  4x 超采样，将所有像素投影到一维空间构建 ESF
 *          6. 微分:  ESF → LSF (Line Spread Function)
 *          7. HammingWindows:  加 Hamming 窗抑制频谱泄漏
 *          8. DFT (FFT):  对 LSF 做傅里叶变换得到 MTF 曲线
 *          9. 归一化:  MTF[0]=1.0 (DC 分量归一化)
 *         10. B-Spline 插值:  在离散 MTF 曲线上插值查询目标频率的精确 MTF 值
 *
 * @note   原始代码的 O(N²) DFT 已替换为 OpenCV 的 FFT (O(N log N))
 * @note   原始代码的 new[] 内存泄漏已修复 (使用 std::vector 和 cv::Mat 管理内存)
 */

#include "sfr.h"
#include <opencv2/opencv.hpp>
#include <complex>
#include <cmath>
#include <iostream>
#include <fstream>
#include <vector>
#include <numeric>
#include <algorithm>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// 常量定义
// ============================================================================

/** @brief 超采样倍数 (ISO 12233 标准推荐 4x) */
const int OVER_SAMPLING_FACTOR = 4;

/** @brief 双边滤波窗口直径 */
const int BILATERAL_DIAMETER = 3;

/** @brief 双边滤波 Sigma 值 (颜色空间 / 坐标空间) */
const double BILATERAL_SIGMA_COLOR = 200.0;
const double BILATERAL_SIGMA_SPACE = 200.0;

// ============================================================================
// 模块内部辅助函数
// ============================================================================

/**
 * @brief 对图像进行 de-Gamma 处理 (将 gamma 空间转换到线性空间)
 * @param Src   [in/out] 8 位单通道灰度图像，原地修改
 * @param gamma Gamma 值，若为 1.0 则跳过处理
 *
 * 公式:  linear = 255 * (pixel / 255) ^ (1/gamma)
 */
void de_Gamma(cv::Mat& Src, double gamma)
{
    if (Src.channels() != 1 || Src.empty() || gamma == 1.0) {
        return;
    }

    const double inv_gamma = 1.0 / gamma;

    for (int i = 0; i < Src.rows; ++i)
    {
        uchar* rowPtr = Src.ptr<uchar>(i);
        for (int j = 0; j < Src.cols; ++j)
        {
            double normalized = static_cast<double>(rowPtr[j]) / 255.0;
            rowPtr[j] = static_cast<uchar>(255.0 * std::pow(normalized, inv_gamma));
        }
    }
}

/**
 * @brief 简单线性回归 (Simple Linear Regression)
 * @param Cen_Shifts 每行的 X 坐标 (质心位置)
 * @param y_shifts   每行的 Y 坐标 (行索引偏移)
 * @param a          [out] 截距 (intercept)
 * @param b          [out] 斜率 (slope)
 *
 * 模型: y = a + b * x
 * 使用最小二乘法求解 b = Sxy / Sxx, a = ȳ - b * x̄
 */
void SLR(std::vector<double>& Cen_Shifts, std::vector<double>& y_shifts, double* a, double* b)
{
    *a = 0;
    *b = 0;

    int n = static_cast<int>(y_shifts.size());
    if (n == 0 || n != static_cast<int>(Cen_Shifts.size())) {
        std::cerr << "SLR 错误: 输入数组大小不匹配或为0。" << std::endl;
        return;
    }

    // 计算均值
    double xavg = 0, yavg = 0;
    for (int i = 0; i < n; ++i) {
        yavg += y_shifts[i];
        xavg += Cen_Shifts[i];
    }
    xavg /= n;
    yavg /= n;

    // 计算斜率 b = Σ((x_i - x̄)(y_i - ȳ)) / Σ((x_i - x̄)²)
    double numerator = 0.0, denominator = 0.0;
    for (int i = 0; i < n; ++i) {
        double dx = (Cen_Shifts[i] - xavg);
        numerator += dx * (y_shifts[i] - yavg);
        denominator += dx * dx;
    }

    if (denominator == 0) {
        std::cerr << "SLR 错误: 分母为0 (可能是垂直边缘)。" << std::endl;
        return;
    }

    *b = numerator / denominator;
    *a = yavg - (*b) * xavg;
}

/**
 * @brief 逐行质心定位 (找到每行斜边边缘的中心位置)
 * @param Src      [in] ROI 图像 (CV_8UC1)
 * @param y_shifts [out] 每行的 Y 坐标偏移 (行号 - 中心行号)
 * @param CCoffset [out] 图像中心行的质心 X 坐标
 * @return 每行质心的 X 坐标偏移数组 (空数组表示失败)
 *
 * 使用一阶导数加权质心法:
 *   centroid = Σ( (p[j+1]-p[j]) * j ) / Σ( p[j+1]-p[j] )
 *
 * 同时:
 *   - 先用双边滤波平滑图像
 *   - 在质心附近 ±5 像素保留原始值 (用高斯模糊平滑其他区域)
 *   - 检查边缘是否太靠近图像边界
 */
std::vector<double> CentroidFind(cv::Mat& Src, std::vector<double>& y_shifts, double* CCoffset)
{
    int height = Src.rows, width = Src.cols;
    std::vector<double> Cen_Shifts(height);

    // 双边滤波: 平滑噪声同时保留边缘
    cv::Mat tempSrc;
    cv::bilateralFilter(Src, tempSrc, BILATERAL_DIAMETER,
                        BILATERAL_SIGMA_COLOR, BILATERAL_SIGMA_SPACE);

    // 逐行质心计算
    for (int i = 0; i < height; ++i)
    {
        double molecule = 0, denominator = 0;
        uchar* rowPtr = tempSrc.ptr<uchar>(i);
        for (int j = 0; j < width - 1; ++j)
        {
            double diff = static_cast<double>(rowPtr[j + 1]) - static_cast<double>(rowPtr[j]);
            molecule += diff * j;
            denominator += diff;
        }
        if (denominator == 0) {
            std::cerr << "CentroidFind 错误: 第 " << i << " 行分母为0。" << std::endl;
            return {};
        }
        Cen_Shifts[i] = molecule / denominator;
    }

    // 去噪处理: 在质心 ±5 像素范围保留原始值，其余用高斯模糊平滑
    cv::Mat tempSrcClone = Src.clone();
    cv::GaussianBlur(Src, Src, cv::Size(3, 3), 0);
    for (int i = 0; i < height; ++i)
    {
        uchar* srcPtr = Src.ptr<uchar>(i);
        uchar* origPtr = tempSrcClone.ptr<uchar>(i);
        for (int j = static_cast<int>(Cen_Shifts[i]) - 5;
             j < static_cast<int>(Cen_Shifts[i]) + 5; ++j)
        {
            if (j >= 0 && j < width) {
                srcPtr[j] = origPtr[j];
            }
        }
    }

    // 检查边缘是否太靠近图像边界
    if (Cen_Shifts[0] < 2.0 || width - Cen_Shifts[0] < 2.0) {
        std::cerr << "错误: ROI 中的边缘太靠近图像角点 (顶部)。" << std::endl;
        return {};
    }
    if (Cen_Shifts[height - 1] < 2.0 || width - Cen_Shifts[height - 1] < 2.0) {
        std::cerr << "错误: ROI 中的边缘太靠近图像角点 (底部)。" << std::endl;
        return {};
    }

    // 以中心行为基准计算偏移
    int halfY = height / 2;
    *CCoffset = Cen_Shifts[halfY];
    for (int i = 0; i < height; ++i) {
        Cen_Shifts[i] -= *CCoffset;
        y_shifts[i] = i - halfY;
    }

    return Cen_Shifts;
}

/**
 * @brief 超采样重建 ESF (Edge Spread Function) 并微分得到 LSF
 * @param Src         截断后的 ROI 图像
 * @param slope       斜边斜率
 * @param CCoffset    中心偏移
 * @param height      图像高度
 * @param width       图像宽度
 * @param SamplingLen [in/out] 输入: width*4, 输出: 截取 80% 后的长度
 * @return LSF (Line Spread Function) 数组
 *
 * 步骤:
 *   1. 计算每行的投影偏移 RowShifts[i] = (i - halfY) / slope + CCoffset
 *   2. 将所有像素按一维坐标 (j - RowShifts[i]) 4x 超采样投影到 DataMap
 *   3. 对每个 bin 内的像素值取平均，构建 ESF
 *   4. 填补空 bin (使用最近邻非空 bin 的值)
 *   5. 截取中间 80% (去除两端噪声)
 *   6. 微分 ESF → LSF
 */
std::vector<double> OverSampling(cv::Mat& Src, double slope, double CCoffset,
                                  int height, int width, int* SamplingLen)
{
    int halfY = height >> 1;

    // 1. 计算每行投影偏移
    std::vector<double> RowShifts(height);
    for (int i = 0; i < height; ++i) {
        RowShifts[i] = static_cast<double>(i - halfY) / slope + CCoffset;
    }

    // 2. 构建超采样映射
    // DataMap: 每个像素投影到的一维坐标
    // Datas:   对应像素的灰度值
    int totalPixels = height * width;
    std::vector<double> DataMap(totalPixels);
    std::vector<double> Datas(totalPixels);

    for (int i = 0; i < height; ++i) {
        int baseIdx = width * i;
        for (int j = 0; j < width; ++j) {
            DataMap[baseIdx + j] = j - RowShifts[i];
            Datas[baseIdx + j] = static_cast<double>(Src.at<uchar>(i, j));
        }
    }

    // 3. 4x 超采样 binning
    int originalLen = *SamplingLen;
    std::vector<double> SamplingBar(originalLen, 0.0);  // ESF 累加器
    std::vector<int>    MappingCount(originalLen, 0);    // 每个 bin 的像素计数

    for (int i = 0; i < totalPixels; ++i) {
        int binIdx = static_cast<int>(OVER_SAMPLING_FACTOR * DataMap[i]);
        if (binIdx >= 0 && binIdx < originalLen) {
            SamplingBar[binIdx] += Datas[i];
            MappingCount[binIdx]++;
        }
    }

    // 4. 平均每个 bin 并填补空 bin (用最近邻)
    for (int i = 0; i < originalLen; ++i) {
        if (MappingCount[i] == 0) {
            // 空 bin: 向前搜索最近的非空 bin 赋值
            bool found = false;
            if (i == 0) {
                // 左边界: 向右搜索
                for (int k = 1; i + k < originalLen; ++k) {
                    if (MappingCount[i + k] != 0) {
                        SamplingBar[i] = SamplingBar[i + k] / MappingCount[i + k];
                        found = true;
                        break;
                    }
                }
            } else {
                // 向左搜索
                for (int k = 1; i - k >= 0; ++k) {
                    if (MappingCount[i - k] != 0) {
                        SamplingBar[i] = SamplingBar[i - k];
                        found = true;
                        break;
                    }
                }
                // 向左未找到则向右搜索
                if (!found) {
                    for (int k = 1; i + k < originalLen; ++k) {
                        if (MappingCount[i + k] != 0) {
                            SamplingBar[i] = SamplingBar[i + k] / MappingCount[i + k];
                            break;
                        }
                    }
                }
            }
        } else {
            SamplingBar[i] /= MappingCount[i];
        }
    }

    // 5. 截取中间 80% 以去除两端噪声
    int newLen = static_cast<int>(originalLen * 0.8);
    *SamplingLen = newLen;

    // 6. 微分: ESF → LSF
    std::vector<double> LSF(newLen, 0.0);
    int offset = static_cast<int>(originalLen * 0.1);
    for (int i = offset, j = 1; j < newLen; ++i, ++j) {
        LSF[j] = SamplingBar[i + 1] - SamplingBar[i];
    }
    LSF[0] = LSF[1];  // 首元素用相邻值填充

    return LSF;
}

/**
 * @brief 应用 Hamming 窗函数
 * @param deSampling [in/out] LSF 数据，先做峰值居中平移，再加窗
 * @param SamplingLen LSF 数据的长度
 * @return 加窗后的 LSF
 *
 * Hamming 窗公式: w[n] = 0.54 - 0.46 * cos(2π * n / (N-1))
 * 目的: 抑制 DFT 的频谱泄漏效应
 */
std::vector<double> HammingWindows(std::vector<double>& deSampling, int SamplingLen)
{
    // 1. 找到 LSF 峰值位置
    double peakValue = 0;
    for (int i = 0; i < SamplingLen; ++i) {
        if (std::fabs(deSampling[i]) > std::fabs(peakValue)) {
            peakValue = deSampling[i];
        }
    }

    int L_location = -1, R_location = -1;
    for (int i = 0; i < SamplingLen; ++i) {
        if (deSampling[i] == peakValue) {
            if (L_location < 0) L_location = i;
            R_location = i;
        }
    }
    int PeakOffset = (R_location + L_location) / 2 - SamplingLen / 2;

    // 2. 将峰值平移到数据中心
    std::vector<double> tempData(SamplingLen);
    if (PeakOffset != 0) {
        for (int i = 0; i < SamplingLen; ++i) {
            int newIdx = i - PeakOffset;
            if (newIdx >= 0 && newIdx < SamplingLen) {
                tempData[newIdx] = deSampling[i];
            }
        }
    } else {
        tempData = deSampling;
    }

    // 3. 应用 Hamming 窗
    for (int i = 0; i < SamplingLen; ++i) {
        tempData[i] *= (0.54 - 0.46 * std::cos(2.0 * M_PI * i / (SamplingLen - 1)));
    }

    return tempData;
}

/**
 * @brief 离散傅里叶变换 (使用 OpenCV FFT, O(N log N))
 * @param data [in/out] 输入: LSF 数据; 输出: MTF 幅值 (取模后)
 * @param size 数据长度
 *
 * MTF = | DFT(LSF) |
 * 只保留前一半频率分量 (DFT 共轭对称性)
 */
void DFT(std::vector<double>& data, int size)
{
    if (static_cast<int>(data.size()) < size) {
        data.resize(size, 0.0);
    }

    // 准备输入矩阵 (1 x N, 64位浮点)
    cv::Mat inputMat;
    cv::Mat(1, size, CV_64F, data.data()).copyTo(inputMat);

    // 执行 DFT (输出为复数: 实部 + 虚部)
    cv::Mat complexOutput;
    cv::dft(inputMat, complexOutput, cv::DFT_COMPLEX_OUTPUT, 0);

    // 分离实部和虚部
    std::vector<cv::Mat> channels;
    cv::split(complexOutput, channels);

    // 计算幅值: sqrt(real² + imag²)
    cv::Mat magnitudeMat;
    cv::magnitude(channels[0], channels[1], magnitudeMat);

    // 只保留前一半频率分量
    int halfSize = (size / 2) + 1;
    if (static_cast<int>(data.size()) < halfSize) {
        data.resize(halfSize);
    }

    // 拷贝幅值数据
    std::memcpy(data.data(), magnitudeMat.data,
                static_cast<size_t>(halfSize) * sizeof(double));

    // 清空后半部分
    std::fill(data.begin() + halfSize, data.end(), 0.0);
}

/**
 * @brief 按斜边斜率裁剪图像行数 (减少边缘效应)
 * @param slope     斜边斜率
 * @param ImgHeight [in/out] 图像高度，被裁剪为 slope 的整数倍
 */
void ReduceRows(double slope, int* ImgHeight)
{
    double absSlope = std::fabs(slope);
    if (absSlope == 0) absSlope = 1.0;

    int cycles = static_cast<int>((*ImgHeight) / absSlope);
    if (cycles > 0) {
        *ImgHeight = static_cast<int>(absSlope * cycles);
    }
}

/**
 * @brief B-Spline 三次样条插值
 * @param xy 已知数据点集 (频率, MTF)
 * @param xx 待插值的 X 坐标数组 (目标频率)
 * @param yy [out] 插值结果 Y 坐标数组 (对应目标频率的 MTF 值)
 *
 * 用于在离散的 MTF 频率点之间插值，查询任意频率处的精确 MTF 值。
 * 使用自然边界条件 (二阶导数在两端为 0)。
 */
void BSpline(std::vector<cv::Point2f> xy, std::vector<float> xx, std::vector<float>& yy)
{
    yy.clear();
    int n = static_cast<int>(xy.size());

    // 至少需要 3 个点才能做三次样条
    if (n < 3) {
        // 退化为线性插值
        if (n == 2 && !xx.empty()) {
            double slope = (xy[1].y - xy[0].y) / (xy[1].x - xy[0].x);
            yy.push_back(static_cast<float>(xy[0].y + slope * (xx[0] - xy[0].x)));
        } else if (!xx.empty()) {
            yy.push_back(0.0f);
        }
        return;
    }

    // 初始化三对角矩阵
    cv::Mat a  = cv::Mat::zeros(n - 1, 1, CV_32FC1);
    cv::Mat b  = cv::Mat::zeros(n - 1, 1, CV_32FC1);
    cv::Mat d  = cv::Mat::zeros(n - 1, 1, CV_32FC1);
    cv::Mat dx = cv::Mat::zeros(n - 1, 1, CV_32FC1);
    cv::Mat dy = cv::Mat::zeros(n - 1, 1, CV_32FC1);

    for (int i = 0; i < n - 1; i++) {
        a.at<float>(i, 0) = xy[i].y;
        dx.at<float>(i, 0) = (xy[i + 1].x - xy[i].x);
        dy.at<float>(i, 0) = (xy[i + 1].y - xy[i].y);
    }

    // 构建三对角矩阵 A 和右侧向量 B
    cv::Mat A = cv::Mat::zeros(n, n, CV_32FC1);
    cv::Mat B = cv::Mat::zeros(n, 1, CV_32FC1);
    A.at<float>(0, 0) = 1;
    A.at<float>(n - 1, n - 1) = 1;

    for (int i = 1; i <= n - 2; i++) {
        A.at<float>(i, i - 1) = dx.at<float>(i - 1, 0);
        A.at<float>(i, i)     = 2 * (dx.at<float>(i - 1, 0) + dx.at<float>(i, 0));
        A.at<float>(i, i + 1) = dx.at<float>(i, 0);

        float dx_i   = dx.at<float>(i, 0);
        float dx_i_1 = dx.at<float>(i - 1, 0);
        if (dx_i == 0.0f || dx_i_1 == 0.0f) {
            B.at<float>(i, 0) = 0.0f;
        } else {
            B.at<float>(i, 0) = 3 * (dy.at<float>(i, 0) / dx_i
                                     - dy.at<float>(i - 1, 0) / dx_i_1);
        }
    }

    // 求解线性方程组 A * c = B
    cv::Mat c;
    try {
        c = A.inv() * B;
    } catch (const cv::Exception& e) {
        std::cerr << "BSpline 错误: 矩阵求逆失败。" << e.what() << std::endl;
        return;
    }

    // 计算插值系数
    for (int i = 0; i <= n - 2; i++) {
        float dx_i = dx.at<float>(i, 0);
        if (dx_i == 0.0f) {
            d.at<float>(i, 0) = 0.0f;
            b.at<float>(i, 0) = 0.0f;
        } else {
            d.at<float>(i, 0) = (c.at<float>(i + 1, 0) - c.at<float>(i, 0)) / (3 * dx_i);
            b.at<float>(i, 0) = dy.at<float>(i, 0) / dx_i
                               - dx_i * (2 * c.at<float>(i, 0) + c.at<float>(i + 1, 0)) / 3;
        }
    }

    // 对每个目标点进行插值
    for (size_t i = 0; i < xx.size(); i++) {
        int j = -1;
        // 定位 xx[i] 所在的区间
        for (int ii = 0; ii <= n - 2; ii++) {
            if (xx[i] >= xy[ii].x && xx[i] < xy[ii + 1].x) {
                j = ii;
                break;
            }
        }
        if (xx[i] == xy[n - 1].x) j = n - 2;  // 精确匹配终点
        if (j == -1) {
            // 超出范围: 使用最近边界
            j = (xx[i] < xy[0].x) ? 0 : n - 2;
        }

        float diff    = (xx[i] - xy[j].x);
        float diff_sq = diff * diff;
        float diff_cu = diff_sq * diff;

        float interpolated =
            a.at<float>(j, 0) +
            b.at<float>(j, 0) * diff +
            c.at<float>(j, 0) * diff_sq +
            d.at<float>(j, 0) * diff_cu;

        yy.push_back(interpolated);
    }
}

// ============================================================================
// 主入口: SFR 计算
// ============================================================================

int SFRCalculation(cv::Mat& ROI, double gamma, double PixelSize,
                    double TestFrequency, double& MTFresult,
                    std::string path, int debug)
{
    MTFresult = 0.0;

    if (ROI.empty()) {
        std::cerr << "SFR 错误: 输入 ROI 图像为空。" << std::endl;
        return 0;
    }

    int height = ROI.rows, width = ROI.cols;

    // ---------- 步骤 1: de-Gamma 线性化 ----------
    de_Gamma(ROI, gamma);

    // ---------- 步骤 2: 逐行质心定位 ----------
    double slope = 0, intercept = 0, CCoffset = 0;
    std::vector<double> y_shifts(height);

    std::vector<double> Cen_Shifts = CentroidFind(ROI, y_shifts, &CCoffset);
    if (Cen_Shifts.empty()) {
        std::cerr << "SFR 错误: 质心定位失败。" << std::endl;
        return 0;
    }

    // ---------- 步骤 3: 线性回归求斜边斜率 ----------
    SLR(Cen_Shifts, y_shifts, &intercept, &slope);

    // ---------- 步骤 4: 按斜率裁剪行数 ----------
    ReduceRows(slope, &height);

    if (std::isnan(slope) || slope == 0.0 || height == 0 || width == 0) {
        std::cerr << "SFR 错误: 斜率无效或裁剪后图像尺寸为 0。" << std::endl;
        return 0;
    }

    // 截取有效行
    cv::Mat ROITruncated = ROI.rowRange(0, height).clone();

    // ---------- 步骤 5: 超采样构建 ESF → LSF ----------
    CCoffset = CCoffset + 0.5 + intercept - static_cast<double>(width) / 2.0;
    int SamplingLen = width * OVER_SAMPLING_FACTOR;

    std::vector<double> LSF = OverSampling(ROITruncated, slope, CCoffset,
                                            height, width, &SamplingLen);
    if (SamplingLen == 0 || LSF.empty()) {
        std::cerr << "SFR 错误: 超采样 (OverSampling) 失败。" << std::endl;
        return 0;
    }

    // ---------- 步骤 6: Hamming 窗 ----------
    LSF = HammingWindows(LSF, SamplingLen);

    // ---------- 步骤 7: DFT / FFT ----------
    DFT(LSF, SamplingLen);

    // ---------- 步骤 8: 归一化 (DC 分量 = 1) ----------
    double dcValue = LSF[0];
    if (dcValue <= 0.0) {
        std::cerr << "SFR 错误: MTF 的 DC 分量 <= 0。" << std::endl;
        return 0;
    }

    for (int i = 0; i < SamplingLen; ++i) {
        LSF[i] /= dcValue;
    }

    // ---------- 步骤 9: 准备频率-MTF 数据点 ----------
    // 频率轴刻度: (i / width) * (1000 um/mm / PixelSize um)
    double freqScale = (1000.0 / PixelSize) / static_cast<double>(width);

    int numFreqs = (width / 2) + 1;  // 奈奎斯特频率以内
    if (numFreqs > static_cast<int>(LSF.size())) {
        numFreqs = static_cast<int>(LSF.size());
    }

    std::vector<cv::Point2f> mtfPoints(numFreqs);
    for (int i = 0; i < numFreqs; ++i) {
        mtfPoints[i].x = static_cast<float>(static_cast<double>(i) * freqScale);
        mtfPoints[i].y = static_cast<float>(LSF[i]);
    }

    // ---------- 步骤 10: B-Spline 插值查询目标频率 MTF ----------
    std::vector<float> targetFreq = { static_cast<float>(TestFrequency) };
    std::vector<float> interpolated;
    BSpline(mtfPoints, targetFreq, interpolated);

    if (interpolated.empty()) {
        std::cerr << "SFR 错误: B-Spline 插值失败。" << std::endl;
        return 0;
    }

    MTFresult = interpolated[0];

    // ---------- 调试输出: 保存 MTF 曲线到 CSV ----------
    if (debug) {
        std::fstream mtfFile(path, std::ios::out);
        mtfFile << "Frequency (lp/mm),SFR(MTF)\n";
        for (int i = 0; i < numFreqs; ++i) {
            mtfFile << mtfPoints[i].x << "," << mtfPoints[i].y << "\n";
        }
        mtfFile.close();
    }

    return 1;  // 计算成功
}
