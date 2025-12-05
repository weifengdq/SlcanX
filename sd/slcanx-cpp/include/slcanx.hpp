#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>

namespace slcanx {

struct CanFrame {
    uint32_t id;
    std::vector<uint8_t> data;
    bool ext = false;
    bool rtr = false;
    bool fd = false;
    bool brs = false;

    static CanFrame new_std(uint32_t id, const std::vector<uint8_t>& data);
    static CanFrame new_ext(uint32_t id, const std::vector<uint8_t>& data);
    static CanFrame new_fd(uint32_t id, const std::vector<uint8_t>& data, bool brs = false);
};

class Slcanx {
public:
    using RxCallback = std::function<void(uint8_t channel, const CanFrame&)>;

    Slcanx(const std::string& port, uint32_t baudrate = 115200, uint32_t group_window_us = 125);
    ~Slcanx();

    // Open/Close specific channel
    bool open_channel(uint8_t channel);
    bool close_channel(uint8_t channel);

    // Configuration
    bool set_bitrate(uint8_t channel, uint32_t bitrate);
    bool set_data_bitrate(uint8_t channel, uint32_t bitrate);
    bool set_sample_point(uint8_t channel, double nominal_percent, double data_percent);
    bool send_cmd(uint8_t channel, const std::string& cmd);

    // Sending
    bool send(uint8_t channel, const CanFrame& frame);

    // Receiving
    // Register a callback for received frames. 
    // Note: Callback is called from the internal read thread.
    void set_rx_callback(RxCallback cb);

private:
    class SerialPort; // Forward declaration of internal helper

    void read_loop();
    void write_loop();
    void parse_line(const std::string& line);

    std::unique_ptr<SerialPort> serial_;
    std::atomic<bool> running_{true};
    uint32_t group_window_us_;

    // Read Thread
    std::thread read_thread_;
    RxCallback rx_callback_;
    std::mutex rx_mutex_;

    // Write Thread
    std::thread write_thread_;
    std::mutex write_mutex_;
    std::condition_variable write_cv_;
    std::vector<uint8_t> write_buffer_; // Pending data to be written
};

} // namespace slcanx
