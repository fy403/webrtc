#include "rc_client.h"
#include "parse_cl.h"
#include <iostream>
#include <string>
#include <signal.h>

extern RCClient *global_client;
extern int rtc_main(int argc, char **argv);

int main(int argc, char *argv[])
{
    try
    {
        rtc_main(argc, argv);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}