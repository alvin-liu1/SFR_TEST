/**
 * @file    sfr.h
 * @brief   SFR (Spatial Frequency Response) 空间频率响应计算模块
 * @details 基于 ISO 12233 斜边法标准，计算相机模组/镜头的 MTF (Modulation Transfer Function)。
 *          核心流程：de-Gamma 线性化 → 质心定位 → 线性回归求斜边斜率 → 超采样构建 ESF
 *          → 微分得 LSF → Hamming 窗 → DFT/FFT 得 MTF 曲线 → B-Spline 插值查目标频率 MTF 值。
 */

#pragma once
#include <opencv2/opencv.hpp>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * @brief SFR 计算主函数
 * @param ROI            [in/out] 包含斜边边缘的感兴趣区域图像 (CV_8UC1)，会被 de-gamma 原地修改
 * @param gamma          图像的 Gamma 值 (通常 1.0 或 2.2)
 * @param PixelSize      传感器像素尺寸 (单位: um/pixel)
 * @param TestFrequency  目标测试空间频率 (单位: lp/mm)
 * @param MTFresult      [out] 在 TestFrequency 处插值得到的 MTF 值 (0.0 ~ 1.0)
 * @param path           调试输出路径 (保存 MTF 曲线 CSV)
 * @param debug          是否启用调试模式 (1=输出中间图像和 CSV, 0=关闭)
 * @return 1=计算成功, 0=计算失败
 */
int SFRCalculation(cv::Mat& ROI, double gamma, double PixelSize, double TestFrequency,
                   double& MTFresult, std::string path, int debug);
