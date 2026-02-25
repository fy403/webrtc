#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H

#include <string>
#include <chrono>
#include <memory>
#include <array>
#include <vector>
#include <map>
#include "cpu_monitor.h"
#include "network_monitor.h"
#include "memory_monitor.h"
#include "service_monitor.h"
#include "gsm_monitor.h"
#include "gps_nmea_monitor.h"

class SystemMonitor
{
public:
    SystemMonitor();

    // 启动所有监控线程
    void startMonitoring();

    // 停止所有监控线程
    void stopMonitoring();

    // 初始化4G模块（可选）
    bool open_4g(const std::string &gsm_port = "/dev/ttyACM0", int gsm_baudrate = 115200);

    // 初始化GPS模块（可选）
    bool open_gps(const std::string &gps_port = "/dev/ttyUSB1", int gps_baudrate = 115200);

    // Network statistics
    bool getNetworkStats(double &rx_speed, double &tx_speed);

    // CPU usage
    bool getCPUUsage(double &cpu_usage);

    // CPU detailed info
    bool getCPUInfo(CPUMonitor::CPUInfo &info);

    // Memory info
    bool getMemoryInfo(double &total_mb, double &used_mb, double &free_mb, double &usage_percent);

    // Service status
    bool checkServiceStatus(const std::string &service_name);

    // 一次性获取所有GSM/4G模块信息（需要先调用 open_4g）
    void getGsmInfo(std::string &signal, std::string &simStatus,
        std::string &network, std::string &moduleInfo,
        int timeout_ms = 3000);

    // GPS NMEA信息获取函数
    bool getGpsGGAInfo(std::string &time, float &latitude, char &lat_dir,
                       float &longitude, char &lon_dir, int &quality,
                       int &satellites, float &altitude);
    bool getGpsRMCInfo(std::string &time, std::string &date,
                       float &latitude, char &lat_dir,
                       float &longitude, char &lon_dir,
                       float &speed_knots, float &course);
    bool getGpsVTGInfo(float &course_true, float &speed_knots, float &speed_kmh);

    // 检查4G模块是否已初始化
    bool is4gInitialized() const { return gsm_monitor_.isInitialized(); }

    // 检查GPS模块是否已初始化
    bool isGpsInitialized() const { return gps_monitor_.isInitialized(); }

private:
    // 子Monitor类实例
    CPUMonitor cpu_monitor_;
    NetworkMonitor network_monitor_;
    MemoryMonitor memory_monitor_;
    ServiceMonitor service_monitor_;
    GSMMonitor gsm_monitor_;
    GPSNMEAMonitor gps_monitor_;
};

#endif // SYSTEM_MONITOR_H