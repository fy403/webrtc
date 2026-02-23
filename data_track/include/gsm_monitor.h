#ifndef GSM_MONITOR_H
#define GSM_MONITOR_H

#include <string>
#include "4g_tty.h"

class GSMMonitor
{
public:
    GSMMonitor();
    ~GSMMonitor();

    // 初始化4G模块
    bool open(const std::string &gsm_port = "/dev/ttyACM0", int gsm_baudrate = 115200);

    // 获取信号质量
    std::string getSignalQuality(int timeout_ms = 3000);

    // 获取SIM卡状态
    std::string getSimStatus(int timeout_ms = 3000);

    // 获取网络注册状态
    std::string getNetworkRegistration(int timeout_ms = 3000);

    // 获取模块信息
    std::string getModuleInfo(int timeout_ms = 3000);

    // 一次性获取所有4G模块信息
    void getAllInfo(std::string &signal, std::string &simStatus,
                    std::string &network, std::string &moduleInfo,
                    int timeout_ms = 3000);

    // 检查是否已初始化
    bool isInitialized() const { return initialized_; }

private:
    FourGTty gsm_;
    bool initialized_;
};

#endif // GSM_MONITOR_H
