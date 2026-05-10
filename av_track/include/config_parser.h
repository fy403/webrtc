#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

#include <string>
#include <unordered_map>
#include <stdexcept>

class Config
{
private:
    std::unordered_map<std::string, std::string> _configMap;
    std::string _configFile;
    std::string trim(const std::string& str);

public:
    explicit Config(const std::string& configFile);
    ~Config() = default;

    void load();
    std::string get(const std::string& key, const std::string& defaultValue = "") const;
    int getAsInt(const std::string& key, int defaultValue = 0) const;
    bool getAsBool(const std::string& key, bool defaultValue = false) const;
    float getAsFloat(const std::string& key, float defaultValue = 0.0f) const;
    bool has(const std::string& key) const;
    std::string getConfigFile() const { return _configFile; }
    void display() const;
};

#endif // CONFIG_PARSER_H
