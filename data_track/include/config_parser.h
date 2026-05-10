/******************************************************************************
**
** config_parser.h
**
** Configuration file parser class
** Supports simple key=value format with # comments
**
******************************************************************************/

#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

#include <string>
#include <unordered_map>
#include <stdexcept>

/*----------------------------------------------------------------------------
**
** class Config
**
** Configuration file parser and manager
**
** Supports:
**   - key=value format
**   - # comments
**   - Type conversion (string, int, bool)
**   - Default values for missing keys
**
**--------------------------------------------------------------------------*/

class Config
{
private:
    std::unordered_map<std::string, std::string> _configMap;
    std::string _configFile;

    // Helper function to trim whitespace from start/end
    std::string trim(const std::string& str);

public:
    /* Constructor and destructor */
    explicit Config(const std::string& configFile);
    ~Config() = default;

    /* Load configuration from file */
    void load();

    /* Get configuration value as string */
    std::string get(const std::string& key, const std::string& defaultValue = "") const;

    /* Get configuration value as integer */
    int getAsInt(const std::string& key, int defaultValue = 0) const;

    /* Get configuration value as boolean */
    bool getAsBool(const std::string& key, bool defaultValue = false) const;

    /* Check if a key exists */
    bool has(const std::string& key) const;

    /* Get the configuration file path */
    std::string getConfigFile() const { return _configFile; }

    /* Display all loaded configuration */
    void display() const;
};

#endif // CONFIG_PARSER_H
