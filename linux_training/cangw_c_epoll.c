#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <errno.h>

#define MAX_EVENTS 10

// Helper to open and configure a CAN socket
int open_can_socket(const char *ifname) {
    int s;
    struct sockaddr_can addr;
    struct ifreq ifr;
    int enable_canfd = 1;
    int loopback = 0;

    if ((s = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
        perror("socket");
        return -1;
    }

    // Enable CAN FD support
    if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable_canfd, sizeof(enable_canfd))){
        perror("setsockopt CAN_RAW_FD_FRAMES");
        close(s);
        return -1;
    }

    // Disable loopback
    if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_LOOPBACK, &loopback, sizeof(loopback))){
        perror("setsockopt CAN_RAW_LOOPBACK");
        close(s);
        return -1;
    }

    strcpy(ifr.ifr_name, ifname);
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        perror("SIOCGIFINDEX");
        close(s);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(s);
        return -1;
    }

    return s;
}

int main(void) {
    int epfd;
    int s_can0, s_can1, s_can2, s_can3;
    struct epoll_event ev, events[MAX_EVENTS];

    // Cleanup existing rules and background jobs
    system("sudo cangw -F > /dev/null 2>&1");
    system("pkill -f \"candump -L can0 can1 can2\" > /dev/null 2>&1");

    // Open sockets
    s_can0 = open_can_socket("can0");
    s_can1 = open_can_socket("can1");
    s_can2 = open_can_socket("can2");
    s_can3 = open_can_socket("can3"); // Used for sending

    if (s_can0 < 0 || s_can1 < 0 || s_can2 < 0 || s_can3 < 0) {
        fprintf(stderr, "Failed to open CAN sockets\n");
        return 1;
    }

    // Create epoll instance
    epfd = epoll_create1(0);
    if (epfd == -1) {
        perror("epoll_create1");
        return 1;
    }

    // Add RX sockets to epoll
    ev.events = EPOLLIN;
    ev.data.fd = s_can0;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, s_can0, &ev) == -1) {
        perror("epoll_ctl: s_can0");
        return 1;
    }

    ev.events = EPOLLIN;
    ev.data.fd = s_can1;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, s_can1, &ev) == -1) {
        perror("epoll_ctl: s_can1");
        return 1;
    }

    ev.events = EPOLLIN;
    ev.data.fd = s_can2;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, s_can2, &ev) == -1) {
        perror("epoll_ctl: s_can2");
        return 1;
    }

    printf("cangw_c_epoll: Forwarding can0, can1, can2 -> can3 (CAN FD BRS) using epoll\n");

    while (1) {
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_wait");
            break;
        }

        for (int n = 0; n < nfds; ++n) {
            int fd = events[n].data.fd;
            struct canfd_frame frame;
            int nbytes;

            // Read frame
            nbytes = read(fd, &frame, sizeof(frame));
            if (nbytes < 0) {
                perror("read");
                continue;
            }

            // Prepare frame for sending to can3
            struct canfd_frame out_frame;
            memset(&out_frame, 0, sizeof(out_frame));

            if (nbytes == sizeof(struct can_frame)) {
                // Received Classical CAN frame
                struct can_frame *cf = (struct can_frame *)&frame;
                
                out_frame.can_id = cf->can_id;
                out_frame.len = cf->can_dlc; 
                
                // Copy data
                memcpy(out_frame.data, cf->data, cf->can_dlc);
                
                // Clear RTR flag as CAN FD does not support it
                if (out_frame.can_id & CAN_RTR_FLAG) {
                    out_frame.can_id &= ~CAN_RTR_FLAG;
                }
                
            } else if (nbytes == sizeof(struct canfd_frame)) {
                // Received CAN FD frame
                out_frame = frame;
            } else {
                // Unknown size, ignore
                continue;
            }

            // Force CAN FD BRS
            out_frame.flags |= CANFD_BRS;

            // Send to can3 using s_can3
            // Since s_can3 is bound to can3, write() will send out of can3
            int wbytes = write(s_can3, &out_frame, sizeof(out_frame));
            if (wbytes < 0) {
                perror("write");
            }
        }
    }

    close(s_can0);
    close(s_can1);
    close(s_can2);
    close(s_can3);
    close(epfd);
    return 0;
}
