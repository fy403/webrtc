#ifndef GPS_NMEA_MONITOR_H
#define GPS_NMEA_MONITOR_H

#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include <condition_variable>

class GPSNMEAMonitor
{
public:
    GPSNMEAMonitor();
    ~GPSNMEAMonitor();

    // 初始化GPS模块
    bool open(const std::string &gps_port = "/dev/ttyUSB1", int gps_baudrate = 115200);

    // 获取GGA语句信息 (定位信息)
    bool getGGAInfo(std::string &time, float &latitude, char &lat_dir,
                    float &longitude, char &lon_dir, int &quality,
                    int &satellites, float &altitude);

    // 获取RMC语句信息 (推荐最小定位信息)
    bool getRMCInfo(std::string &time, std::string &date,
                    float &latitude, char &lat_dir,
                    float &longitude, char &lon_dir,
                    float &speed_knots, float &course);

    // 获取VTG语句信息 (地面速度信息)
    bool getVTGInfo(float &course_true, float &speed_knots, float &speed_kmh);

    // 获取GSA语句信息 (卫星数据)
    bool getGSAInfo(char &mode, int &fix_mode,
                    float &pdop, float &hdop, float &vdop);

    // 获取GSV语句信息 (卫星视界信息)
    bool getGSVInfo(int &total_msgs, int &msg_num, int &total_sats);

    // 获取原始NMEA数据
    std::string getRawData();

    // 检查是否已初始化
    bool isInitialized() const { return initialized_; }

    // 启动/停止数据接收
    void start();
    void stop();

private:
    // 串口操作
    int serial_fd_;
    std::string device_;
    int baudrate_;
    bool initialized_;

    // 数据接收线程
    std::thread receiver_thread_;
    std::atomic<bool> running_;
    std::mutex data_mutex_;

    // NMEA数据缓存
    std::string latest_gga_;
    std::string latest_rmc_;
    std::string latest_vtg_;
    std::string latest_gsa_;
    std::string latest_gsv_;
    std::string raw_data_;

    // 内部函数
    void receiverLoop();
    bool configureSerialPort();
    int readData(char *buffer, int size);
    void parseNMEA(const std::string &nmea);
    bool parseGGA(const std::string &gga, std::string &time, float &latitude, char &lat_dir,
                  float &longitude, char &lon_dir, int &quality,
                  int &satellites, float &altitude);
    bool parseRMC(const std::string &rmc, std::string &time, std::string &date,
                  float &latitude, char &lat_dir,
                  float &longitude, char &lon_dir,
                  float &speed_knots, float &course);
    bool parseVTG(const std::string &vtg, float &course_true, float &speed_knots, float &speed_kmh);
    bool parseGSA(const std::string &gsa, char &mode, int &fix_mode,
                  float &pdop, float &hdop, float &vdop);
    bool parseGSV(const std::string &gsv, int &total_msgs, int &msg_num, int &total_sats);
    static int baudrateToConstant(int baudrate);
};

#endif // GPS_NMEA_MONITOR_H
