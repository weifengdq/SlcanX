#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>

int main(void)
{
    int s;
    struct sockaddr_can addr;
    struct ifreq ifr;
    int ifidx_can0, ifidx_can1, ifidx_can2, ifidx_can3;
    int enable_canfd = 1;
    int loopback = 0;

    // Cleanup existing rules and background jobs from previous scripts
    // This prevents double forwarding if cangw_0.sh was run previously
    system("sudo cangw -F > /dev/null 2>&1");
    system("pkill -f \"candump -L can0 can1 can2\" > /dev/null 2>&1");

    // Create socket
    if ((s = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
        perror("Socket");
        return 1;
    }

    // Enable CAN FD support
    if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable_canfd, sizeof(enable_canfd))){
        perror("setsockopt CAN_RAW_FD_FRAMES");
        return 1;
    }

    // Disable loopback (prevent receiving frames sent by this socket)
    if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_LOOPBACK, &loopback, sizeof(loopback))){
        perror("setsockopt CAN_RAW_LOOPBACK");
        return 1;
    }

    // Get interface indices
    ifidx_can0 = if_nametoindex("can0");
    ifidx_can1 = if_nametoindex("can1");
    ifidx_can2 = if_nametoindex("can2");
    ifidx_can3 = if_nametoindex("can3");

    if (ifidx_can0 == 0 || ifidx_can1 == 0 || ifidx_can2 == 0 || ifidx_can3 == 0) {
        fprintf(stderr, "One of the interfaces can0-can3 not found\n");
        return 1;
    }

    // Bind to all interfaces
    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = 0; // Any interface

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Bind");
        return 1;
    }

    printf("cangw_c: Forwarding can0, can1, can2 -> can3 (CAN FD BRS)\n");

    // Loop
    while (1) {
        struct canfd_frame frame;
        struct sockaddr_can src_addr;
        socklen_t len = sizeof(src_addr);
        
        // Read frame
        int nbytes = recvfrom(s, &frame, sizeof(frame), 0, (struct sockaddr *)&src_addr, &len);
        
        if (nbytes < 0) {
            perror("read");
            return 1;
        }

        // Check source interface
        int src_idx = src_addr.can_ifindex;
        if (src_idx != ifidx_can0 && src_idx != ifidx_can1 && src_idx != ifidx_can2) {
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
        
        // Send to can3
        struct sockaddr_can dst_addr;
        memset(&dst_addr, 0, sizeof(dst_addr));
        dst_addr.can_family = AF_CAN;
        dst_addr.can_ifindex = ifidx_can3;
        
        // Send as CAN FD frame (sizeof(struct canfd_frame))
        int wbytes = sendto(s, &out_frame, sizeof(out_frame), 0, (struct sockaddr *)&dst_addr, sizeof(dst_addr));
        
        if (wbytes < 0) {
            perror("write");
        }
    }

    close(s);
    return 0;
}
