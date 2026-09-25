#include "host/host_service.h"

#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    srd::host::HostConfig config;
    config.password = argc > 1 ? argv[1] : "change-me";
    config.port = argc > 2
        ? static_cast<std::uint16_t>(std::stoi(argv[2]))
        : static_cast<std::uint16_t>(45900);
    config.fps = argc > 3
        ? static_cast<unsigned int>(std::stoi(argv[3]))
        : 30u;
    config.jpegQuality = argc > 4
        ? static_cast<float>(std::stoi(argv[4])) / 100.0f
        : 0.90f;
    config.hostId = "console-host";

    try {
        srd::host::HostService host;
        host.set_status_callback([](const std::wstring& text) {
            std::wcout << L"[Host] " << text << L"\n";
        });

        host.start(config);

        std::cout << "SimpleRemoteHost Native\n";
        std::cout << "Press ENTER to stop.\n";
        std::cin.get();

        host.stop();
        return 0;
    }
    catch (const std::exception& ex) {
        std::cerr << "Host error: " << ex.what() << "\n";
        return 1;
    }
}
