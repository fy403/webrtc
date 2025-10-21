#include "webrtc_publisher.h"
#include <iostream>
#include <string>

int main(int argc, char *argv[])
{
    try
    {
        Cmdline params(argc, argv);

        // 使用命令行参数或生成随机 ID
        std::string client_id = params.clientId(); // 使用新的client_id参数
        if (client_id.empty())
        {
            client_id = randomId(4);
            std::cout << "Generated client ID: " << client_id << std::endl;
        }
        else
        {
            std::cout << "Using specified client ID: " << client_id << std::endl;
        }

        localId = client_id;

        WebRTCPublisher publisher(client_id, params, params.inputDevice());
        publisher.start();

        std::cout << "WebRTC Publisher started. Press Enter to exit..." << std::endl;
        std::cin.get();

        publisher.stop();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}