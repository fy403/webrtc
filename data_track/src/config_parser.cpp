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

/*----------------------------------------------------------------------------
**
** Config::trim()
**
** Helper function to trim whitespace from start/end of string
**
**--------------------------------------------------------------------------*/

std::string Config::trim(const std::string& str)
{
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return "";

    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

/*----------------------------------------------------------------------------
**
** Config::Config()
**
** Constructor - initializes with configuration file path
**
**--------------------------------------------------------------------------*/

Config::Config(const std::string& configFile)
    : _configFile(configFile)
{
}

/*----------------------------------------------------------------------------
**
** Config::load()
**
** Load configuration from file
** Parses lines in key=value format, ignores # comments
**
**--------------------------------------------------------------------------*/

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

        // 剥离行内注释（# 后面的内容）
        size_t commentPos = line.find('#');
        if (commentPos != std::string::npos) {
            line = trim(line.substr(0, commentPos));
        }

        // Skip empty lines
        if (line.empty())
            continue;

        // Skip comments
        if (line[0] == '#')
            continue;

        // Find key=value separator
        size_t separatorPos = line.find('=');
        if (separatorPos == std::string::npos)
        {
            std::cerr << "Warning: Invalid format at line " << lineNumber
                      << " (no '=' found): " << line << std::endl;
            continue;
        }

        // Extract key and value
        std::string key = trim(line.substr(0, separatorPos));
        std::string value = trim(line.substr(separatorPos + 1));

        if (key.empty())
        {
            std::cerr << "Warning: Empty key at line " << lineNumber << std::endl;
            continue;
        }

        // Store in config map
        _configMap[key] = value;
    }

    file.close();
}

/*----------------------------------------------------------------------------
**
** Config::get()
**
** Get configuration value as string
**
**--------------------------------------------------------------------------*/

std::string Config::get(const std::string& key, const std::string& defaultValue) const
{
    auto it = _configMap.find(key);
    if (it != _configMap.end())
    {
        return it->second;
    }
    return defaultValue;
}

/*----------------------------------------------------------------------------
**
** Config::getAsInt()
**
** Get configuration value as integer
**
**--------------------------------------------------------------------------*/

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

/*----------------------------------------------------------------------------
**
** Config::getAsBool()
**
** Get configuration value as boolean
** Accepts: true/false, 1/0, yes/no (case-insensitive)
**
**--------------------------------------------------------------------------*/

bool Config::getAsBool(const std::string& key, bool defaultValue) const
{
    auto it = _configMap.find(key);
    if (it != _configMap.end())
    {
        std::string value = it->second;
        // Convert to lowercase for comparison
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

/*----------------------------------------------------------------------------
**
** Config::getAsFloat()
**
** Get configuration value as float
**
**--------------------------------------------------------------------------*/

float Config::getAsFloat(const std::string& key, float defaultValue) const
{
    auto it = _configMap.find(key);
    if (it != _configMap.end())
    {
        try
        {
            return std::stof(it->second);
        }
        catch (const std::exception& e)
        {
            std::cerr << "Warning: Invalid float value for key '" << key
                      << "': " << it->second << ", using default: " << defaultValue
                      << std::endl;
            return defaultValue;
        }
    }
    return defaultValue;
}

/*----------------------------------------------------------------------------
**
** Config::has()
**
** Check if a key exists in configuration
**
**--------------------------------------------------------------------------*/

bool Config::has(const std::string& key) const
{
    return _configMap.find(key) != _configMap.end();
}

/*----------------------------------------------------------------------------
**
** Config::display()
**
** Display all loaded configuration values
**
**--------------------------------------------------------------------------*/

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
