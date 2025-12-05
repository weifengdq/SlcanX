#include "slcanx.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <windows.h>
#include <chrono>

namespace slcanx {

// ================= CanFrame Implementation =================

CanFrame CanFrame::new_std(uint32_t id, const std::vector<uint8_t>& data) {
    CanFrame f;
    f.id = id;
    f.data = data;
    f.ext = false;
    return f;
}

CanFrame CanFrame::new_ext(uint32_t id, const std::vector<uint8_t>& data) {
    CanFrame f;
    f.id = id;
    f.data = data;
    f.ext = true;
    return f;
}

CanFrame CanFrame::new_fd(uint32_t id, const std::vector<uint8_t>& data, bool brs) {
    CanFrame f;
    f.id = id;
    f.data = data;
    f.ext = false; // Assuming std ID for simplicity, user can change
    f.fd = true;
    f.brs = brs;
    return f;
}

static uint8_t len_to_dlc(size_t len) {
    if (len <= 8) return (uint8_t)len;
    if (len <= 12) return 9;
    if (len <= 16) return 10;
    if (len <= 20) return 11;
    if (len <= 24) return 12;
    if (len <= 32) return 13;
    if (len <= 48) return 14;
    return 15;
}

static size_t dlc_to_len(uint8_t dlc) {
    static const size_t map[] = {0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64};
    if (dlc < 16) return map[dlc];
    return 64;
}

// ================= SerialPort Implementation (Windows) =================

class Slcanx::SerialPort {
public:
    HANDLE hComm;

    SerialPort(const std::string& port, uint32_t baudrate) {
        std::string portName = "\\\\.\\" + port;
        hComm = CreateFileA(portName.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0, NULL, OPEN_EXISTING, 0, NULL);

        if (hComm == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("Failed to open serial port");
        }

        DCB dcbSerialParams = { 0 };
        dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
        if (!GetCommState(hComm, &dcbSerialParams)) {
            CloseHandle(hComm);
            throw std::runtime_error("Failed to get comm state");
        }

        dcbSerialParams.BaudRate = baudrate;
        dcbSerialParams.ByteSize = 8;
        dcbSerialParams.StopBits = ONESTOPBIT;
        dcbSerialParams.Parity = NOPARITY;
        dcbSerialParams.fDtrControl = DTR_CONTROL_ENABLE; // Important for CDC

        if (!SetCommState(hComm, &dcbSerialParams)) {
            CloseHandle(hComm);
            throw std::runtime_error("Failed to set comm state");
        }

        COMMTIMEOUTS timeouts = { 0 };
        timeouts.ReadIntervalTimeout = 1;
        timeouts.ReadTotalTimeoutConstant = 1;
        timeouts.ReadTotalTimeoutMultiplier = 1;
        timeouts.WriteTotalTimeoutConstant = 10;
        timeouts.WriteTotalTimeoutMultiplier = 1;
        SetCommTimeouts(hComm, &timeouts);
    }

    ~SerialPort() {
        if (hComm != INVALID_HANDLE_VALUE) {
            CloseHandle(hComm);
        }
    }

    int read(uint8_t* buf, int max_len) {
        DWORD bytesRead;
        if (ReadFile(hComm, buf, max_len, &bytesRead, NULL)) {
            return bytesRead;
        }
        return -1;
    }

    bool write(const uint8_t* buf, int len) {
        DWORD bytesWritten;
        return WriteFile(hComm, buf, len, &bytesWritten, NULL) != 0;
    }
};

// ================= Slcanx Implementation =================

Slcanx::Slcanx(const std::string& port, uint32_t baudrate, uint32_t group_window_us)
    : group_window_us_(group_window_us) {
    serial_ = std::make_unique<SerialPort>(port, baudrate);
    
    read_thread_ = std::thread(&Slcanx::read_loop, this);
    write_thread_ = std::thread(&Slcanx::write_loop, this);
}

Slcanx::~Slcanx() {
    running_ = false;
    write_cv_.notify_all();
    if (read_thread_.joinable()) read_thread_.join();
    if (write_thread_.joinable()) write_thread_.join();
}

void Slcanx::set_rx_callback(RxCallback cb) {
    std::lock_guard<std::mutex> lock(rx_mutex_);
    rx_callback_ = cb;
}

bool Slcanx::send_cmd(uint8_t channel, const std::string& cmd) {
    std::string line = std::to_string(channel) + cmd + "\r";
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(line.c_str());
        write_buffer_.insert(write_buffer_.end(), ptr, ptr + line.size());
    }
    write_cv_.notify_one();
    return true;
}

bool Slcanx::open_channel(uint8_t channel) {
    return send_cmd(channel, "O");
}

bool Slcanx::close_channel(uint8_t channel) {
    return send_cmd(channel, "C");
}

bool Slcanx::set_bitrate(uint8_t channel, uint32_t bitrate) {
    // Simple mapping
    int idx = -1;
    switch(bitrate) {
        case 10000: idx = 0; break;
        case 20000: idx = 1; break;
        case 50000: idx = 2; break;
        case 100000: idx = 3; break;
        case 125000: idx = 4; break;
        case 250000: idx = 5; break;
        case 500000: idx = 6; break;
        case 800000: idx = 7; break;
        case 1000000: idx = 8; break;
    }
    if (idx >= 0) return send_cmd(channel, "S" + std::to_string(idx));
    return send_cmd(channel, "y" + std::to_string(bitrate));
}

bool Slcanx::set_data_bitrate(uint8_t channel, uint32_t bitrate) {
    if (bitrate % 1000000 == 0) {
        int idx = bitrate / 1000000;
        if (idx >= 1 && idx <= 15) {
            return send_cmd(channel, "Y" + std::to_string(idx));
        }
    }
    return false;
}

bool Slcanx::set_sample_point(uint8_t channel, double nominal_percent, double data_percent) {
    if (nominal_percent > 0) {
        send_cmd(channel, "p" + std::to_string((int)(nominal_percent * 10)));
    }
    if (data_percent > 0) {
        send_cmd(channel, "P" + std::to_string((int)(data_percent * 10)));
    }
    return true;
}

bool Slcanx::send(uint8_t channel, const CanFrame& frame) {
    std::stringstream ss;
    ss << (int)channel;
    
    char cmd;
    if (frame.fd) {
        if (frame.brs) cmd = frame.ext ? 'B' : 'b';
        else cmd = frame.ext ? 'D' : 'd';
    } else {
        if (frame.rtr) cmd = frame.ext ? 'R' : 'r';
        else cmd = frame.ext ? 'T' : 't';
    }
    ss << cmd;

    ss << std::hex << std::uppercase;
    if (frame.ext) ss << std::setw(8) << std::setfill('0') << frame.id;
    else ss << std::setw(3) << std::setfill('0') << frame.id;

    ss << (int)len_to_dlc(frame.data.size());

    if (!frame.rtr) {
        for (uint8_t b : frame.data) {
            ss << std::setw(2) << std::setfill('0') << (int)b;
        }
    }
    ss << "\r";

    std::string line = ss.str();
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(line.c_str());
        write_buffer_.insert(write_buffer_.end(), ptr, ptr + line.size());
    }
    write_cv_.notify_one();
    return true;
}

void Slcanx::write_loop() {
    while (running_) {
        std::vector<uint8_t> chunk;
        {
            std::unique_lock<std::mutex> lock(write_mutex_);
            write_cv_.wait(lock, [this] { return !write_buffer_.empty() || !running_; });
            
            if (!running_) break;

            // Grouping logic:
            // If we have data, wait a bit to see if more comes, unless buffer is already large
            if (write_buffer_.size() < 1024 && group_window_us_ > 0) {
                lock.unlock();
                std::this_thread::sleep_for(std::chrono::microseconds(group_window_us_));
                lock.lock();
            }

            // Swap buffer
            chunk.swap(write_buffer_);
        }

        if (!chunk.empty()) {
            serial_->write(chunk.data(), chunk.size());
        }
    }
}

void Slcanx::read_loop() {
    uint8_t buf[1024];
    std::string line_buf;

    while (running_) {
        int n = serial_->read(buf, sizeof(buf));
        if (n > 0) {
            for (int i = 0; i < n; ++i) {
                if (buf[i] == '\r') {
                    parse_line(line_buf);
                    line_buf.clear();
                } else {
                    line_buf += (char)buf[i];
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void Slcanx::parse_line(const std::string& line) {
    if (line.empty()) return;

    uint8_t channel = 0;
    int idx = 0;
    if (line[0] >= '0' && line[0] <= '3') {
        channel = line[0] - '0';
        idx++;
    }

    if (idx >= line.size()) return;
    char cmd = line[idx];

    if (cmd == 't' || cmd == 'T' || cmd == 'r' || cmd == 'R' || 
        cmd == 'd' || cmd == 'D' || cmd == 'b' || cmd == 'B') {
        
        bool is_ext = (cmd == 'T' || cmd == 'R' || cmd == 'D' || cmd == 'B');
        bool is_fd = (cmd == 'd' || cmd == 'D' || cmd == 'b' || cmd == 'B');
        bool is_brs = (cmd == 'b' || cmd == 'B');
        bool is_rtr = (cmd == 'r' || cmd == 'R');

        int id_len = is_ext ? 8 : 3;
        if (line.size() < idx + 1 + id_len + 1) return;

        try {
            std::string id_str = line.substr(idx + 1, id_len);
            uint32_t id = std::stoul(id_str, nullptr, 16);

            char dlc_char = line[idx + 1 + id_len];
            uint8_t dlc_val = std::stoul(std::string(1, dlc_char), nullptr, 16);
            size_t len = dlc_to_len(dlc_val);

            std::vector<uint8_t> data;
            if (!is_rtr) {
                std::string data_str = line.substr(idx + 1 + id_len + 1);
                for (size_t i = 0; i < data_str.size(); i += 2) {
                    if (i + 1 < data_str.size()) {
                        data.push_back((uint8_t)std::stoul(data_str.substr(i, 2), nullptr, 16));
                    }
                }
            }

            CanFrame frame;
            frame.id = id;
            frame.data = data;
            frame.ext = is_ext;
            frame.rtr = is_rtr;
            frame.fd = is_fd;
            frame.brs = is_brs;

            std::lock_guard<std::mutex> lock(rx_mutex_);
            if (rx_callback_) {
                rx_callback_(channel, frame);
            }

        } catch (...) {
            // Parse error
        }
    }
}

} // namespace slcanx
