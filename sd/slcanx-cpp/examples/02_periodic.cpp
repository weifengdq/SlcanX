#include "slcanx.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace slcanx;

int main(int argc, char** argv) {
    std::string port = "COM3";
    if (argc > 1) port = argv[1];

    Slcanx slcan(port);
    slcan.close_channel(0);
    slcan.set_bitrate(0, 500000);
    slcan.open_channel(0);

    slcan.set_rx_callback([](uint8_t ch, const CanFrame& frame) {
        std::cout << "Rx Ch" << (int)ch << ": ID=" << std::hex << frame.id << std::dec << std::endl;
    });

    std::cout << "Starting periodic send..." << std::endl;
    
    int cnt = 0;
    while(true) {
        std::vector<uint8_t> data = {0, 1, 2, (uint8_t)cnt};
        CanFrame frame = CanFrame::new_std(0x100, data);
        slcan.send(0, frame);
        
        cnt++;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
