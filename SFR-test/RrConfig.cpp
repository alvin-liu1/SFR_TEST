/**
 * @file    RrConfig.cpp
 * @brief   INI 配置文件解析器实现
 * @details 实现了 [Section] Key=Value 格式 INI 配置文件的读取和解析。
 *          支持 # 行注释、前后空白自动去除。底层使用 std::map 存储。
 */

#include "RrConfig.h"
#include <fstream>
#include <stdlib.h>
#include <iostream>

namespace rr {

/**
 * @brief 判断字符是否为空白 (空格或制表符)
 */
bool RrConfig::IsSpace(char c)
{
    return (c == ' ' || c == '\t');
}

/**
 * @brief 判断字符是否为注释起始符 (仅支持 #)
 */
bool RrConfig::IsCommentChar(char c)
{
    return (c == '#');
}

/**
 * @brief 去除字符串两端的空白字符 (原地修改)
 */
void RrConfig::Trim(std::string& str)
{
    if (str.empty()) return;

    // 找左端第一个非空白字符
    int start = 0;
    for (; start < static_cast<int>(str.size()); ++start) {
        if (!IsSpace(str[start])) break;
    }

    if (start == static_cast<int>(str.size())) {
        str = "";
        return;
    }

    // 找右端第一个非空白字符
    int end = static_cast<int>(str.size()) - 1;
    for (; end >= 0; --end) {
        if (!IsSpace(str[end])) break;
    }

    str = str.substr(start, end - start + 1);
}

/**
 * @brief 分析 INI 文件的一行
 * @param line    输入行文本
 * @param section [out] 若该行是节名，返回节名称
 * @param key     [out] 若该行是键值对，返回键名
 * @param value   [out] 若该行是键值对，返回值
 * @return true=有效行, false=空白行或注释行
 *
 * 支持格式:
 *   [SectionName]   → 更新当前节
 *   Key = Value     → 返回键值对
 *   # comment       → 忽略
 */
bool RrConfig::AnalyseLine(const std::string& line, std::string& section,
                            std::string& key, std::string& value)
{
    if (line.empty()) return false;

    int start = 0, end = static_cast<int>(line.size()) - 1, pos;

    // 处理行内注释 (截断 # 之后的内容)
    if ((pos = static_cast<int>(line.find("#"))) != -1) {
        if (pos == 0) return false;  // 整行都是注释
        end = pos - 1;
    }

    // 截取有效内容并去除两端空白
    std::string trimmed = line.substr(start, end - start + 1);
    Trim(trimmed);
    if (trimmed.empty()) return false;

    // 检查是否为节名 [Section]
    int s_start, s_end;
    if (((s_start = static_cast<int>(trimmed.find("["))) != -1) &&
        ((s_end   = static_cast<int>(trimmed.find("]"))) != -1))
    {
        section = trimmed.substr(s_start + 1, s_end - s_start - 1);
        Trim(section);
        return true;
    }

    // 检查是否为键值对 Key=Value
    if ((pos = static_cast<int>(trimmed.find('='))) == -1) {
        return false;
    }

    key   = trimmed.substr(0, pos);
    value = trimmed.substr(pos + 1);

    Trim(key);
    if (key.empty()) return false;

    Trim(value);

    // 去除可能存在的换行符 \r \n
    if ((pos = static_cast<int>(value.find("\r"))) != -1) value.replace(pos, 1, "");
    if ((pos = static_cast<int>(value.find("\n"))) != -1) value.replace(pos, 1, "");

    return true;
}

/**
 * @brief 读取并解析整个 INI 配置文件
 * @param filename INI 文件完整路径
 * @return true=读取成功, false=文件打开失败
 */
bool RrConfig::ReadConfig(const std::string& filename)
{
    settings_.clear();

    std::ifstream infile(filename.c_str());
    if (!infile) {
        std::cerr << "RrConfig: 无法打开文件 " << filename << std::endl;
        return false;
    }

    std::string line, key, value;
    std::string current_section = "GLOBAL";  // 默认节

    while (std::getline(infile, line)) {
        key.clear();
        value.clear();

        if (AnalyseLine(line, current_section, key, value)) {
            // 只有当是键值对时 (key 非空) 才存储
            if (!key.empty()) {
                settings_[current_section][key] = value;
            }
        }
    }

    infile.close();
    return true;
}

/**
 * @brief 读取字符串值
 */
std::string RrConfig::ReadString(const char* section, const char* item,
                                  const char* default_value)
{
    auto secIt = settings_.find(section);
    if (secIt == settings_.end()) return default_value;

    auto itemIt = secIt->second.find(item);
    if (itemIt == secIt->second.end()) return default_value;

    return itemIt->second;
}

/**
 * @brief 读取整数值
 */
int RrConfig::ReadInt(const char* section, const char* item, const int& default_value)
{
    std::string val = ReadString(section, item, "");
    if (val.empty()) return default_value;
    return std::atoi(val.c_str());
}

/**
 * @brief 读取浮点值
 */
float RrConfig::ReadFloat(const char* section, const char* item, const float& default_value)
{
    std::string val = ReadString(section, item, "");
    if (val.empty()) return default_value;
    return static_cast<float>(std::atof(val.c_str()));
}

} // namespace rr
