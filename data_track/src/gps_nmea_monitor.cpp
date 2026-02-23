#include "gps_nmea_monitor.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <sys/select.h>
#include <sys/time.h>

GPSNMEAMonitor::GPSNMEAMonitor()
    : serial_fd_(-1), device_("/dev/ttyUSB1"), baudrate_(115200),
      initialized_(false), running_(false)
{
}

GPSNMEAMonitor::~GPSNMEAMonitor()
{
    stop();
    if (serial_fd_ >= 0)
    {
        close(serial_fd_);
    }
}

bool GPSNMEAMonitor::open(const std::string &gps_port, int gps_baudrate)
{
    if (initialized_)
    {
        std::cerr << "Warning: GPS NMEA monitor already initialized" << std::endl;
        return true;
    }

    device_ = gps_port;
    baudrate_ = gps_baudrate;

    // 以读写和非阻塞模式打开串口
    serial_fd_ = ::open(device_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (serial_fd_ < 0)
    {
        std::cerr << "Failed to open GPS serial port: " << device_ << " - " << strerror(errno) << std::endl;
        return false;
    }

    // 配置串口
    if (!configureSerialPort())
    {
        close(serial_fd_);
        serial_fd_ = -1;
        return false;
    }

    initialized_ = true;
    std::cout << "GPS NMEA monitor initialized on " << device_ << " baudrate: " << baudrate_ << std::endl;

    // 启动数据接收线程
    start();

    return true;
}

void GPSNMEAMonitor::start()
{
    if (!initialized_ || running_)
    {
        return;
    }

    running_ = true;
    receiver_thread_ = std::thread(&GPSNMEAMonitor::receiverLoop, this);
}

void GPSNMEAMonitor::stop()
{
    if (!running_)
    {
        return;
    }

    running_ = false;

    if (receiver_thread_.joinable())
    {
        receiver_thread_.join();
    }
}

bool GPSNMEAMonitor::configureSerialPort()
{
    struct termios tty;

    if (tcgetattr(serial_fd_, &tty) != 0)
    {
        std::cerr << "Failed to get serial port attributes: " << strerror(errno) << std::endl;
        return false;
    }

    // 设置波特率
    speed_t speed = baudrateToConstant(baudrate_);
    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    // 控制模式设置
    tty.c_cflag &= ~PARENB;          // 无奇偶校验
    tty.c_cflag &= ~CSTOPB;          // 1个停止位
    tty.c_cflag &= ~CSIZE;           // 清除数据位掩码
    tty.c_cflag |= CS8;              // 8个数据位
    tty.c_cflag &= ~CRTSCTS;         // 无硬件流控
    tty.c_cflag |= (CREAD | CLOCAL); // 启用接收，忽略调制解调器状态

    // 本地模式设置
    tty.c_lflag &= ~ICANON; // 非规范模式
    tty.c_lflag &= ~ECHO;   // 禁用回显
    tty.c_lflag &= ~ECHOE;  // 禁用擦除
    tty.c_lflag &= ~ECHONL; // 禁用换行回显
    tty.c_lflag &= ~ISIG;   // 禁用信号字符

    // 输入模式设置
    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // 禁用软件流控
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    // 输出模式设置
    tty.c_oflag &= ~OPOST; // 原始输出
    tty.c_oflag &= ~ONLCR; // 不将换行转换为回车换行

    // 超时设置：立即返回
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(serial_fd_, TCSANOW, &tty) != 0)
    {
        std::cerr << "Failed to set serial port attributes: " << strerror(errno) << std::endl;
        return false;
    }

    // 清空缓冲区
    tcflush(serial_fd_, TCIOFLUSH);

    return true;
}

int GPSNMEAMonitor::readData(char *buffer, int size)
{
    if (serial_fd_ < 0)
    {
        return -1;
    }

    struct timeval tv;
    fd_set readfds;

    // 设置超时
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    FD_ZERO(&readfds);
    FD_SET(serial_fd_, &readfds);

    int ret = select(serial_fd_ + 1, &readfds, NULL, NULL, &tv);

    if (ret > 0 && FD_ISSET(serial_fd_, &readfds))
    {
        return read(serial_fd_, buffer, size);
    }

    return -1;
}

void GPSNMEAMonitor::receiverLoop()
{
    char buffer[1024];
    std::string line_buffer;

    while (running_)
    {
        int bytes_read = readData(buffer, sizeof(buffer) - 1);

        if (bytes_read > 0)
        {
            buffer[bytes_read] = '\0';

            std::lock_guard<std::mutex> lock(data_mutex_);

            // 处理接收到的数据
            for (int i = 0; i < bytes_read; i++)
            {
                char c = buffer[i];

                if (c == '\r')
                {
                    continue; // 忽略回车
                }
                else if (c == '\n')
                {
                    if (!line_buffer.empty())
                    {
                        raw_data_ = line_buffer;
                        parseNMEA(line_buffer);
                        line_buffer.clear();
                    }
                }
                else
                {
                    line_buffer += c;
                }
            }
        }
        else
        {
            // 短暂休眠避免CPU占用过高
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

void GPSNMEAMonitor::parseNMEA(const std::string &nmea)
{
    if (nmea.empty() || nmea[0] != '$')
    {
        return;
    }

    // 提取语句类型 (例如: $GPGGA, $GPRMC, $GPVTG, $GPGSA, $GPGSV)
    if (nmea.length() >= 6)
    {
        std::string sentence_type = nmea.substr(0, 6);

        if (sentence_type == "$GPGGA" || sentence_type == "$BDGGA")
        {
            latest_gga_ = nmea;
        }
        else if (sentence_type == "$GPRMC" || sentence_type == "$BDRMC")
        {
            latest_rmc_ = nmea;
        }
        else if (sentence_type == "$GPVTG" || sentence_type == "$BDVTG")
        {
            latest_vtg_ = nmea;
        }
        else if (sentence_type == "$GPGSA" || sentence_type == "$BDGSA")
        {
            latest_gsa_ = nmea;
        }
        else if (sentence_type == "$GPGSV" || sentence_type == "$BDGSV")
        {
            latest_gsv_ = nmea;
        }
    }
}

/**
 * 获取GGA语句信息（全球定位数据）
 * @param time [输出] UTC时间，格式：HHMMSS
 * @param latitude [输出] 纬度，格式：DDMM.MMMM
 * @param lat_dir [输出] 纬度方向，N=北，S=南
 * @param longitude [输出] 经度，格式：DDDMM.MMMM
 * @param lon_dir [输出] 经度方向，E=东，W=西
 * @param quality [输出] 定位质量，0=无效，1=GPS，2=DGPS
 * @param satellites [输出] 使用卫星数量
 * @param altitude [输出] 海拔高度（米）
 * @return 成功返回true，失败返回false
 */
bool GPSNMEAMonitor::getGGAInfo(std::string &time, float &latitude, char &lat_dir,
                                float &longitude, char &lon_dir, int &quality,
                                int &satellites, float &altitude)
{
    std::lock_guard<std::mutex> lock(data_mutex_);
    return parseGGA(latest_gga_, time, latitude, lat_dir, longitude, lon_dir,
                    quality, satellites, altitude);
}

/**
 * 获取RMC语句信息（推荐最小定位数据）
 * @param time [输出] UTC时间，格式：HHMMSS
 * @param date [输出] UTC日期，格式：DDMMYY
 * @param latitude [输出] 纬度，格式：DDMM.MMMM
 * @param lat_dir [输出] 纬度方向，N=北，S=南
 * @param longitude [输出] 经度，格式：DDDMM.MMMM
 * @param lon_dir [输出] 经度方向，E=东，W=西
 * @param speed_knots [输出] 地面速度（节）
 * @param course [输出] 航向角（度）
 * @return 成功返回true，失败返回false
 */
bool GPSNMEAMonitor::getRMCInfo(std::string &time, std::string &date,
                                float &latitude, char &lat_dir,
                                float &longitude, char &lon_dir,
                                float &speed_knots, float &course)
{
    std::lock_guard<std::mutex> lock(data_mutex_);
    return parseRMC(latest_rmc_, time, date, latitude, lat_dir, longitude, lon_dir,
                    speed_knots, course);
}

/**
 * 获取VTG语句信息（地面速度信息）
 * @param course_true [输出] 真北向航向（度）
 * @param speed_knots [输出] 地面速度（节）
 * @param speed_kmh [输出] 地面速度（公里/小时）
 * @return 成功返回true，失败返回false
 */
bool GPSNMEAMonitor::getVTGInfo(float &course_true, float &speed_knots, float &speed_kmh)
{
    std::lock_guard<std::mutex> lock(data_mutex_);
    return parseVTG(latest_vtg_, course_true, speed_knots, speed_kmh);
}

/**
 * 获取GSA语句信息（卫星数据）
 * @param mode [输出] 定位模式，A=自动，M=手动
 * @param fix_mode [输出] 定位类型，1=无效，2=2D，3=3D
 * @param pdop [输出] 位置精度因子
 * @param hdop [输出] 水平精度因子
 * @param vdop [输出] 垂直精度因子
 * @return 成功返回true，失败返回false
 */
bool GPSNMEAMonitor::getGSAInfo(char &mode, int &fix_mode,
                                float &pdop, float &hdop, float &vdop)
{
    std::lock_guard<std::mutex> lock(data_mutex_);
    return parseGSA(latest_gsa_, mode, fix_mode, pdop, hdop, vdop);
}

/**
 * 获取GSV语句信息（卫星视界信息）
 * @param total_msgs [输出] 总消息数
 * @param msg_num [输出] 当前消息序号
 * @param total_sats [输出] 视界内卫星总数
 * @return 成功返回true，失败返回false
 */
bool GPSNMEAMonitor::getGSVInfo(int &total_msgs, int &msg_num, int &total_sats)
{
    std::lock_guard<std::mutex> lock(data_mutex_);
    return parseGSV(latest_gsv_, total_msgs, msg_num, total_sats);
}

std::string GPSNMEAMonitor::getRawData()
{
    std::lock_guard<std::mutex> lock(data_mutex_);
    return raw_data_;
}

bool GPSNMEAMonitor::parseGGA(const std::string &gga, std::string &time, float &latitude, char &lat_dir,
                              float &longitude, char &lon_dir, int &quality,
                              int &satellites, float &altitude)
{
    if (gga.empty())
    {
        return false;
    }

    // GGA格式: $GPGGA,HHMMSS,DDMM.MMMM,N,DDDMM.MMMM,E,0,00,0.0,M,,,,0000*00
    std::vector<std::string> fields;
    std::stringstream ss(gga);
    std::string field;

    while (std::getline(ss, field, ','))
    {
        fields.push_back(field);
    }

    if (fields.size() < 10)
    {
        return false;
    }

    time = fields[1];
    latitude = fields[2].empty() ? 0.0f : std::stof(fields[2]);
    lat_dir = fields[3].empty() ? 'N' : fields[3][0];
    longitude = fields[4].empty() ? 0.0f : std::stof(fields[4]);
    lon_dir = fields[5].empty() ? 'E' : fields[5][0];
    quality = fields[6].empty() ? 0 : std::stoi(fields[6]);
    satellites = fields[7].empty() ? 0 : std::stoi(fields[7]);
    altitude = fields[9].empty() ? 0.0f : std::stof(fields[9]);

    return true;
}

bool GPSNMEAMonitor::parseRMC(const std::string &rmc, std::string &time, std::string &date,
                              float &latitude, char &lat_dir,
                              float &longitude, char &lon_dir,
                              float &speed_knots, float &course)
{
    if (rmc.empty())
    {
        return false;
    }

    // RMC格式: $GPRMC,HHMMSS,A,DDMM.MMMM,N,DDDMM.MMMM,E,000.0,000.0,DDMMYY,,,A*00
    std::vector<std::string> fields;
    std::stringstream ss(rmc);
    std::string field;

    while (std::getline(ss, field, ','))
    {
        fields.push_back(field);
    }

    if (fields.size() < 12)
    {
        return false;
    }

    time = fields[1];
    latitude = fields[3].empty() ? 0.0f : std::stof(fields[3]);
    lat_dir = fields[4].empty() ? 'N' : fields[4][0];
    longitude = fields[5].empty() ? 0.0f : std::stof(fields[5]);
    lon_dir = fields[6].empty() ? 'E' : fields[6][0];
    speed_knots = fields[7].empty() ? 0.0f : std::stof(fields[7]);
    course = fields[8].empty() ? 0.0f : std::stof(fields[8]);
    date = fields[9];

    return true;
}

bool GPSNMEAMonitor::parseVTG(const std::string &vtg, float &course_true, float &speed_knots, float &speed_kmh)
{
    if (vtg.empty())
    {
        return false;
    }

    // VTG格式: $GPVTG,000.0,T,,M,000.0,N,000.0,K*00
    std::vector<std::string> fields;
    std::stringstream ss(vtg);
    std::string field;

    while (std::getline(ss, field, ','))
    {
        fields.push_back(field);
    }

    if (fields.size() < 10)
    {
        return false;
    }

    course_true = fields[1].empty() ? 0.0f : std::stof(fields[1]);
    speed_knots = fields[5].empty() ? 0.0f : std::stof(fields[5]);
    speed_kmh = fields[7].empty() ? 0.0f : std::stof(fields[7]);

    return true;
}

bool GPSNMEAMonitor::parseGSA(const std::string &gsa, char &mode, int &fix_mode,
                              float &pdop, float &hdop, float &vdop)
{
    if (gsa.empty())
    {
        return false;
    }

    // GSA格式: $GPGSA,A,1,,,,,,,,,,,,,,0.0,0.0,0.0*00
    std::vector<std::string> fields;
    std::stringstream ss(gsa);
    std::string field;

    while (std::getline(ss, field, ','))
    {
        fields.push_back(field);
    }

    if (fields.size() < 18)
    {
        return false;
    }

    mode = fields[1].empty() ? 'A' : fields[1][0];
    fix_mode = fields[2].empty() ? 0 : std::stoi(fields[2]);
    pdop = fields[15].empty() ? 0.0f : std::stof(fields[15]);
    hdop = fields[16].empty() ? 0.0f : std::stof(fields[16]);
    vdop = fields[17].empty() ? 0.0f : std::stof(fields[17]);

    return true;
}

bool GPSNMEAMonitor::parseGSV(const std::string &gsv, int &total_msgs, int &msg_num, int &total_sats)
{
    if (gsv.empty())
    {
        return false;
    }

    // GSV格式: $GPGSV,1,1,00*00
    std::vector<std::string> fields;
    std::stringstream ss(gsv);
    std::string field;

    while (std::getline(ss, field, ','))
    {
        fields.push_back(field);
    }

    if (fields.size() < 4)
    {
        return false;
    }

    total_msgs = fields[1].empty() ? 0 : std::stoi(fields[1]);
    msg_num = fields[2].empty() ? 0 : std::stoi(fields[2]);
    total_sats = fields[3].empty() ? 0 : std::stoi(fields[3]);

    return true;
}

int GPSNMEAMonitor::baudrateToConstant(int baudrate)
{
    switch (baudrate)
    {
    case 9600:
        return B9600;
    case 19200:
        return B19200;
    case 38400:
        return B38400;
    case 57600:
        return B57600;
    case 115200:
        return B115200;
    case 230400:
        return B230400;
    case 460800:
        return B460800;
    case 500000:
        return B500000;
    case 576000:
        return B576000;
    case 921600:
        return B921600;
    case 1000000:
        return B1000000;
    case 1152000:
        return B1152000;
    case 1500000:
        return B1500000;
    case 2000000:
        return B2000000;
    case 2500000:
        return B2500000;
    case 3000000:
        return B3000000;
    case 3500000:
        return B3500000;
    case 4000000:
        return B4000000;
    default:
        return B9600;
    }
}
