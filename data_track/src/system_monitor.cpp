#include "system_monitor.h"

SystemMonitor::SystemMonitor()
    : cpu_monitor_(), network_monitor_(), memory_monitor_(),
      service_monitor_(), gsm_monitor_(), gps_monitor_()
{
}

void SystemMonitor::startMonitoring()
{
    cpu_monitor_.start();
    network_monitor_.start();
    memory_monitor_.start();
}

void SystemMonitor::stopMonitoring()
{
    cpu_monitor_.stop();
    network_monitor_.stop();
    memory_monitor_.stop();
}

// 初始化4G模块（可选）
bool SystemMonitor::open_4g(const std::string &gsm_port, int gsm_baudrate)
{
    return gsm_monitor_.open(gsm_port, gsm_baudrate);
}

// 初始化GPS模块（可选）
bool SystemMonitor::open_gps(const std::string &gps_port, int gps_baudrate)
{
    return gps_monitor_.open(gps_port, gps_baudrate);
}

bool SystemMonitor::getNetworkStats(double &rx_speed, double &tx_speed)
{
    return network_monitor_.getStats(rx_speed, tx_speed);
}

bool SystemMonitor::getCPUUsage(double &cpu_usage)
{
    return cpu_monitor_.getUsage(cpu_usage);
}

bool SystemMonitor::getCPUInfo(CPUMonitor::CPUInfo &info)
{
    return cpu_monitor_.getCPUInfo(info);
}

bool SystemMonitor::getMemoryInfo(double &total_mb, double &used_mb, double &free_mb, double &usage_percent)
{
    return memory_monitor_.getMemoryInfo(total_mb, used_mb, free_mb, usage_percent);
}

bool SystemMonitor::checkServiceStatus(const std::string &service_name)
{
    return service_monitor_.checkServiceStatus(service_name);
}

void SystemMonitor::getGsmInfo(std::string &signal, std::string &simStatus,
                                std::string &network, std::string &moduleInfo,
                                int timeout_ms)
{
    gsm_monitor_.getAllInfo(signal, simStatus, network, moduleInfo, timeout_ms);
}

bool SystemMonitor::getGpsGGAInfo(std::string &time, float &latitude, char &lat_dir,
                                  float &longitude, char &lon_dir, int &quality,
                                  int &satellites, float &altitude)
{
    return gps_monitor_.getGGAInfo(time, latitude, lat_dir, longitude, lon_dir,
                                    quality, satellites, altitude);
}

bool SystemMonitor::getGpsRMCInfo(std::string &time, std::string &date,
                                  float &latitude, char &lat_dir,
                                  float &longitude, char &lon_dir,
                                  float &speed_knots, float &course)
{
    return gps_monitor_.getRMCInfo(time, date, latitude, lat_dir, longitude, lon_dir,
                                    speed_knots, course);
}

bool SystemMonitor::getGpsVTGInfo(float &course_true, float &speed_knots, float &speed_kmh)
{
    return gps_monitor_.getVTGInfo(course_true, speed_knots, speed_kmh);
}