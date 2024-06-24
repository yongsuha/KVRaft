#include "./include/mprpcconfigg.h"
#include <iostream>
#include <string>

void MprpcConfig::LoadConfigFile(const char* config_file)
{
    FILE* fp = fopen(config_file, "r");
    if (fp == nullptr)
    {
        std::cout << config_file << " is not exists!!" << std::endl;
        exit(EXIT_FAILURE);
    }
    while (!feof(fp))
    {
        char buf[512] = {0};
        fgets(buf, 512, fp);
        // 去掉多余空格
        std::string read_buf(buf);
        Trim(read_buf);
        // 判断注释
        if (read_buf[0] == '#' || read_buf.empty())
        {
            continue;
        }
        // 解析配置项
        int index = read_buf.find('=');
        if (index == -1)
        {
            continue;
        }
        std::string key;
        std::string value;
        key = read_buf.substr(0, index);
        Trim(key);
        int endIndex = read_buf.find('\n', index);
        value = read_buf.substr(index + 1, endIndex - index - 1);
        Trim(value);
        _configMap.insert({key, value});
    }
    fclose(fp);
}

std::string MprpcConfig::Load(const std::string& key)
{
    auto it = _configMap.find(key);
    if (it == _configMap.end())
    {
        return "";
    }
    return it->second;
}

void MprpcConfig::Trim(std::string& src_buf)
{
    int index = src_buf.find_first_not_of(' ');
    if (index != -1)
    {
        src_buf = src_buf.substr(index, src_buf.size() - index);
    }
    index = src_buf.find_last_not_of(' ');
    if (index != -1)
    {
        src_buf = src_buf.substr(0, index + 1);
    }
}