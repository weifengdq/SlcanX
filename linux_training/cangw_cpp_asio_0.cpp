/*
 * cangw_cpp_asio.cpp
 * 
 * Implementation of CAN gateway using C++17, Functional Programming style, and standalone Asio.
 * 
 * Prerequisites:
 * - Asio C++ Library (Standalone): https://github.com/chriskohlhoff/asio
 * 
 * Compilation:
 * g++ -std=c++17 -I/path/to/asio/include -o cangw_cpp_asio cangw_cpp_asio.cpp -lpthread
 * 
 * Note: Since Asio is a header-only library and might not be in the standard include path,
 * you need to ensure the compiler can find "asio.hpp".
 */

#define ASIO_STANDALONE
#include <asio.hpp>

#include <iostream>
#include <vector>
#include <string>
#include <variant>
#include <algorithm>
#include <optional>
#include <memory>
#include <functional>
#include <system_error>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>

// Use generic raw protocol for CAN
using Protocol = asio::generic::raw_protocol;
using Socket = Protocol::socket;
using Endpoint = Protocol::endpoint;

// Helper to get interface index
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

// Helper to create and bind a CAN socket
auto create_socket(asio::io_context& io, const std::string& ifname) -> std::shared_ptr<Socket> {
    auto sock = std::make_shared<Socket>(io, Protocol(PF_CAN, CAN_RAW));
    
    // Enable CAN FD
    int enable_canfd = 1;
    if (setsockopt(sock->native_handle(), SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable_canfd, sizeof(enable_canfd)) < 0) {
        throw std::system_error(errno, std::generic_category(), "setsockopt CAN_RAW_FD_FRAMES");
    }

    // Disable Loopback
    int loopback = 0;
    if (setsockopt(sock->native_handle(), SOL_CAN_RAW, CAN_RAW_LOOPBACK, &loopback, sizeof(loopback)) < 0) {
        throw std::system_error(errno, std::generic_category(), "setsockopt CAN_RAW_LOOPBACK");
    }

    // Bind
    struct sockaddr_can addr = {};
    addr.can_family = AF_CAN;
    addr.can_ifindex = get_ifindex(ifname);
    
    sock->bind(Endpoint(&addr, sizeof(addr)));
    
    return sock;
}

// Pure function to transform frame
auto transform_frame = [](const std::vector<uint8_t>& buffer, std::size_t bytes_transferred) -> std::optional<canfd_frame> {
    if (bytes_transferred == sizeof(can_frame)) {
        const auto* cf = reinterpret_cast<const can_frame*>(buffer.data());
        canfd_frame out = {};
        out.can_id = cf->can_id & ~CAN_RTR_FLAG;
        out.len = cf->can_dlc;
        out.flags = CANFD_BRS;
        std::copy(std::begin(cf->data), std::begin(cf->data) + cf->can_dlc, std::begin(out.data));
        return out;
    } else if (bytes_transferred == sizeof(canfd_frame)) {
        canfd_frame out = *reinterpret_cast<const canfd_frame*>(buffer.data());
        out.flags |= CANFD_BRS;
        return out;
    }
    return std::nullopt;
};

int main() {
    try {
        // Cleanup
        std::system("sudo cangw -F > /dev/null 2>&1");
        std::system("pkill -f \"candump -L can0 can1 can2\" > /dev/null 2>&1");

        asio::io_context io;

        // Create sockets
        auto tx_sock = create_socket(io, "can3");
        std::vector<std::string> rx_ifaces = {"can0", "can1", "can2"};
        
        std::cout << "cangw_cpp_asio: Forwarding can0, can1, can2 -> can3 (CAN FD BRS)" << std::endl;

        // Define the async loop function
        // We use a recursive lambda pattern with std::function to keep it alive
        std::vector<std::function<void()>> loops;

        for (const auto& ifname : rx_ifaces) {
            auto rx_sock = create_socket(io, ifname);
            
            // Each interface gets its own loop and buffer
            // We capture by value to keep shared_ptrs alive
            std::function<void()> loop;
            
            // Buffer needs to be kept alive across async calls
            auto buffer = std::make_shared<std::vector<uint8_t>>(sizeof(canfd_frame));

            loop = [rx_sock, tx_sock, buffer, &loops, idx = loops.size()]() {
                // We need to access the 'loop' function itself to recurse.
                // Since we can't capture 'loop' (it's being defined), we access it from the 'loops' vector
                // or use a Y-combinator. Here we use the vector reference which is stable enough for main scope.
                // A safer way for general async is `enable_shared_from_this` or passing `self`.
                // Here we'll use a raw recursive lambda with a fix-point helper or just capture the std::function by reference?
                // No, std::function by reference is dangerous if it moves.
                // Let's use a simple trick: pass the function to the handler.
                
                rx_sock->async_receive(asio::buffer(*buffer),
                    [rx_sock, tx_sock, buffer, loop_func = loops.size() /* placeholder */](std::error_code ec, std::size_t bytes) {
                        // We can't easily retrieve the loop function from the vector inside the lambda 
                        // without capturing the vector reference, which is fine here as main() outlives io.run().
                    });
            };
        }
        
        // Let's rewrite the loop structure to be cleaner and self-contained
        for (const auto& ifname : rx_ifaces) {
            auto rx_sock = create_socket(io, ifname);
            auto buffer = std::make_shared<std::vector<uint8_t>>(sizeof(canfd_frame));

            // Recursive lambda using a self-referencing shared_ptr approach (simulating Y-combinator for async)
            struct Loop {
                std::shared_ptr<Socket> rx;
                std::shared_ptr<Socket> tx;
                std::shared_ptr<std::vector<uint8_t>> buf;
                
                void operator()(std::error_code ec = {}, std::size_t bytes = 0) {
                    if (ec) {
                        if (ec != asio::error::operation_aborted) {
                            std::cerr << "Receive error: " << ec.message() << std::endl;
                        }
                        return;
                    }

                    if (bytes > 0) {
                        // Transform and Send
                        if (auto frame = transform_frame(*buf, bytes)) {
                            // Async send (fire and forget for this example, or chain it)
                            // We copy the frame to a shared_ptr to keep it alive for the send operation
                            auto out_frame = std::make_shared<canfd_frame>(*frame);
                            tx->async_send(asio::buffer(out_frame.get(), sizeof(canfd_frame)),
                                [out_frame](std::error_code ec, std::size_t) {
                                    if (ec) std::cerr << "Send error: " << ec.message() << std::endl;
                                });
                        }
                    }

                    // Next receive
                    // We capture 'this' (copy of the struct) to keep the chain alive? 
                    // No, we need a shared_ptr to the Loop state.
                }
            };
            
            // Better approach: A simple recursive function using std::enable_shared_from_this is the C++ idiomatic way for Asio.
            // But to stick to "Functional" and "Lambda", let's use a recursive lambda with a shared_ptr to itself.
            
            using LoopSignature = std::function<void(std::error_code, std::size_t)>;
            auto loop_ptr = std::make_shared<LoopSignature>();
            
            *loop_ptr = [rx_sock, tx_sock, buffer, loop_ptr](std::error_code ec, std::size_t bytes) {
                if (ec) return;

                if (bytes > 0) {
                    if (auto frame = transform_frame(*buffer, bytes)) {
                        auto out_frame = std::make_shared<canfd_frame>(*frame);
                        tx_sock->async_send(asio::buffer(out_frame.get(), sizeof(canfd_frame)),
                            [out_frame](std::error_code, std::size_t){});
                    }
                }

                rx_sock->async_receive(asio::buffer(*buffer), *loop_ptr);
            };

            // Start the loop
            (*loop_ptr)({}, 0);
        }

        io.run();

    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
