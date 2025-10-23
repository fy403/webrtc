#ifndef FOURG_TTY_H
#define FOURG_TTY_H

#include <string>
#include <vector>

class FourGTty
{
public:
    // Constructor and destructor
    FourGTty();
    ~FourGTty();

    // Serial operations
    bool open(const std::string &device = "/dev/ttyACM0", int baudrate = 115200);
    void close();
    bool isOpen() const;

    // AT command operations
    bool sendCommand(const std::string &command);
    std::vector<std::string> sendCommandWithResponse(const std::string &command, int timeout_ms = 3000);

    // Specific AT command wrappers
    bool testAT(int timeout_ms = 1000);
    std::string getSignalQuality(int timeout_ms = 3000);
    std::string getSimStatus(int timeout_ms = 3000);
    std::string getNetworkRegistration(int timeout_ms = 3000);
    std::string getModuleInfo(int timeout_ms = 3000);

private:
    int serial_fd_;
    std::string device_;
    int baudrate_;
    bool debug_ = false;

    bool configureSerialPort();
    int readResponse(std::vector<std::string> &response, int timeout_ms);
    static int baudrateToConstant(int baudrate);
};

#endif // FOURG_TTY_H