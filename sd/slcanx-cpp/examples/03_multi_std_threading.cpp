#include "slcanx.hpp"
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>

using namespace slcanx;

void tx_worker(Slcanx* slcan, int ch) {
    int cnt = 0;
    while(true) {
        std::vector<uint8_t> data = {(uint8_t)ch, (uint8_t)cnt};
        CanFrame frame = CanFrame::new_std(0x200 + ch, data);
        slcan->send(ch, frame);
        cnt++;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

int main(int argc, char** argv) {
    std::string port = "COM3";
    if (argc > 1) port = argv[1];

    Slcanx slcan(port);

    slcan.set_rx_callback([](uint8_t ch, const CanFrame& frame) {
        std::cout << "[Ch" << (int)ch << "] Rx ID=" << std::hex << frame.id << std::dec << std::endl;
    });

    std::vector<std::thread> threads;

    for(int i=0; i<4; i++) {
        std::cout << "Opening Channel " << i << std::endl;
        slcan.close_channel(i);
        slcan.set_bitrate(i, 500000);
        slcan.open_channel(i);
        
        threads.emplace_back(tx_worker, &slcan, i);
    }

    std::cout << "All channels running..." << std::endl;
    
    for(auto& t : threads) {
        t.join();
    }

    return 0;
}
