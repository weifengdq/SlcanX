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
    
    // Arb 500K, Data 2M
    slcan.set_bitrate(0, 500000);
    slcan.set_data_bitrate(0, 2000000);
    slcan.set_sample_point(0, 80.0, 80.0);
    
    slcan.open_channel(0);

    slcan.set_rx_callback([](uint8_t ch, const CanFrame& frame) {
        if (frame.fd) {
            std::cout << "Rx FD: ID=" << std::hex << frame.id << " Len=" << frame.data.size() << std::dec << std::endl;
        }
    });

    std::vector<uint8_t> data(64, 0xAA);
    CanFrame frame = CanFrame::new_fd(0x123, data, true); // BRS
    
    std::cout << "Sending FD frame..." << std::endl;
    slcan.send(0, frame);

    while(true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
