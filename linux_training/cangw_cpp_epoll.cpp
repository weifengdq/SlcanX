#include <iostream>
#include <vector>
#include <string>
#include <functional>
#include <algorithm>
#include <optional>
#include <variant>
#include <array>
#include <system_error>
#include <memory>
#include <cstring>

#include <unistd.h>
#include <fcntl.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <linux/can.h>
#include <linux/can/raw.h>

// C++17 Functional Programming Style Implementation
// - Uses RAII for resource management
// - Uses std::variant for type-safe frame handling
// - Uses lambdas and std::function for logic
// - No Boost, no ASIO, just standard C++17 and Linux syscalls

// RAII wrapper for file descriptors
class FileDescriptor {
    int fd_ = -1;
public:
    explicit FileDescriptor(int fd) : fd_(fd) {}
    ~FileDescriptor() { if (fd_ >= 0) close(fd_); }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    FileDescriptor(FileDescriptor&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
    int get() const { return fd_; }
    operator int() const { return fd_; }
};

// Error handling helper
void check_error(int result, const std::string& msg) {
    if (result < 0) {
        throw std::system_error(errno, std::generic_category(), msg);
    }
}

// Socket creation function (Factory)
auto create_can_socket = [](const std::string& ifname) -> FileDescriptor {
    int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    check_error(s, "socket");
    FileDescriptor fd(s);

    int enable_canfd = 1;
    check_error(setsockopt(s, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable_canfd, sizeof(enable_canfd)), "setsockopt CAN_RAW_FD_FRAMES");

    int loopback = 0;
    check_error(setsockopt(s, SOL_CAN_RAW, CAN_RAW_LOOPBACK, &loopback, sizeof(loopback)), "setsockopt CAN_RAW_LOOPBACK");

    struct ifreq ifr;
    std::strncpy(ifr.ifr_name, ifname.c_str(), IFNAMSIZ - 1);
    check_error(ioctl(s, SIOCGIFINDEX, &ifr), "ioctl SIOCGIFINDEX");

    struct sockaddr_can addr = {};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    check_error(bind(s, (struct sockaddr*)&addr, sizeof(addr)), "bind");
    return fd;
};

// Frame transformation logic (Pure function)
// Takes a variant of either Classical CAN or CAN FD, returns a CAN FD frame with BRS
auto transform_frame = [](const std::variant<can_frame, canfd_frame>& input) -> canfd_frame {
    return std::visit([](auto&& arg) -> canfd_frame {
        using T = std::decay_t<decltype(arg)>;
        canfd_frame out_frame = {};
        
        if constexpr (std::is_same_v<T, can_frame>) {
            // Convert Classical CAN to CAN FD
            out_frame.can_id = arg.can_id & ~CAN_RTR_FLAG; // Clear RTR
            out_frame.len = arg.can_dlc;
            std::copy(std::begin(arg.data), std::begin(arg.data) + arg.can_dlc, std::begin(out_frame.data));
        } else if constexpr (std::is_same_v<T, canfd_frame>) {
            // Already CAN FD
            out_frame = arg;
        }
        
        // Apply BRS flag
        out_frame.flags |= CANFD_BRS;
        return out_frame;
    }, input);
};

int main() {
    try {
        // Cleanup existing rules
        std::system("sudo cangw -F > /dev/null 2>&1");
        std::system("pkill -f \"candump -L can0 can1 can2\" > /dev/null 2>&1");

        // Initialize resources
        std::vector<std::string> rx_ifaces = {"can0", "can1", "can2"};
        std::vector<FileDescriptor> rx_sockets;
        rx_sockets.reserve(rx_ifaces.size());
        
        // Functional: Map interface names to sockets
        for (const auto& iface : rx_ifaces) {
            rx_sockets.push_back(create_can_socket(iface));
        }
        
        auto tx_socket = create_can_socket("can3");

        // Setup Epoll
        FileDescriptor epoll_fd(epoll_create1(0));
        check_error(epoll_fd, "epoll_create1");

        // Register sockets
        for (const auto& sock : rx_sockets) {
            struct epoll_event ev = {};
            ev.events = EPOLLIN;
            ev.data.fd = sock.get();
            check_error(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock.get(), &ev), "epoll_ctl");
        }

        std::cout << "cangw_cpp: Forwarding can0, can1, can2 -> can3 (CAN FD BRS) [C++17]" << std::endl;

        // Event Loop
        std::array<struct epoll_event, 10> events;
        while (true) {
            int nfds = epoll_wait(epoll_fd, events.data(), events.size(), -1);
            check_error(nfds, "epoll_wait");

            // Process events
            for (int i = 0; i < nfds; ++i) {
                int fd = events[i].data.fd;
                
                // Read raw bytes
                struct canfd_frame frame_buf;
                int nbytes = read(fd, &frame_buf, sizeof(frame_buf));
                
                if (nbytes < 0) continue;

                // Lift raw bytes into typed variant
                std::optional<std::variant<can_frame, canfd_frame>> input_frame;
                
                if (nbytes == sizeof(can_frame)) {
                    can_frame cf;
                    std::memcpy(&cf, &frame_buf, sizeof(can_frame));
                    input_frame = cf;
                } else if (nbytes == sizeof(canfd_frame)) {
                    input_frame = frame_buf;
                }

                // Pipeline: Input -> Transform -> Send
                if (input_frame) {
                    auto out_frame = transform_frame(*input_frame);
                    write(tx_socket, &out_frame, sizeof(out_frame));
                }
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

// g++ -std=c++17 -o cangw_cpp_epoll cangw_cpp_epoll.cpp