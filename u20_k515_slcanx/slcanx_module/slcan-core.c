/*
 * slcan.c - serial line CAN interface driver (using tty line discipline)
 *
 * This file is derived from linux/drivers/net/slip/slip.c and got
 * inspiration from linux/drivers/net/can/can327.c for the rework made
 * on the line discipline code.
 *
 * slip.c Authors  : Laurence Culhane <loz@holmes.demon.co.uk>
 *                   Fred N. van Kempen <waltje@uwalt.nl.mugnet.org>
 * slcan.c Author  : Oliver Hartkopp <socketcan@hartkopp.net>
 * can327.c Author : Max Staudt <max-linux@enpas.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see http://www.gnu.org/licenses/gpl.html
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>

#include <linux/bitops.h>
#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/length.h>
#include <linux/can/skb.h>
#include <linux/errno.h>
#include <linux/hrtimer.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>

#include "slcan.h"

/* Compatibility for older kernels */
#ifndef CAN_BITRATE_UNSET
#define CAN_BITRATE_UNSET 0
#endif

#ifndef CAN_BITRATE_UNKNOWN
#define CAN_BITRATE_UNKNOWN 0xffffffffU
#endif

#ifndef CAN_ERR_CNT
#define CAN_ERR_CNT 0x00000200U
#endif

#ifndef can_dev_dropped_skb
#define can_dev_dropped_skb(dev, skb) can_dropped_invalid_skb(dev, skb)
#endif

MODULE_ALIAS_LDISC(N_SLCAN);
MODULE_DESCRIPTION("serial line CAN interface (slcanx)");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Oliver Hartkopp <socketcan@hartkopp.net>");
MODULE_AUTHOR("Dario Binacchi <dario.binacchi@amarulasolutions.com>");

static unsigned int tx_batch_us = 0;
module_param(tx_batch_us, uint, 0644);
MODULE_PARM_DESC(tx_batch_us,
                 "Batch transmission time in microseconds (0=disable)");

/* maximum rx buffer len: extended CAN FD frame (64 bytes payload) */
#define SLCAN_MTU (1024 - 40)

#define SLCAN_CMD_LEN 1
#define SLCAN_SFF_ID_LEN 3
#define SLCAN_EFF_ID_LEN 8
#define SLCAN_STATE_LEN 1
#define SLCAN_STATE_BE_RXCNT_LEN 3
#define SLCAN_STATE_BE_TXCNT_LEN 3
#define SLCAN_STATE_FRAME_LEN                                                  \
  (1 + SLCAN_CMD_LEN + SLCAN_STATE_BE_RXCNT_LEN + SLCAN_STATE_BE_TXCNT_LEN)
#define SLCAN_MAX_CHANNELS 4

struct slcan_port {
  struct tty_struct *tty;
  spinlock_t lock;
  struct work_struct tx_work;
  struct hrtimer tx_timer;
  wait_queue_head_t xcmd_wait;
  unsigned char rbuff[SLCAN_MTU];
  int rcount;
  unsigned char xbuff[SLCAN_MTU];
  unsigned char *xhead;
  int xleft;
  unsigned long flags;
#define SLF_ERROR 0
#define SLF_XCMD 1
  struct slcan *channels[SLCAN_MAX_CHANNELS];
  struct slcan *tx_chan;
};

struct slcan {
  struct can_priv can;
  struct slcan_port *port;
  struct net_device *dev;
  unsigned long cmd_flags;
#define CF_ERR_RST 0
  u8 chan_idx;
  bool is_open;
};

static int slcan_transmit_cmd(struct slcan *sl, const unsigned char *cmd);

static int slcan_hex_to_u32(const char *s, int len, u32 *val) {
  u32 v = 0;
  int i, d;
  for (i = 0; i < len; i++) {
    d = hex_to_bin(s[i]);
    if (d < 0)
      return -1;
    v = (v << 4) | d;
  }
  *val = v;
  return 0;
}

static struct slcan *slcan_first_channel(struct slcan_port *port) {
  int i;

  for (i = 0; i < SLCAN_MAX_CHANNELS; i++) {
    if (port->channels[i])
      return port->channels[i];
  }

  return NULL;
}

static struct slcan *slcan_select_channel(struct slcan_port *port,
                                          int *prefix_len) {
  struct slcan *sl = NULL;
  int idx = 0;

  if (!port || !prefix_len || port->rcount <= 0)
    return NULL;

  if (port->rbuff[0] >= '0' && port->rbuff[0] < '0' + SLCAN_MAX_CHANNELS) {
    idx = port->rbuff[0] - '0';
    *prefix_len = 1;
    sl = port->channels[idx];
  } else {
    *prefix_len = 0;
    sl = slcan_first_channel(port);
  }

  if (!sl)
    sl = slcan_first_channel(port);

  return sl;
}

static const u32 slcan_bitrate_const[] = {
    10000, 20000, 50000, 100000, 125000, 250000, 500000, 800000, 1000000};

static const u32 slcan_data_bitrate_const[] = {
    0,        1000000,  2000000,  3000000,  4000000,  5000000,
    6000000,  7000000,  8000000,  9000000,  10000000, 11000000,
    12000000, 13000000, 14000000, 16000000,
};

static int slcan_find_bitrate_index(u32 bitrate, const u32 *table, size_t cnt) {
  int i;

  if (!bitrate || bitrate == CAN_BITRATE_UNSET ||
      bitrate == CAN_BITRATE_UNKNOWN)
    return -EINVAL;

  for (i = 0; i < cnt; i++) {
    if (table[i] == bitrate)
      return i;
  }

  return -EINVAL;
}

static int slcan_nominal_bitrate_index(u32 bitrate) {
  return slcan_find_bitrate_index(bitrate, slcan_bitrate_const,
                                  ARRAY_SIZE(slcan_bitrate_const));
}

static int slcan_data_bitrate_index(u32 bitrate) {
  return slcan_find_bitrate_index(bitrate, slcan_data_bitrate_const,
                                  ARRAY_SIZE(slcan_data_bitrate_const));
}

static int slcan_send_sample_point_cmd(struct slcan *sl, u16 sample_point,
                                       bool is_fd) {
  unsigned char cmd[32];

  if (!sample_point)
    return 0;

  sample_point = clamp_t(u16, sample_point, 1, 1000);
  snprintf(cmd, sizeof(cmd), "%c%u\r", is_fd ? 'P' : 'p', sample_point);

  return slcan_transmit_cmd(sl, cmd);
}

bool slcan_err_rst_on_open(struct net_device *ndev) {
  struct slcan *sl = netdev_priv(ndev);

  return !!test_bit(CF_ERR_RST, &sl->cmd_flags);
}

int slcan_enable_err_rst_on_open(struct net_device *ndev, bool on) {
  struct slcan *sl = netdev_priv(ndev);

  if (netif_running(ndev))
    return -EBUSY;

  if (on)
    set_bit(CF_ERR_RST, &sl->cmd_flags);
  else
    clear_bit(CF_ERR_RST, &sl->cmd_flags);

  return 0;
}

/*************************************************************************
 *			SLCAN ENCAPSULATION FORMAT			 *
 *************************************************************************/

/* A CAN frame has a can_id (11 bit standard frame format OR 29 bit extended
 * frame format) a data length code (len) which can be from 0 to 8
 * and up to <len> data bytes as payload.
 * Additionally a CAN frame may become a remote transmission frame if the
 * RTR-bit is set. This causes another ECU to send a CAN frame with the
 * given can_id.
 *
 * The SLCAN ASCII representation of these different frame types is:
 * <type> <id> <dlc> <data>*
 *
 * Extended frames (29 bit) are defined by capital characters in the type.
 * RTR frames are defined as 'r' types - normal frames have 't' type:
 * t => 11 bit data frame
 * r => 11 bit RTR frame
 * T => 29 bit data frame
 * R => 29 bit RTR frame
 *
 * The <id> is 3 (standard) or 8 (extended) bytes in ASCII Hex (base64).
 * The <dlc> is a one byte ASCII number ('0' - '8')
 * The <data> section has at much ASCII Hex bytes as defined by the <dlc>
 *
 * Examples:
 *
 * t1230 : can_id 0x123, len 0, no data
 * t4563112233 : can_id 0x456, len 3, data 0x11 0x22 0x33
 * T12ABCDEF2AA55 : extended can_id 0x12ABCDEF, len 2, data 0xAA 0x55
 * r1230 : can_id 0x123, len 0, no data, remote transmission request
 *
 */

/*************************************************************************
 *			STANDARD SLCAN DECAPSULATION			 *
 *************************************************************************/

/* Send one completely decapsulated can_frame to the network layer */
static void slcan_bump_frame(struct slcan *sl) {
  struct slcan_port *port = sl->port;
  char *cmd = port->rbuff;
  bool is_fd = false, is_brs = false, is_eff = false, is_rtr = false;
  struct sk_buff *skb;
  struct can_frame *cf = NULL;
  struct canfd_frame *cfd = NULL;
  int id_len, payload_len = 0;
  int dlc_val;
  char dlc_char;
  u32 tmpid;
  unsigned char *pdata;
  int i;

  switch (*cmd) {
  case 'r':
    is_rtr = true;
    break;
  case 't':
    break;
  case 'R':
    is_rtr = true;
    is_eff = true;
    break;
  case 'T':
    is_eff = true;
    break;
  case 'd':
    is_fd = true;
    break;
  case 'D':
    is_fd = true;
    is_eff = true;
    break;
  case 'b':
    is_fd = true;
    is_brs = true;
    break;
  case 'B':
    is_fd = true;
    is_brs = true;
    is_eff = true;
    break;
  default:
    return;
  }

  if (is_fd) {
    skb = alloc_canfd_skb(sl->dev, &cfd);
    if (unlikely(!skb)) {
      sl->dev->stats.rx_dropped++;
      return;
    }
  } else {
    skb = alloc_can_skb(sl->dev, &cf);
    if (unlikely(!skb)) {
      sl->dev->stats.rx_dropped++;
      return;
    }
    cfd = (struct canfd_frame *)cf;
  }

  id_len = is_eff ? SLCAN_EFF_ID_LEN : SLCAN_SFF_ID_LEN;
  if (slcan_hex_to_u32(port->rbuff + SLCAN_CMD_LEN, id_len, &tmpid))
    goto decode_failed;
  dlc_char = port->rbuff[SLCAN_CMD_LEN + id_len];

  if (is_eff)
    tmpid &= CAN_EFF_MASK;
  else
    tmpid &= CAN_SFF_MASK;

  if (is_eff)
    cfd->can_id = tmpid | CAN_EFF_FLAG;
  else
    cfd->can_id = tmpid;

  if (is_rtr)
    cfd->can_id |= CAN_RTR_FLAG;

  dlc_val = hex_to_bin(dlc_char);
  if (dlc_val < 0)
    goto decode_failed;

  if (is_fd) {
    payload_len = can_fd_dlc2len(dlc_val & 0xF);
    if (payload_len > CANFD_MAX_DLEN)
      goto decode_failed;
    cfd->len = payload_len;
    cfd->flags = is_brs ? CANFD_BRS : 0;
    if (is_rtr)
      goto decode_failed;
  } else {
    if (dlc_val > 8)
      goto decode_failed;
    cf->len = dlc_val;
    payload_len = cf->len;
  }

  cmd += SLCAN_CMD_LEN + id_len + 1;
  if (!(cfd->can_id & CAN_RTR_FLAG)) {
    if (port->rcount < SLCAN_CMD_LEN + id_len + 1 + payload_len * 2)
      goto decode_failed;
    pdata = cfd->data;
    for (i = 0; i < payload_len; i++) {
      int high = hex_to_bin(*cmd++);
      int low = hex_to_bin(*cmd++);
      if (high < 0 || low < 0)
        goto decode_failed;
      pdata[i] = (high << 4) | low;
    }
  }

  sl->dev->stats.rx_packets++;
  if (!(cfd->can_id & CAN_RTR_FLAG))
    sl->dev->stats.rx_bytes += payload_len;

  netif_rx(skb);
  return;

decode_failed:
  sl->dev->stats.rx_errors++;
  dev_kfree_skb(skb);
}

/* A change state frame must contain state info and receive and transmit
 * error counters.
 *
 * Examples:
 *
 * sb256256 : state bus-off: rx counter 256, tx counter 256
 * sa057033 : state active, rx counter 57, tx counter 33
 */
static void slcan_bump_state(struct slcan *sl) {
  struct net_device *dev = sl->dev;
  struct sk_buff *skb;
  struct can_frame *cf;
  struct slcan_port *port = sl->port;
  char *cmd = port->rbuff;
  u32 rxerr, txerr;
  enum can_state state, rx_state, tx_state;

  switch (cmd[1]) {
  case 'a':
    state = CAN_STATE_ERROR_ACTIVE;
    break;
  case 'w':
    state = CAN_STATE_ERROR_WARNING;
    break;
  case 'p':
    state = CAN_STATE_ERROR_PASSIVE;
    break;
  case 'b':
    state = CAN_STATE_BUS_OFF;
    break;
  default:
    return;
  }

  if (state == sl->can.state || port->rcount < SLCAN_STATE_FRAME_LEN)
    return;

  cmd += SLCAN_STATE_BE_RXCNT_LEN + SLCAN_CMD_LEN + 1;
  cmd[SLCAN_STATE_BE_TXCNT_LEN] = 0;
  if (kstrtou32(cmd, 10, &txerr))
    return;

  *cmd = 0;
  cmd -= SLCAN_STATE_BE_RXCNT_LEN;
  if (kstrtou32(cmd, 10, &rxerr))
    return;

  skb = alloc_can_err_skb(dev, &cf);

  tx_state = txerr >= rxerr ? state : 0;
  rx_state = txerr <= rxerr ? state : 0;
  can_change_state(dev, cf, tx_state, rx_state);

  if (state == CAN_STATE_BUS_OFF) {
    can_bus_off(dev);
  } else if (skb) {
    cf->can_id |= CAN_ERR_CNT;
    cf->data[6] = txerr;
    cf->data[7] = rxerr;
  }

  if (skb)
    netif_rx(skb);
}

/* An error frame can contain more than one type of error.
 *
 * Examples:
 *
 * e1a : len 1, errors: ACK error
 * e3bcO: len 3, errors: Bit0 error, CRC error, Tx overrun error
 */
static void slcan_bump_err(struct slcan *sl) {
  struct net_device *dev = sl->dev;
  struct sk_buff *skb;
  struct can_frame *cf;
  struct slcan_port *port = sl->port;
  char *cmd = port->rbuff;
  bool rx_errors = false, tx_errors = false, rx_over_errors = false;
  int i, len;

  /* get len from sanitized ASCII value */
  len = cmd[1];
  if (len >= '0' && len < '9')
    len -= '0';
  else
    return;

  if ((len + SLCAN_CMD_LEN + 1) > port->rcount)
    return;

  skb = alloc_can_err_skb(dev, &cf);

  if (skb)
    cf->can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR;

  cmd += SLCAN_CMD_LEN + 1;
  for (i = 0; i < len; i++, cmd++) {
    switch (*cmd) {
    case 'a':
      netdev_dbg(dev, "ACK error\n");
      tx_errors = true;
      if (skb) {
        cf->can_id |= CAN_ERR_ACK;
        cf->data[3] = CAN_ERR_PROT_LOC_ACK;
      }

      break;
    case 'b':
      netdev_dbg(dev, "Bit0 error\n");
      tx_errors = true;
      if (skb)
        cf->data[2] |= CAN_ERR_PROT_BIT0;

      break;
    case 'B':
      netdev_dbg(dev, "Bit1 error\n");
      tx_errors = true;
      if (skb)
        cf->data[2] |= CAN_ERR_PROT_BIT1;

      break;
    case 'c':
      netdev_dbg(dev, "CRC error\n");
      rx_errors = true;
      if (skb) {
        cf->data[2] |= CAN_ERR_PROT_BIT;
        cf->data[3] = CAN_ERR_PROT_LOC_CRC_SEQ;
      }

      break;
    case 'f':
      netdev_dbg(dev, "Form Error\n");
      rx_errors = true;
      if (skb)
        cf->data[2] |= CAN_ERR_PROT_FORM;

      break;
    case 'o':
      netdev_dbg(dev, "Rx overrun error\n");
      rx_over_errors = true;
      rx_errors = true;
      if (skb) {
        cf->can_id |= CAN_ERR_CRTL;
        cf->data[1] = CAN_ERR_CRTL_RX_OVERFLOW;
      }

      break;
    case 'O':
      netdev_dbg(dev, "Tx overrun error\n");
      tx_errors = true;
      if (skb) {
        cf->can_id |= CAN_ERR_CRTL;
        cf->data[1] = CAN_ERR_CRTL_TX_OVERFLOW;
      }

      break;
    case 's':
      netdev_dbg(dev, "Stuff error\n");
      rx_errors = true;
      if (skb)
        cf->data[2] |= CAN_ERR_PROT_STUFF;

      break;
    default:
      if (skb)
        dev_kfree_skb(skb);

      return;
    }
  }

  if (rx_errors)
    dev->stats.rx_errors++;

  if (rx_over_errors)
    dev->stats.rx_over_errors++;

  if (tx_errors)
    dev->stats.tx_errors++;

  if (skb)
    netif_rx(skb);
}

/* Parse new error frame format: Eslffttss */
static void slcan_bump_err_new(struct slcan *sl) {
  struct net_device *dev = sl->dev;
  struct sk_buff *skb;
  struct can_frame *cf;
  struct slcan_port *port = sl->port;
  char *cmd = port->rbuff;
  u32 txerr, rxerr;
  u8 fw_err;
  enum can_state state;
  char hex_buf[3];

  if (port->rcount < 9)
    return;

  skb = alloc_can_err_skb(dev, &cf);
  if (!skb)
    return;

  /* Bus Status */
  switch (cmd[1]) {
  case '0':
    state = CAN_STATE_ERROR_ACTIVE;
    break;
  case '1':
    state = CAN_STATE_ERROR_WARNING;
    break;
  case '2':
    state = CAN_STATE_ERROR_PASSIVE;
    break;
  case '3':
    state = CAN_STATE_BUS_OFF;
    break;
  default:
    state = CAN_STATE_ERROR_ACTIVE;
    break;
  }

  /* Last Protocol Error */
  cf->can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR;
  switch (cmd[2]) {
  case '0': /* None */
    break;
  case '1':
    cf->data[2] |= CAN_ERR_PROT_STUFF;
    break;
  case '2':
    cf->data[2] |= CAN_ERR_PROT_FORM;
    break;
  case '3':
    cf->can_id |= CAN_ERR_ACK;
    cf->data[3] = CAN_ERR_PROT_LOC_ACK;
    break;
  case '4':
    cf->data[2] |= CAN_ERR_PROT_BIT1;
    break;
  case '5':
    cf->data[2] |= CAN_ERR_PROT_BIT0;
    break;
  case '6':
    cf->data[3] = CAN_ERR_PROT_LOC_CRC_SEQ;
    break; /* CRC Error */
  }

  /* Firmware Error Flags */
  hex_buf[0] = cmd[3];
  hex_buf[1] = cmd[4];
  hex_buf[2] = 0;
  if (!kstrtou8(hex_buf, 16, &fw_err)) {
    if (fw_err & 0x01) { /* Rx Failed (Buffer Full) */
      cf->can_id |= CAN_ERR_CRTL;
      cf->data[1] |= CAN_ERR_CRTL_RX_OVERFLOW;
      dev->stats.rx_over_errors++;
    }
    if (fw_err & 0x04) { /* Txfifo full */
      cf->can_id |= CAN_ERR_CRTL;
      cf->data[1] |= CAN_ERR_CRTL_TX_OVERFLOW;
    }
    if (fw_err & 0x08) { /* USB IN buffer overflow */
      cf->can_id |= CAN_ERR_CRTL;
      cf->data[1] |= CAN_ERR_CRTL_RX_OVERFLOW;
      dev->stats.rx_over_errors++;
    }
  }

  /* Tx Error Count */
  hex_buf[0] = cmd[5];
  hex_buf[1] = cmd[6];
  hex_buf[2] = 0;
  if (!kstrtou32(hex_buf, 16, &txerr))
    cf->data[6] = txerr;

  /* Rx Error Count */
  hex_buf[0] = cmd[7];
  hex_buf[1] = cmd[8];
  hex_buf[2] = 0;
  if (!kstrtou32(hex_buf, 16, &rxerr))
    cf->data[7] = rxerr;

  cf->can_id |= CAN_ERR_CNT;

  if (state == CAN_STATE_BUS_OFF) {
    can_bus_off(dev);
  } else {
    sl->can.state = state;
  }

  netif_rx(skb);

  dev->stats.rx_packets++;
  dev->stats.rx_bytes += cf->len;
}

static void slcan_bump(struct slcan *sl) {
  struct slcan_port *port = sl->port;

  switch (port->rbuff[0]) {
  case 'r':
    fallthrough;
  case 't':
    fallthrough;
  case 'R':
    fallthrough;
  case 'T':
    return slcan_bump_frame(sl);
  case 'd':
    fallthrough;
  case 'D':
    fallthrough;
  case 'b':
    fallthrough;
  case 'B':
    return slcan_bump_frame(sl);
  case 'e':
    return slcan_bump_err(sl);
  case 'E':
    return slcan_bump_err_new(sl);
  case 's':
    return slcan_bump_state(sl);
  default:
    return;
  }
}

/* parse tty input stream */
static void slcan_unesc(struct slcan_port *port, unsigned char s) {
  if (!port)
    return;

  if ((s == '\r') || (s == '\a')) { /* CR or BEL ends the pdu */
    if (!test_and_clear_bit(SLF_ERROR, &port->flags) && port->rcount > 4) {
      int prefix_len = 0;
      struct slcan *sl = slcan_select_channel(port, &prefix_len);

      if (sl) {
        if (prefix_len > 0 && prefix_len < port->rcount) {
          port->rcount -= prefix_len;
          memmove(port->rbuff, port->rbuff + prefix_len, port->rcount);
        }
        slcan_bump(sl);
      }
    }

    port->rcount = 0;
  } else {
    if (!test_bit(SLF_ERROR, &port->flags)) {
      if (port->rcount < SLCAN_MTU) {
        port->rbuff[port->rcount++] = s;
        return;
      }

      {
        struct slcan *tmp = slcan_first_channel(port);

        if (tmp)
          tmp->dev->stats.rx_over_errors++;
      }
      set_bit(SLF_ERROR, &port->flags);
    }
  }
}

/*************************************************************************
 *			STANDARD SLCAN ENCAPSULATION			 *
 *************************************************************************/

/* Encapsulate one CAN or CAN FD frame and stuff into a TTY queue. */
static void slcan_encaps(struct slcan *sl, const struct canfd_frame *cfd,
                         bool is_fd) {
  int actual, i;
  unsigned char *pos;
  unsigned char *endpos;
  canid_t id = cfd->can_id;
  struct slcan_port *port = sl->port;
  bool is_eff = id & CAN_EFF_FLAG;
  bool is_rtr = id & CAN_RTR_FLAG;
  char cmd;
  unsigned char dlc_char;
  int data_len;

  if (port->xleft == 0)
    port->xhead = port->xbuff;
  pos = port->xhead + port->xleft;

  if (sl->chan_idx)
    *pos++ = '0' + sl->chan_idx;

  if (is_fd) {
    bool brs = cfd->flags & CANFD_BRS;
    cmd = is_eff ? (brs ? 'B' : 'D') : (brs ? 'b' : 'd');
    data_len = cfd->len;
    dlc_char = hex_asc_upper[can_fd_len2dlc(data_len) & 0xF];
  } else {
    const struct can_frame *cf = (const struct can_frame *)cfd;
    if (is_rtr)
      cmd = is_eff ? 'R' : 'r';
    else
      cmd = is_eff ? 'T' : 't';
    data_len = cf->len;
    dlc_char = '0' + cf->len;
  }

  *pos++ = cmd;

  /* determine number of chars for the CAN-identifier */
  if (is_eff) {
    id &= CAN_EFF_MASK;
    endpos = pos + SLCAN_EFF_ID_LEN - 1;
  } else {
    id &= CAN_SFF_MASK;
    endpos = pos + SLCAN_SFF_ID_LEN - 1;
  }

  /* build 3 (SFF) or 8 (EFF) digit CAN identifier */
  while (endpos >= pos) {
    *endpos-- = hex_asc_upper[id & 0xf];
    id >>= 4;
  }

  pos += is_eff ? SLCAN_EFF_ID_LEN : SLCAN_SFF_ID_LEN;

  *pos++ = dlc_char;

  /* RTR frames may have a dlc > 0 but they never have any data bytes */
  if (!is_rtr) {
    const unsigned char *data = cfd->data;
    for (i = 0; i < data_len; i++)
      pos = hex_byte_pack_upper(pos, data[i]);

    sl->dev->stats.tx_bytes += data_len;
  }

  *pos++ = '\r';

  port->xleft = pos - port->xhead;
  sl->dev->stats.tx_packets++;

  if (tx_batch_us > 0) {
    if (port->xleft < SLCAN_MTU - 100) {
      if (!hrtimer_is_queued(&port->tx_timer))
        hrtimer_start(&port->tx_timer, ns_to_ktime(tx_batch_us * 1000),
                      HRTIMER_MODE_REL);
      return;
    }
    hrtimer_cancel(&port->tx_timer);
  }

  /* Order of next two lines is *very* important.
   * When we are sending a little amount of data,
   * the transfer may be completed inside the ops->write()
   * routine, because it's running with interrupts enabled.
   * In this case we *never* got WRITE_WAKEUP event,
   * if we did not request it before write operation.
   *       14 Oct 1994  Dmitry Gorodchanin.
   */
  set_bit(TTY_DO_WRITE_WAKEUP, &port->tty->flags);
  actual = port->tty->ops->write(port->tty, port->xhead, port->xleft);
  port->xleft -= actual;
  port->xhead += actual;
}

static enum hrtimer_restart slcan_tx_timer_handler(struct hrtimer *t) {
  struct slcan_port *port = container_of(t, struct slcan_port, tx_timer);
  schedule_work(&port->tx_work);
  return HRTIMER_NORESTART;
}

/* Write out any remaining transmit buffer. Scheduled when tty is writable */
static void slcan_transmit(struct work_struct *work) {
  struct slcan_port *port = container_of(work, struct slcan_port, tx_work);
  int actual;
  int i;

  spin_lock_bh(&port->lock);
  /* If we are sending a command, we don't batch and we wake up the waiter */
  if (test_bit(SLF_XCMD, &port->flags)) {
    if (port->xleft <= 0) {
      clear_bit(SLF_XCMD, &port->flags);
      clear_bit(TTY_DO_WRITE_WAKEUP, &port->tty->flags);
      spin_unlock_bh(&port->lock);
      wake_up(&port->xcmd_wait);
      return;
    }
  } else {
    if (port->xleft <= 0) {
      clear_bit(TTY_DO_WRITE_WAKEUP, &port->tty->flags);
      port->xhead = port->xbuff;
      spin_unlock_bh(&port->lock);

      for (i = 0; i < SLCAN_MAX_CHANNELS; i++) {
        struct slcan *sl = port->channels[i];
        if (sl)
          netif_wake_queue(sl->dev);
      }
      return;
    }
  }

  actual = port->tty->ops->write(port->tty, port->xhead, port->xleft);
  port->xleft -= actual;
  port->xhead += actual;
  spin_unlock_bh(&port->lock);
}

/* Called by the driver when there's room for more data.
 * Schedule the transmit.
 */
static void slcan_write_wakeup(struct tty_struct *tty) {
  struct slcan_port *port = tty->disc_data;

  if (port)
    schedule_work(&port->tx_work);
}

/* Send a can_frame to a TTY queue. */
static netdev_tx_t slcan_netdev_xmit(struct sk_buff *skb,
                                     struct net_device *dev) {
  struct slcan *sl = netdev_priv(dev);
  struct slcan_port *port = sl->port;
  bool is_fd = can_is_canfd_skb(skb);

  if (can_dev_dropped_skb(dev, skb))
    return NETDEV_TX_OK;

  spin_lock(&port->lock);
  if (!netif_running(dev)) {
    spin_unlock(&port->lock);
    netdev_warn(dev, "xmit: iface is down\n");
    goto out;
  }
  if (!port->tty) {
    spin_unlock(&port->lock);
    goto out;
  }

  if (port->xleft > SLCAN_MTU - 100) {
    netif_stop_queue(sl->dev);
    spin_unlock(&port->lock);
    return NETDEV_TX_BUSY;
  }

  slcan_encaps(sl, (struct canfd_frame *)skb->data, is_fd);
  WRITE_ONCE(port->tx_chan, sl);
  spin_unlock(&port->lock);

  skb_tx_timestamp(skb);

out:
  kfree_skb(skb);
  return NETDEV_TX_OK;
}

/******************************************
 *   Routines looking at netdevice side.
 ******************************************/

static int slcan_transmit_cmd(struct slcan *sl, const unsigned char *cmd) {
  int ret, actual, n;
  struct slcan_port *port = sl->port;

  spin_lock(&port->lock);
  if (!port->tty) {
    spin_unlock(&port->lock);
    return -ENODEV;
  }

  n = 0;
  {
    bool need_prefix = sl->chan_idx > 0;
    bool new_chunk = true;
    const unsigned char *src = cmd;

    while (*src && n < sizeof(port->xbuff) - 1) {
      unsigned char ch = *src++;

      if (new_chunk && need_prefix) {
        port->xbuff[n++] = '0' + sl->chan_idx;
        if (n >= sizeof(port->xbuff) - 1)
          break;
      }

      new_chunk = false;
      port->xbuff[n++] = ch;
      if (ch == '\r')
        new_chunk = true;
    }
    port->xbuff[n] = '\0';
  }
  set_bit(TTY_DO_WRITE_WAKEUP, &port->tty->flags);
  actual = port->tty->ops->write(port->tty, port->xbuff, n);
  port->xleft = n - actual;
  port->xhead = port->xbuff + actual;
  WRITE_ONCE(port->tx_chan, sl);
  set_bit(SLF_XCMD, &port->flags);
  spin_unlock(&port->lock);
  ret = wait_event_interruptible_timeout(port->xcmd_wait,
                                         !test_bit(SLF_XCMD, &port->flags), HZ);
  clear_bit(SLF_XCMD, &port->flags);
  if (ret == -ERESTARTSYS)
    return ret;

  if (ret == 0)
    return -ETIMEDOUT;

  return 0;
}

static int slcan_configure_nominal(struct slcan *sl) {
  unsigned char cmd[SLCAN_MTU];
  int idx, err;

  idx = slcan_nominal_bitrate_index(sl->can.bittiming.bitrate);
  if (idx < 0) {
    netdev_err(sl->dev, "unsupported nominal bitrate %u\n",
               sl->can.bittiming.bitrate);
    return idx;
  }

  snprintf(cmd, sizeof(cmd), "C\rS%d\r", idx);
  err = slcan_transmit_cmd(sl, cmd);
  if (err)
    return err;

  return slcan_send_sample_point_cmd(sl, sl->can.bittiming.sample_point, false);
}

static int slcan_configure_fd(struct slcan *sl) {
  unsigned char cmd[SLCAN_MTU];
  int idx, err;

  idx = slcan_data_bitrate_index(sl->can.data_bittiming.bitrate);
  if (idx <= 0) {
    netdev_err(sl->dev, "unsupported data bitrate %u for FD mode\n",
               sl->can.data_bittiming.bitrate);
    return idx < 0 ? idx : -EINVAL;
  }

  snprintf(cmd, sizeof(cmd), "Y%c\r", hex_asc_upper[idx & 0xF]);
  err = slcan_transmit_cmd(sl, cmd);
  if (err)
    return err;

  return slcan_send_sample_point_cmd(sl, sl->can.data_bittiming.sample_point,
                                     true);
}

/* Netdevice UP -> DOWN routine */
static int slcan_netdev_close(struct net_device *dev) {
  struct slcan *sl = netdev_priv(dev);
  struct slcan_port *port = sl->port;
  int err;

  if (sl->can.bittiming.bitrate &&
      sl->can.bittiming.bitrate != CAN_BITRATE_UNKNOWN) {
    err = slcan_transmit_cmd(sl, "C\r");
    if (err)
      netdev_warn(dev, "failed to send close command 'C\\r'\n");
  }

  /* TTY discipline is running. */
  if (port && port->tty)
    clear_bit(TTY_DO_WRITE_WAKEUP, &port->tty->flags);
  if (port)
    flush_work(&port->tx_work);

  netif_stop_queue(dev);
  if (port) {
    port->rcount = 0;
    port->xleft = 0;
  }
  close_candev(dev);
  sl->can.state = CAN_STATE_STOPPED;
  if (sl->can.bittiming.bitrate == CAN_BITRATE_UNKNOWN)
    sl->can.bittiming.bitrate = CAN_BITRATE_UNSET;

  return 0;
}

/* Netdevice DOWN -> UP routine */
static int slcan_netdev_open(struct net_device *dev) {
  struct slcan *sl = netdev_priv(dev);
  int err;

  /* The baud rate is not set with the command
   * `ip link set <iface> type can bitrate <baud>' and therefore
   * can.bittiming.bitrate is CAN_BITRATE_UNSET (0), causing
   * open_candev() to fail. So let's set to a fake value.
   */
  if (sl->can.bittiming.bitrate == CAN_BITRATE_UNSET)
    sl->can.bittiming.bitrate = CAN_BITRATE_UNKNOWN;

  err = open_candev(dev);
  if (err) {
    netdev_err(dev, "failed to open can device\n");
    return err;
  }

  if (sl->can.bittiming.bitrate != CAN_BITRATE_UNKNOWN &&
      sl->can.bittiming.bitrate != CAN_BITRATE_UNSET) {
    err = slcan_configure_nominal(sl);
    if (err)
      goto cmd_transmit_failed;

    if (sl->can.ctrlmode & CAN_CTRLMODE_FD) {
      if (sl->can.data_bittiming.bitrate == CAN_BITRATE_UNSET ||
          sl->can.data_bittiming.bitrate == CAN_BITRATE_UNKNOWN ||
          !sl->can.data_bittiming.bitrate) {
        netdev_err(dev, "CAN FD requested without data bitrate\n");
        err = -EINVAL;
        goto cmd_transmit_failed;
      }

      err = slcan_configure_fd(sl);
      if (err)
        goto cmd_transmit_failed;
    } else {
      err = slcan_transmit_cmd(sl, "Y0\r");
      if (err)
        goto cmd_transmit_failed;
    }

    if (test_bit(CF_ERR_RST, &sl->cmd_flags)) {
      err = slcan_transmit_cmd(sl, "F\r");
      if (err) {
        netdev_err(dev, "failed to send error command 'F\\r'\n");
        goto cmd_transmit_failed;
      }
    }

    if (sl->can.ctrlmode & CAN_CTRLMODE_LISTENONLY) {
      err = slcan_transmit_cmd(sl, "L\r");
      if (err) {
        netdev_err(dev, "failed to send listen-only command 'L\\r'\n");
        goto cmd_transmit_failed;
      }
    } else {
      err = slcan_transmit_cmd(sl, "O\r");
      if (err) {
        netdev_err(dev, "failed to send open command 'O\\r'\n");
        goto cmd_transmit_failed;
      }
    }
  }

  sl->can.state = CAN_STATE_ERROR_ACTIVE;
  netif_start_queue(dev);
  return 0;

cmd_transmit_failed:
  close_candev(dev);
  return err;
}

static const struct net_device_ops slcan_netdev_ops = {
    .ndo_open = slcan_netdev_open,
    .ndo_stop = slcan_netdev_close,
    .ndo_start_xmit = slcan_netdev_xmit,
    .ndo_change_mtu = can_change_mtu,
};

/******************************************
 *  Routines looking at TTY side.
 ******************************************/

/* Handle the 'receiver data ready' interrupt.
 * This function is called by the 'tty_io' module in the kernel when
 * a block of SLCAN data has been received, which can now be decapsulated
 * and sent on to some IP layer for further processing. This will not
 * be re-entered while running but other ldisc functions may be called
 * in parallel
 */
static void slcan_receive_buf(struct tty_struct *tty, const unsigned char *cp,
                              const char *fp, int count) {
  struct slcan_port *port = tty->disc_data;
  struct slcan *sl = NULL;

  if (!port)
    return;

  sl = slcan_first_channel(port);
  if (!sl)
    return;

  /* Read the characters out of the buffer */
  if (!fp) {
    while (count--)
      slcan_unesc(port, *cp++);
  } else {
    while (count--) {
      if (*fp++) {
        if (!test_and_set_bit(SLF_ERROR, &port->flags))
          sl->dev->stats.rx_errors++;
        cp++;
        continue;
      }
      slcan_unesc(port, *cp++);
    }
  }
}

/* Open the high-level part of the SLCAN channel.
 * This function is called by the TTY module when the
 * SLCAN line discipline is called for.
 *
 * Called in process context serialized from other ldisc calls.
 */
static int slcan_open(struct tty_struct *tty) {
  struct slcan_port *port;
  int err, i;

  if (!capable(CAP_NET_ADMIN))
    return -EPERM;

  if (!tty->ops->write)
    return -EOPNOTSUPP;

  port = kzalloc(sizeof(*port), GFP_KERNEL);
  if (!port)
    return -ENOMEM;

  spin_lock_init(&port->lock);
  INIT_WORK(&port->tx_work, slcan_transmit);
  hrtimer_init(&port->tx_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
  port->tx_timer.function = slcan_tx_timer_handler;
  init_waitqueue_head(&port->xcmd_wait);
  port->tty = tty;
  port->rcount = 0;
  port->xleft = 0;
  port->tx_chan = NULL;

  /* Configure TTY interface */
  tty->receive_room = 65536; /* We don't flow control */
  tty->disc_data = port;

  for (i = 0; i < SLCAN_MAX_CHANNELS; i++) {
    struct net_device *dev;
    struct slcan *sl;

    dev = alloc_candev(sizeof(*sl), 1);
    if (!dev) {
      err = -ENFILE;
      goto err_register;
    }

    sl = netdev_priv(dev);
    memset(sl, 0, sizeof(*sl));
    sl->port = port;
    sl->dev = dev;
    sl->chan_idx = i;
    port->channels[i] = sl;

    sl->can.bitrate_const = slcan_bitrate_const;
    sl->can.bitrate_const_cnt = ARRAY_SIZE(slcan_bitrate_const);
    sl->can.data_bitrate_const = &slcan_data_bitrate_const[1];
    sl->can.data_bitrate_const_cnt = ARRAY_SIZE(slcan_data_bitrate_const) - 1;
    sl->can.ctrlmode_supported = CAN_CTRLMODE_LISTENONLY | CAN_CTRLMODE_FD;

    dev->netdev_ops = &slcan_netdev_ops;
    dev->ethtool_ops = &slcan_ethtool_ops;
    dev->mtu = CANFD_MTU;

    err = register_candev(dev);
    if (err) {
      port->channels[i] = NULL;
      free_candev(dev);
      goto err_register;
    }

    netdev_info(dev, "slcan ch%u on %s.\n", i, tty->name);
  }
  /* TTY layer expects 0 on success */
  return 0;

err_register:
  while (--i >= 0) {
    struct slcan *sl = port->channels[i];

    if (!sl)
      continue;

    unregister_candev(sl->dev);
    free_candev(sl->dev);
    port->channels[i] = NULL;
  }
  spin_lock_bh(&port->lock);
  tty->disc_data = NULL;
  port->tty = NULL;
  spin_unlock_bh(&port->lock);
  kfree(port);
  return err;
}

/* Close down a SLCAN channel.
 * This means flushing out any pending queues, and then returning. This
 * call is serialized against other ldisc functions.
 * Once this is called, no other ldisc function of ours is entered.
 *
 * We also use this method for a hangup event.
 */
static void slcan_close(struct tty_struct *tty) {
  struct slcan_port *port = tty->disc_data;
  int i;

  if (!port)
    return;

  hrtimer_cancel(&port->tx_timer);
  flush_work(&port->tx_work);

  for (i = 0; i < SLCAN_MAX_CHANNELS; i++) {
    if (port->channels[i])
      unregister_candev(port->channels[i]->dev);
  }

  flush_work(&port->tx_work);

  spin_lock_bh(&port->lock);
  tty->disc_data = NULL;
  port->tty = NULL;
  spin_unlock_bh(&port->lock);

  for (i = 0; i < SLCAN_MAX_CHANNELS; i++) {
    struct slcan *sl = port->channels[i];

    if (!sl)
      continue;

    netdev_info(sl->dev, "slcan off %s.\n", tty->name);
    free_candev(sl->dev);
    port->channels[i] = NULL;
  }

  kfree(port);
}

/* Perform I/O control on an active SLCAN channel. */
static int slcan_ioctl(struct tty_struct *tty, struct file *file,
                       unsigned int cmd, unsigned long arg) {
  struct slcan_port *port = tty->disc_data;
  struct slcan *sl = NULL;
  unsigned int tmp;

  if (!port)
    return -ENODEV;

  sl = slcan_first_channel(port);
  if (!sl)
    return -ENODEV;

  switch (cmd) {
  case SIOCGIFNAME:
    tmp = strlen(sl->dev->name) + 1;
    if (copy_to_user((void __user *)arg, sl->dev->name, tmp))
      return -EFAULT;
    return 0;

  case SIOCSIFHWADDR:
    return -EINVAL;

  default:
    return tty_mode_ioctl(tty, file, cmd, arg);
  }
}

static struct tty_ldisc_ops slcan_ldisc = {
    .owner = THIS_MODULE,
    .num = N_SLCAN,
    .name = KBUILD_MODNAME,
    .open = slcan_open,
    .close = slcan_close,
    .ioctl = slcan_ioctl,
    .receive_buf = slcan_receive_buf,
    .write_wakeup = slcan_write_wakeup,
};

static int __init slcan_init(void) {
  int status;

  pr_info("serial line CAN interface driver\n");

  /* Fill in our line protocol discipline, and register it */
  status = tty_register_ldisc(&slcan_ldisc);
  if (status)
    pr_err("can't register line discipline\n");

  return status;
}

static void __exit slcan_exit(void) {
  /* This will only be called when all channels have been closed by
   * userspace - tty_ldisc.c takes care of the module's refcount.
   */
  tty_unregister_ldisc(&slcan_ldisc);
}

module_init(slcan_init);
module_exit(slcan_exit);
