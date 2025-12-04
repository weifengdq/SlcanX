/*
 * cangw_cpp_asio.cpp
 * 
 * 使用 C++17、函数式编程风格和 Standalone Asio 实现的 CAN 网关。
 * 
 * 编译:
 * g++ -std=c++17 -I/path/to/asio/include -o cangw_cpp_asio cangw_cpp_asio.cpp -lpthread
 */

#define ASIO_STANDALONE
#include <asio.hpp>

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <optional>
#include <memory>
#include <functional>
#include <system_error>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>

// 使用通用原始协议
using Protocol = asio::generic::raw_protocol;
using Socket = Protocol::socket;
using Endpoint = Protocol::endpoint;

// 获取接口索引
unsigned int get_ifindex(const std::string& ifname) {
    struct ifreq ifr;
    std::strncpy(ifr.ifr_name, ifname.c_str(), IFNAMSIZ - 1);
    int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) throw std::system_error(errno, std::generic_category(), "socket");
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        close(s);
        throw std::system_error(errno, std::generic_category(), "ioctl");
    }
    close(s);
    return ifr.ifr_ifindex;
}

// 创建并绑定 CAN Socket
auto create_socket(asio::io_context& io, const std::string& ifname) -> std::shared_ptr<Socket> {
    auto sock = std::make_shared<Socket>(io, Protocol(PF_CAN, CAN_RAW));
    
    // 启用 CAN FD
    int enable_canfd = 1;
    setsockopt(sock->native_handle(), SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable_canfd, sizeof(enable_canfd));

    // 禁用回环
    int loopback = 0;
    setsockopt(sock->native_handle(), SOL_CAN_RAW, CAN_RAW_LOOPBACK, &loopback, sizeof(loopback));

    // 绑定
    struct sockaddr_can addr = {};
    addr.can_family = AF_CAN;
    addr.can_ifindex = get_ifindex(ifname);
    sock->bind(Endpoint(&addr, sizeof(addr)));
    
    return sock;
}

// 帧转换纯函数
auto transform_frame = [](const std::vector<uint8_t>& buffer, std::size_t bytes) -> std::optional<canfd_frame> {
    if (bytes == sizeof(can_frame)) {
        const auto* cf = reinterpret_cast<const can_frame*>(buffer.data());
        canfd_frame out = {};
        out.can_id = cf->can_id & ~CAN_RTR_FLAG;
        out.len = cf->can_dlc;
        out.flags = CANFD_BRS;
        std::copy(std::begin(cf->data), std::begin(cf->data) + cf->can_dlc, std::begin(out.data));
        return out;
    } else if (bytes == sizeof(canfd_frame)) {
        canfd_frame out = *reinterpret_cast<const canfd_frame*>(buffer.data());
        out.flags |= CANFD_BRS;
        return out;
    }
    return std::nullopt;
};

int main() {
    try {
        // 清理环境
        std::system("sudo cangw -F > /dev/null 2>&1");
        std::system("pkill -f \"candump -L can0 can1 can2\" > /dev/null 2>&1");

        asio::io_context io;
        auto tx_sock = create_socket(io, "can3");
        std::vector<std::string> rx_ifaces = {"can0", "can1", "can2"};
        
        std::cout << "cangw_cpp_asio: 转发 can0, can1, can2 -> can3 (CAN FD BRS)" << std::endl;

        for (const auto& ifname : rx_ifaces) {
            auto rx_sock = create_socket(io, ifname);
            auto buffer = std::make_shared<std::vector<uint8_t>>(sizeof(canfd_frame));

            // 递归 Lambda 实现异步循环
            using LoopSignature = std::function<void(std::error_code, std::size_t)>;
            auto loop = std::make_shared<LoopSignature>();
            
            *loop = [rx_sock, tx_sock, buffer, loop](std::error_code ec, std::size_t bytes) {
                if (ec) return;

                if (bytes > 0) {
                    if (auto frame = transform_frame(*buffer, bytes)) {
                        auto out_frame = std::make_shared<canfd_frame>(*frame);
                        tx_sock->async_send(asio::buffer(out_frame.get(), sizeof(canfd_frame)),
                            [out_frame](std::error_code, std::size_t){});
                    }
                }
                rx_sock->async_receive(asio::buffer(*buffer), *loop);
            };

            // 启动循环
            (*loop)({}, 0);
        }

        io.run();

    } catch (const std::exception& e) {
        std::cerr << "异常: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
