/**
 * @file    RrConfig.h
 * @brief   INI 格式配置文件解析器
 * @details 轻量级 INI 文件读取类，支持 [Section] Key=Value 格式和 # 注释行。
 *          提供 ReadString / ReadInt / ReadFloat 三种类型安全访问接口。
 */

#ifndef RR_CONFIG_H_
#define RR_CONFIG_H_
#include <string>
#include <map>

namespace rr {

/**
 * @class RrConfig
 * @brief INI 配置文件读取器
 *
 * 使用方式:
 * @code
 *   rr::RrConfig config;
 *   config.ReadConfig("config.ini");
 *   std::string name = config.ReadString("PROJECT", "Name", "default");
 *   int areaNum = config.ReadInt("P223_MZ", "AreaNum", 5);
 * @endcode
 */
class RrConfig
{
public:
    RrConfig()  {}
    ~RrConfig() {}

    /**
     * @brief 读取并解析 INI 配置文件
     * @param filename INI 文件的完整路径
     * @return true=读取成功, false=文件不存在或读取失败
     */
    bool ReadConfig(const std::string& filename);

    /**
     * @brief 读取字符串类型配置值
     * @param section       INI 节名称 (如 "PROJECT")
     * @param item          INI 键名称 (如 "Name")
     * @param default_value 未找到时返回的默认值
     * @return 找到的字符串值或默认值
     */
    std::string ReadString(const char* section, const char* item, const char* default_value);

    /**
     * @brief 读取整数类型配置值
     * @param section       INI 节名称
     * @param item          INI 键名称
     * @param default_value 未找到时返回的默认值
     * @return 找到的整数值或默认值
     */
    int ReadInt(const char* section, const char* item, const int& default_value);

    /**
     * @brief 读取浮点类型配置值
     * @param section       INI 节名称
     * @param item          INI 键名称
     * @param default_value 未找到时返回的默认值
     * @return 找到的浮点值或默认值
     */
    float ReadFloat(const char* section, const char* item, const float& default_value);

private:
    // --- 内部解析辅助函数 ---
    bool IsSpace(char c);
    bool IsCommentChar(char c);
    void Trim(std::string& str);
    bool AnalyseLine(const std::string& line, std::string& section, std::string& key, std::string& value);

    // --- 数据存储 ---
    // 结构: [SectionName] -> ([Key] -> Value)
    std::map<std::string, std::map<std::string, std::string>> settings_;
};

} // namespace rr
#endif
