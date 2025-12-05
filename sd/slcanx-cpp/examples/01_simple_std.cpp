#include "slcanx.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace slcanx;

int main(int argc, char** argv) {
    std::string port = "COM3";
    if (argc > 1) port = argv[1];

    std::cout << "Opening " << port << " Channel 0 @ 500000bps" << std::endl;

    Slcanx slcan(port);
    
    slcan.set_rx_callback([](uint8_t ch, const CanFrame& frame) {
        if (ch == 0) {
            std::cout << "Rx: ID=" << std::hex << frame.id << " DLC=" << frame.data.size() << std::dec << std::endl;
        }
    });

    slcan.close_channel(0);
    slcan.set_bitrate(0, 500000);
    slcan.open_channel(0);

    std::vector<uint8_t> data = {0x11, 0x22, 0x33, 0x44};
    CanFrame frame = CanFrame::new_std(0x123, data);
    
    std::cout << "Sending frame..." << std::endl;
    slcan.send(0, frame);

    std::cout << "Listening... (Ctrl+C to exit)" << std::endl;
    while(true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
