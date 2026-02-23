#include "../include/gsm_monitor.h"
#include <iostream>

GSMMonitor::GSMMonitor() : initialized_(false) {}
GSMMonitor::~GSMMonitor() {}

bool GSMMonitor::open(const std::string &gsm_port, int gsm_baudrate)
{
    if (initialized_)
    {
        std::cerr << "Warning: 4G module already initialized" << std::endl;
        return true;
    }

    if (gsm_.open(gsm_port, gsm_baudrate))
    {
        initialized_ = true;
        return true;
    }
    else
    {
        std::cerr << "Failed to initialize 4G module on " << gsm_port << std::endl;
        return false;
    }
}

std::string GSMMonitor::getSignalQuality(int timeout_ms)
{
    return gsm_.getSignalQuality(timeout_ms);
}

std::string GSMMonitor::getSimStatus(int timeout_ms)
{
    return gsm_.getSimStatus(timeout_ms);
}

std::string GSMMonitor::getNetworkRegistration(int timeout_ms)
{
    return gsm_.getNetworkRegistration(timeout_ms);
}

std::string GSMMonitor::getModuleInfo(int timeout_ms)
{
    return gsm_.getModuleInfo(timeout_ms);
}

void GSMMonitor::getAllInfo(std::string &signal, std::string &simStatus,
                            std::string &network, std::string &moduleInfo,
                            int timeout_ms)
{
    signal = gsm_.getSignalQuality(timeout_ms);
    simStatus = gsm_.getSimStatus(timeout_ms);
    network = gsm_.getNetworkRegistration(timeout_ms);
    moduleInfo = gsm_.getModuleInfo(timeout_ms);
}
