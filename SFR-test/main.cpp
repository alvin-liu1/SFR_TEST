/**
 * @file    main.cpp
 * @brief   应用程序主入口
 * @details SFR 清晰度测试工具的启动点，负责:
 *          1. 关闭 OpenCV 日志输出
 *          2. 获取工作目录
 *          3. 加载 INI 配置文件
 *          4. 创建 SfrTestRunner 并执行测试流程
 *          5. 捕获并报告所有异常
 *
 *          命令行: SFR-test.exe [无参数，所有配置从 Config/sfr_config.ini 读取]
 */

#include "RrConfig.h"
#include "SfrTestRunner.h"
#include <iostream>
#include <opencv2/core/utils/logger.hpp>  // 关闭 OpenCV 日志
#include <direct.h>      // _getcwd
#include <string>
#include <algorithm>     // std::replace
#include <exception>

int main(int argc, char* argv[])
{
    // 关闭 OpenCV 控制台日志，保持输出整洁
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_SILENT);

    // 获取当前工作目录
    char buff[200];
    _getcwd(buff, 200);
    std::string originPath = buff;
    std::replace(originPath.begin(), originPath.end(), '\\', '/');

    try {
        // 加载配置文件
        rr::RrConfig config;
        std::string configPath = originPath + "/Config/sfr_config.ini";

        if (!config.ReadConfig(configPath)) {
            std::cerr << "错误: 无法读取配置文件: " << configPath << std::endl;
            std::cerr << "请确保 'Config/sfr_config.ini' 文件存在。" << std::endl;
            system("pause");
            return -1;
        }

        // 创建测试运行器并执行
        SfrTestRunner runner(config, originPath);
        runner.run();

    } catch (const std::exception& e) {
        std::cerr << "发生严重错误: " << e.what() << std::endl;
        system("pause");
        return -1;
    } catch (...) {
        std::cerr << "发生未知异常。" << std::endl;
        system("pause");
        return -1;
    }

    std::cout << "程序执行完毕。" << std::endl;
    cv::waitKey(10000);  // 保持结果窗口可见
    return 0;
}
