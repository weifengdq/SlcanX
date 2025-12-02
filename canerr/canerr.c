/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * canerr.c - Tool to view and decode CAN error frames
 *
 * Based on candump.c from can-utils
 */

#include <linux/can.h>
#include <linux/can/error.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef CAN_ERR_CNT
#define CAN_ERR_CNT 0x00000200U
#endif

void print_usage(char *prg) {
  fprintf(stderr, "Usage: %s <can-interface>\n", prg);
  fprintf(stderr, "Example: %s can0\n", prg);
}

void decode_error_frame(struct can_frame *cf) {
  printf("Error Frame ID: %08X DLC: %d\n", cf->can_id, cf->can_dlc);

  if (cf->can_id & CAN_ERR_BUSOFF) {
    printf("  [BUS OFF]\n");
  }

  if (cf->can_id & CAN_ERR_CRTL) {
    printf("  [CONTROLLER/FIRMWARE ERROR]\n");
    if (cf->data[1] & CAN_ERR_CRTL_RX_OVERFLOW)
      printf(
          "    - Rx Buffer Overflow (Firmware: Rx Failed / USB IN overflow)\n");
    if (cf->data[1] & CAN_ERR_CRTL_TX_OVERFLOW)
      printf("    - Tx Buffer Overflow (Firmware: Txfifo full)\n");
    if (cf->data[1] & CAN_ERR_CRTL_RX_PASSIVE)
      printf("    - Rx Error Passive\n");
    if (cf->data[1] & CAN_ERR_CRTL_TX_PASSIVE)
      printf("    - Tx Error Passive\n");
  }

  if (cf->can_id & CAN_ERR_PROT) {
    printf("  [PROTOCOL ERROR]\n");
    if (cf->data[2] & CAN_ERR_PROT_BIT)
      printf("    - Bit Error\n");
    if (cf->data[2] & CAN_ERR_PROT_FORM)
      printf("    - Format Error\n");
    if (cf->data[2] & CAN_ERR_PROT_STUFF)
      printf("    - Stuff Error\n");
    if (cf->data[2] & CAN_ERR_PROT_BIT0)
      printf("    - Unable to drive dominant bit (Bit0 Error)\n");
    if (cf->data[2] & CAN_ERR_PROT_BIT1)
      printf("    - Unable to drive recessive bit (Bit1 Error)\n");
    if (cf->data[2] & CAN_ERR_PROT_OVERLOAD)
      printf("    - Bus Overload\n");
    if (cf->data[2] & CAN_ERR_PROT_ACTIVE)
      printf("    - Active Error Announcement\n");
    if (cf->data[2] & CAN_ERR_PROT_TX)
      printf("    - Tx Error\n");

    if (cf->can_id & CAN_ERR_ACK)
      printf("    - ACK Error\n");

    if (cf->data[3] == CAN_ERR_PROT_LOC_CRC_SEQ)
      printf("    - Location: CRC Sequence\n");
    if (cf->data[3] == CAN_ERR_PROT_LOC_ACK)
      printf("    - Location: ACK Slot\n");
  }

  if (cf->can_id & CAN_ERR_CNT) {
    printf("  [ERROR COUNTERS]\n");
    printf("    - Tx Error Count: %d\n", cf->data[6]);
    printf("    - Rx Error Count: %d\n", cf->data[7]);
  }

  printf("\n");
}

int main(int argc, char **argv) {
  int s;
  struct sockaddr_can addr;
  struct ifreq ifr;
  struct can_frame frame;
  can_err_mask_t err_mask = CAN_ERR_MASK;

  if (argc != 2) {
    print_usage(argv[0]);
    return 1;
  }

  if ((s = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
    perror("socket");
    return 1;
  }

  strcpy(ifr.ifr_name, argv[1]);
  if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
    perror("ioctl");
    return 1;
  }

  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;

  if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    return 1;
  }

  /* Enable error frames */
  if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &err_mask,
                 sizeof(err_mask)) < 0) {
    perror("setsockopt");
    return 1;
  }

  printf("Listening for error frames on %s...\n", argv[1]);

  while (1) {
    int nbytes = read(s, &frame, sizeof(struct can_frame));

    if (nbytes < 0) {
      perror("read");
      return 1;
    }

    if (nbytes < (int)sizeof(struct can_frame)) {
      fprintf(stderr, "read: incomplete CAN frame\n");
      return 1;
    }

    if (frame.can_id & CAN_ERR_FLAG) {
      decode_error_frame(&frame);
    }
  }

  close(s);
  return 0;
}
