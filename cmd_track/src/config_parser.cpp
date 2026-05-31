/******************************************************************************
**
** config_parser.cpp
**
** Configuration file parser class implementation
**
******************************************************************************/

#include "config_parser.h"
#include <fstream>
#include <iostream>
#include <algorithm>

std::string Config::trim(const std::string& str)
{
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return "";

    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

Config::Config(const std::string& configFile)
    : _configFile(configFile)
{
}

void Config::load()
{
    std::ifstream file(_configFile);
    if (!file.is_open())
    {
        throw std::runtime_error("Failed to open configuration file: " + _configFile);
    }

    std::string line;
    int lineNumber = 0;

    while (std::getline(file, line))
    {
        lineNumber++;
        line = trim(line);

        size_t commentPos = line.find('#');
        if (commentPos != std::string::npos) {
            line = trim(line.substr(0, commentPos));
        }

        if (line.empty())
            continue;

        if (line[0] == '#')
            continue;

        size_t separatorPos = line.find('=');
        if (separatorPos == std::string::npos)
        {
            std::cerr << "Warning: Invalid format at line " << lineNumber
                      << " (no '=' found): " << line << std::endl;
            continue;
        }

        std::string key = trim(line.substr(0, separatorPos));
        std::string value = trim(line.substr(separatorPos + 1));

        if (key.empty())
        {
            std::cerr << "Warning: Empty key at line " << lineNumber << std::endl;
            continue;
        }

        _configMap[key] = value;
    }

    file.close();
}

std::string Config::get(const std::string& key, const std::string& defaultValue) const
{
    auto it = _configMap.find(key);
    if (it != _configMap.end())
    {
        return it->second;
    }
    return defaultValue;
}

int Config::getAsInt(const std::string& key, int defaultValue) const
{
    auto it = _configMap.find(key);
    if (it != _configMap.end())
    {
        try
        {
            return std::stoi(it->second);
        }
        catch (const std::exception& e)
        {
            std::cerr << "Warning: Invalid integer value for key '" << key
                      << "': " << it->second << ", using default: " << defaultValue
                      << std::endl;
            return defaultValue;
        }
    }
    return defaultValue;
}

bool Config::getAsBool(const std::string& key, bool defaultValue) const
{
    auto it = _configMap.find(key);
    if (it != _configMap.end())
    {
        std::string value = it->second;
        std::transform(value.begin(), value.end(), value.begin(), ::tolower);

        if (value == "true" || value == "1" || value == "yes")
            return true;
        if (value == "false" || value == "0" || value == "no")
            return false;

        std::cerr << "Warning: Invalid boolean value for key '" << key
                  << "': " << it->second << ", using default: "
                  << (defaultValue ? "true" : "false") << std::endl;
        return defaultValue;
    }
    return defaultValue;
}

bool Config::has(const std::string& key) const
{
    return _configMap.find(key) != _configMap.end();
}

void Config::display() const
{
    std::cout << "========================================" << std::endl;
    std::cout << "Configuration loaded from: " << _configFile << std::endl;
    std::cout << "========================================" << std::endl;

    for (const auto& pair : _configMap)
    {
        std::cout << pair.first << " = " << pair.second << std::endl;
    }

    std::cout << "========================================" << std::endl;
}
