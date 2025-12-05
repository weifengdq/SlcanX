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
    
    // Custom timing
    // Arb: Pre 4, Seg1 31, Seg2 8, SJW 8 -> a80_4_31_8_8_0
    // Data: Pre 2, Seg1 15, Seg2 4, SJW 4, TDC on -> A80_2_15_4_4_1
    slcan.send_cmd(0, "a80_4_31_8_8_0");
    slcan.send_cmd(0, "A80_2_15_4_4_1");
    
    slcan.open_channel(0);

    slcan.set_rx_callback([](uint8_t ch, const CanFrame& frame) {
        std::cout << "Rx Custom: ID=" << std::hex << frame.id << std::dec << std::endl;
    });

    std::vector<uint8_t> data(8, 0xCC);
    CanFrame frame = CanFrame::new_fd(0x600, data, true);
    
    std::cout << "Sending custom timing frame..." << std::endl;
    slcan.send(0, frame);

    while(true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
