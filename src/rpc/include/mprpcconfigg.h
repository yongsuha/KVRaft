#pragma once
#include <string>
#include <unordered_map>

// 负责框架读取配置文件
class MprpcConfig
{
public:
    void LoadConfigFile(const char* config_file);
    std::string Load(const std::string& key);
private:
    void Trim(std::string& src_buf);
private:
    std::unordered_map<std::string, std::string> _configMap;
};