/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * slcanfd.c - userspace daemon for serial line CAN FD interface driver SLCAN
 *
 * Based on slcand.c from can-utils
 * Copyright (c) 2009 Robert Haddon <robert.haddon@verari.com>
 * Copyright (c) 2009 Verari Systems Inc.
 * CAN FD extensions by weifengdq
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Send feedback to <linux-can@vger.kernel.org>
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/serial.h>
#include <linux/sockios.h>
#include <linux/tty.h>
#include <net/if.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <termios.h>
#include <unistd.h>

/* Change this to whatever your daemon is called */
#define DAEMON_NAME "slcanfd"

/* Change this to the user under which to run */
#define RUN_AS_USER "root"

/* The length of ttypath buffer */
#define TTYPATH_LENGTH 256

/* UART flow control types */
#define FLOW_NONE 0
#define FLOW_HW 1
#define FLOW_SW 2

static void fake_syslog(int priority, const char *format, ...)
{
	va_list ap;

	printf("[%d] ", priority);
	va_start(ap, format);
	vprintf(format, ap);
	va_end(ap);
	printf("\n");
}

typedef void (*syslog_t)(int priority, const char *format, ...);
static syslog_t syslogger = syslog;

void print_usage(char *prg)
{
	fprintf(stderr, "%s - userspace daemon for serial line CAN FD interface driver SLCAN.\n", prg);
	fprintf(stderr, "\nUsage: %s [options] <tty> [canif-name]\n\n", prg);
	fprintf(stderr, "Options (prefix with -[0-3] for channel, -[no prefix] defaults to channel 0):\n");
	fprintf(stderr, "         -[ch]o        (send open command; e.g., -0o, -1o, -o for channel 0)\n");
	fprintf(stderr, "         -[ch]c        (send close command on exit)\n");
	fprintf(stderr, "         -[ch]f        (read status flags to reset error states)\n");
	fprintf(stderr, "         -[ch]l[0|1]   (listen-only mode: -l/-l1=enable, -l0=disable)\n");
	fprintf(stderr, "         -[ch]s <idx>  (set nominal speed index 0..8)\n");
	fprintf(stderr, "                       0=10k 1=20k 2=50k 3=100k 4=125k 5=250k 6=500k 7=800k 8=1000k\n");
	fprintf(stderr, "         -[ch]y <baud> (set nominal speed numeric 5000..1000000)\n");
	fprintf(stderr, "         -[ch]Y <idx>  (set CANFD data speed 0..F: 0=disable, 1=1M..F=16M)\n");
	fprintf(stderr, "         -[ch]q        (query nominal CAN config)\n");
	fprintf(stderr, "         -[ch]Q        (query CANFD data config)\n");
	fprintf(stderr, "         -[ch]N        (query device UUID/serial)\n");
	fprintf(stderr, "         -[ch]p <sp>   (set classic sample point *10, range 750..875)\n");
	fprintf(stderr, "         -[ch]P <sp>   (set CANFD sample point *10, range 750..875)\n");
	fprintf(stderr, "         -[ch]a <timing> (custom classic timing CLK_PRE_SEG1_SEG2_SJW_TDC)\n");
	fprintf(stderr, "         -[ch]A <timing> (custom CANFD timing CLK_PRE_SEG1_SEG2_SJW_TDC)\n");
	fprintf(stderr, "         -S <speed>    (set UART speed in baud)\n");
	fprintf(stderr, "         -t <type>     (set UART flow control: 'hw' or 'sw')\n");
	fprintf(stderr, "         -[ch]b <btr>  (set legacy bit timing register)\n");
	fprintf(stderr, "         -F            (stay in foreground; no daemonize)\n");
	fprintf(stderr, "         -h            (show this help page)\n");
	fprintf(stderr, "\nNotes:\n");
	fprintf(stderr, "  Protocol supports 4 CAN channels (0-3) sharing one serial port.\n");
	fprintf(stderr, "  Use -[0-3] prefix before commands to specify channel (default: 0).\n");
	fprintf(stderr, "  Timing strings need exactly 5 underscores (6 fields).\n");
	fprintf(stderr, "\nExamples:\n");
	fprintf(stderr, "  Single channel (default ch0):\n");
	fprintf(stderr, "    %s -o -c -f -s6 -Y2 ttyUSB0 can0\n", prg);
	fprintf(stderr, "  Multi-channel config (ch0 creates netdev, others configured):\n");
	fprintf(stderr, "    %s -0s6 -0o -1s4 -1o -2s6 -2Y4 -2o -3y250000 -3o ttyUSB0 can0\n", prg);
	fprintf(stderr, "\n");
	exit(EXIT_FAILURE);
}

static int slcanfd_running;
static volatile sig_atomic_t exit_code;
static char ttypath[TTYPATH_LENGTH];

/* 读取直到获得以 expect 开头且以 CR 结束的一行; 过滤其它杂散行/空 CR. */
static int read_slcan_reply(int fd, char expect, char *out, size_t outlen, int timeout_ms)
{
	if (!out || outlen == 0)
		return -1;
	size_t pos = 0;
	int elapsed = 0;
	const int slice_ms = 50; /* select 片段 */
	while (elapsed < timeout_ms) {
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		struct timeval tv;
		tv.tv_sec = slice_ms / 1000;
		tv.tv_usec = (slice_ms % 1000) * 1000;
		int r = select(fd + 1, &rfds, NULL, NULL, &tv);
		if (r < 0)
			return -2; /* select error */
		if (r == 0) {
			elapsed += slice_ms;
			continue;
		}
		char ch;
		int n = read(fd, &ch, 1);
		if (n <= 0)
			return -3;
		if (ch == '\r') { /* 行结束 */
			if (pos > 0) { /* 有内容 */
				out[pos] = '\0';
				if (out[0] == expect)
					return (int)pos;
			}
			pos = 0; /* 重置继续找 */
			continue;
		}
		if (pos < outlen - 1)
			out[pos++] = ch;
		else { /* overflow -> 丢弃此行 */
			pos = 0;
		}
	}
	return -4; /* timeout 无匹配 */
}

static void parse_and_print_config_line(char prefix, const char *line)
{
	/* line 已去掉首字母, 不包含 CR */
	if (!line)
		return;
	const char *us = strchr(line, '_');
	if (!us) {
		syslogger(LOG_INFO, "%c raw:%s", prefix, line);
		printf("%c %s\n", prefix, line);
		return;
	}
	char baud[32];
	char sp[16];
	size_t blen = (size_t)(us - line);
	if (blen >= sizeof(baud))
		blen = sizeof(baud) - 1;
	memcpy(baud, line, blen);
	baud[blen] = '\0';
	strncpy(sp, us + 1, sizeof(sp) - 1);
	sp[sizeof(sp) - 1] = '\0';
	syslogger(LOG_INFO, "%c baud=%s sample_point=%s/1000", prefix, baud, sp);
}

static void child_handler(int signum)
{
	switch (signum) {
	case SIGUSR1:
		/* exit parent */
		exit(EXIT_SUCCESS);
		break;
	case SIGINT:
	case SIGTERM:
	case SIGALRM:
	case SIGCHLD:
		syslogger(LOG_NOTICE, "received signal %i on %s", signum, ttypath);
		exit_code = 128 + signum;
		slcanfd_running = 0;
		break;
	}
}

static int look_up_uart_speed(long int s)
{
	switch (s) {
	case 9600:
		return B9600;
	case 19200:
		return B19200;
	case 38400:
		return B38400;
	case 57600:
		return B57600;
	case 115200:
		return B115200;
	case 230400:
		return B230400;
	case 460800:
		return B460800;
	case 500000:
		return B500000;
	case 576000:
		return B576000;
	case 921600:
		return B921600;
	case 1000000:
		return B1000000;
	case 1152000:
		return B1152000;
	case 1500000:
		return B1500000;
	case 2000000:
		return B2000000;
#ifdef B2500000
	case 2500000:
		return B2500000;
#endif
#ifdef B3000000
	case 3000000:
		return B3000000;
#endif
#ifdef B3500000
	case 3500000:
		return B3500000;
#endif
#ifdef B3710000
	case 3710000:
		return B3710000;
#endif
#ifdef B4000000
	case 4000000:
		return B4000000;
#endif
	default:
		return -1;
	}
}

/* Convert CAN FD data speed character to description 
 * Note: index mapping per new protocol - 0=disable FD, 1-15 = 1-16MHz (index F=16M not 15M)
 */
static const char *canfd_speed_to_string(char speed_char)
{
	switch (speed_char) {
	case '0':
		return "0M (FD disabled)";
	case '1':
		return "1M";
	case '2':
		return "2M";
	case '3':
		return "3M";
	case '4':
		return "4M";
	case '5':
		return "5M";
	case '6':
		return "6M";
	case '7':
		return "7M";
	case '8':
		return "8M";
	case '9':
		return "9M";
	case 'A':
	case 'a':
		return "10M";
	case 'B':
	case 'b':
		return "11M";
	case 'C':
	case 'c':
		return "12M";
	case 'D':
	case 'd':
		return "13M";
	case 'E':
	case 'e':
		return "14M";
	case 'F':
	case 'f':
		return "16M";
	default:
		return "unknown";
	}
}

/* Validate CAN FD data speed parameter (index 0-F, but 1-F for 1-16M, 0 disables FD) */
static int validate_canfd_speed(char speed_char)
{
	switch (speed_char) {
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
	case 'A':
	case 'a':
	case 'B':
	case 'b':
	case 'C':
	case 'c':
	case 'D':
	case 'd':
	case 'E':
	case 'e':
	case 'F':
	case 'f':
		return 1;
	default:
		return 0;
	}
}

/* Command queue entry for multi-channel support */
typedef struct {
	int channel;
	char cmd;
	char *arg;
} cmd_entry_t;

#define MAX_COMMANDS 128

int main(int argc, char *argv[])
{
	char *tty = NULL;
	char const *devprefix = "/dev/";
	char *name = NULL;
	char buf[256];
	static struct ifreq ifr;
	struct termios tios;
	speed_t old_ispeed;
	speed_t old_ospeed;

	char *uart_speed_str = NULL;
	long int uart_speed = 0;
	int flow_type = FLOW_NONE;
	int run_as_daemon = 1;
	char *pch;
	int ldisc = N_SLCAN;
	int fd;

	/* Command queue for all channels */
	cmd_entry_t commands[MAX_COMMANDS];
	int cmd_count = 0;
	int first_non_option = -1; /* Track position of tty device argument */

	ttypath[0] = '\0';

	if (argc > 1 && strcmp(argv[1], "clean") == 0) {
		printf("Cleaning up slcandx processes...\n");
		system("killall slcandx");
		return 0;
	}

	/* Manual argument parsing to support -[0-3]command format */
	for (int i = 1; i < argc; i++) {
		char *arg = argv[i];
		
		/* Skip if not an option - remember first non-option position */
		if (arg[0] != '-') {
			if (first_non_option < 0)
				first_non_option = i;
			continue;
		}
		
		/* Check for channel prefix: -[0-3]command */
		int ch = 0;  /* default channel 0 */
		int cmd_start = 1;
		
		if (arg[1] >= '0' && arg[1] <= '3') {
			ch = arg[1] - '0';
			cmd_start = 2;
			if (arg[2] == '\0') {
				fprintf(stderr, "Error: Channel prefix requires a command (e.g., -0s6)\n");
				exit(EXIT_FAILURE);
			}
		}
		
		char cmd = arg[cmd_start];
		
		/* Handle global options (no channel prefix allowed) */
		if (cmd == 'S') {
			if (cmd_start != 1) {
				fprintf(stderr, "Error: -S (UART speed) cannot have channel prefix\n");
				exit(EXIT_FAILURE);
			}
			if (i + 1 >= argc) {
				fprintf(stderr, "Error: -S requires an argument\n");
				exit(EXIT_FAILURE);
			}
			uart_speed_str = argv[++i];
			errno = 0;
			uart_speed = strtol(uart_speed_str, NULL, 10);
			if (errno || look_up_uart_speed(uart_speed) == -1) {
				fprintf(stderr, "Unsupported UART speed (%s)\n", uart_speed_str);
				exit(EXIT_FAILURE);
			}
			continue;
		}
		if (cmd == 't') {
			if (cmd_start != 1) {
				fprintf(stderr, "Error: -t (flow control) cannot have channel prefix\n");
				exit(EXIT_FAILURE);
			}
			if (i + 1 >= argc) {
				fprintf(stderr, "Error: -t requires an argument\n");
				exit(EXIT_FAILURE);
			}
			char *flow = argv[++i];
			if (!strcmp(flow, "hw"))
				flow_type = FLOW_HW;
			else if (!strcmp(flow, "sw"))
				flow_type = FLOW_SW;
			else {
				fprintf(stderr, "Unsupported flow type (%s)\n", flow);
				exit(EXIT_FAILURE);
			}
			continue;
		}
		if (cmd == 'F') {
			if (cmd_start != 1) {
				fprintf(stderr, "Error: -F (foreground) cannot have channel prefix\n");
				exit(EXIT_FAILURE);
			}
			run_as_daemon = 0;
			continue;
		}
		if (cmd == 'h' || cmd == '?') {
			print_usage(argv[0]);
		}
		
		/* Channel-specific commands - add to queue */
		if (cmd_count >= MAX_COMMANDS) {
			fprintf(stderr, "Error: Too many commands (max %d)\n", MAX_COMMANDS);
			exit(EXIT_FAILURE);
		}
		
		commands[cmd_count].channel = ch;
		commands[cmd_count].cmd = cmd;
		commands[cmd_count].arg = NULL;
		
		/* Check if command requires an argument */
		if (cmd == 's' || cmd == 'y' || cmd == 'Y' || cmd == 'p' || cmd == 'P' ||
		    cmd == 'a' || cmd == 'A' || cmd == 'b') {
			/* Argument can be concatenated or separate */
			if (arg[cmd_start + 1] != '\0') {
				/* Concatenated: -0s6 or -s6 */
				commands[cmd_count].arg = &arg[cmd_start + 1];
			} else {
				/* Separate: -0s 6 or -s 6 */
				if (i + 1 >= argc) {
					fprintf(stderr, "Error: -%c requires an argument\n", cmd);
					exit(EXIT_FAILURE);
				}
				commands[cmd_count].arg = argv[++i];
			}
		} else if (cmd == 'l') {
			/* Special handling for -l[0|1] */
			if (arg[cmd_start + 1] != '\0') {
				commands[cmd_count].arg = &arg[cmd_start + 1];
			}
			/* else: no arg means default (enable listen-only) */
		}
		
		cmd_count++;
	}

	if (!run_as_daemon)
		syslogger = fake_syslog;

	/* Initialize the logging interface */
	openlog(DAEMON_NAME, LOG_PID, LOG_LOCAL5);

	/* Parse serial device name and optional can interface name */
	if (first_non_option < 0) {
		fprintf(stderr, "Error: Missing TTY device path\n");
		print_usage(argv[0]);
	}
	
	tty = argv[first_non_option];
	if (NULL == tty)
		print_usage(argv[0]);

	name = (first_non_option + 1 < argc && argv[first_non_option + 1][0] != '-') 
	       ? argv[first_non_option + 1] : NULL;
	if (name && (strlen(name) > sizeof(ifr.ifr_newname) - 1))
		print_usage(argv[0]);

	/* Prepare the tty device name string */
	pch = strstr(tty, devprefix);
	if (pch != tty)
		snprintf(ttypath, TTYPATH_LENGTH, "%s%s", devprefix, tty);
	else
		snprintf(ttypath, TTYPATH_LENGTH, "%s", tty);

	syslogger(LOG_INFO, "starting on TTY device %s", ttypath);

	fd = open(ttypath, O_RDWR | O_NONBLOCK | O_NOCTTY);
	if (fd < 0) {
		syslogger(LOG_NOTICE, "failed to open TTY device %s\n", ttypath);
		perror(ttypath);
		exit(EXIT_FAILURE);
	}

	/* Configure baud rate */
	memset(&tios, 0, sizeof(tios));
	if (tcgetattr(fd, &tios) < 0) {
		syslogger(LOG_NOTICE, "failed to get attributes for TTY device %s: %s\n", ttypath, strerror(errno));
		exit(EXIT_FAILURE);
	}

	// Because of a recent change in linux - https://patchwork.kernel.org/patch/9589541/
	// we need to set low latency flag to get proper receive latency
	struct serial_struct snew;
	ioctl(fd, TIOCGSERIAL, &snew);
	snew.flags |= ASYNC_LOW_LATENCY;
	ioctl(fd, TIOCSSERIAL, &snew);

	/* Get old values for later restore */
	old_ispeed = cfgetispeed(&tios);
	old_ospeed = cfgetospeed(&tios);

	/* Reset UART settings */
	cfmakeraw(&tios);
	tios.c_iflag &= ~IXOFF;
	tios.c_cflag &= ~CRTSCTS;

	/* Baud Rate */
	cfsetispeed(&tios, look_up_uart_speed(uart_speed));
	cfsetospeed(&tios, look_up_uart_speed(uart_speed));

	/* Flow control */
	if (flow_type == FLOW_HW)
		tios.c_cflag |= CRTSCTS;
	else if (flow_type == FLOW_SW)
		tios.c_iflag |= (IXON | IXOFF);

	/* apply changes */
	if (tcsetattr(fd, TCSADRAIN, &tios) < 0)
		syslogger(LOG_NOTICE, "Cannot set attributes for device \"%s\": %s!\n", ttypath, strerror(errno));

	/* Execute all queued commands */
	for (int i = 0; i < cmd_count; i++) {
		cmd_entry_t *c = &commands[i];
		int n = 0;
		
		switch (c->cmd) {
		case 'c': /* Close */
			n = snprintf(buf, sizeof(buf), "%dC\r", c->channel);
			if (write(fd, buf, n) > 0)
				syslogger(LOG_INFO, "Channel %d: Close", c->channel);
			break;
			
		case 'o': /* Open */
			n = snprintf(buf, sizeof(buf), "%dO\r", c->channel);
			if (write(fd, buf, n) > 0)
				syslogger(LOG_INFO, "Channel %d: Open", c->channel);
			break;
			
		case 'f': /* Read status flags */
			n = snprintf(buf, sizeof(buf), "%dF\r", c->channel);
			if (write(fd, buf, n) > 0)
				syslogger(LOG_INFO, "Channel %d: Read status flags", c->channel);
			break;
			
		case 'l': /* Listen-only mode */
			if (!c->arg || (c->arg[0] == '1' && c->arg[1] == '\0'))
				n = snprintf(buf, sizeof(buf), "%dL\r", c->channel);
			else if (c->arg[0] == '0' && c->arg[1] == '\0')
				n = snprintf(buf, sizeof(buf), "%dL0\r", c->channel);
			else {
				fprintf(stderr, "Invalid -l argument: %s\n", c->arg);
				exit(EXIT_FAILURE);
			}
			if (write(fd, buf, n) > 0)
				syslogger(LOG_INFO, "Channel %d: Listen-only %s", c->channel, 
				          (!c->arg || c->arg[0] == '1') ? "enabled" : "disabled");
			break;
			
		case 's': /* Speed index */
			if (!c->arg || strlen(c->arg) != 1 || c->arg[0] < '0' || c->arg[0] > '8') {
				fprintf(stderr, "Invalid speed index: %s (valid: 0-8)\n", c->arg);
				exit(EXIT_FAILURE);
			}
			n = snprintf(buf, sizeof(buf), "%dS%s\r", c->channel, c->arg);
			if (write(fd, buf, n) > 0)
				syslogger(LOG_INFO, "Channel %d: Set speed index %s", c->channel, c->arg);
			break;
			
		case 'y': { /* Numeric speed */
			long v = strtol(c->arg, NULL, 10);
			if (v < 5000 || v > 1000000) {
				fprintf(stderr, "Invalid speed %s (valid: 5000-1000000)\n", c->arg);
				exit(EXIT_FAILURE);
			}
			n = snprintf(buf, sizeof(buf), "%dy%ld\r", c->channel, v);
			if (write(fd, buf, n) > 0)
				syslogger(LOG_INFO, "Channel %d: Set speed %ld bps", c->channel, v);
			break;
		}
		
		case 'Y': /* CANFD data speed */
			if (!c->arg || strlen(c->arg) != 1 || !validate_canfd_speed(c->arg[0])) {
				fprintf(stderr, "Invalid CANFD speed: %s (valid: 0-9,A-F)\n", c->arg);
				exit(EXIT_FAILURE);
			}
			n = snprintf(buf, sizeof(buf), "%dY%s\r", c->channel, c->arg);
			if (write(fd, buf, n) > 0)
				syslogger(LOG_INFO, "Channel %d: Set CANFD speed %s (%s)", c->channel, 
				          c->arg, canfd_speed_to_string(c->arg[0]));
			break;
			
		case 'p': { /* Classic sample point */
			long sp = strtol(c->arg, NULL, 10);
			if (sp < 750 || sp > 875) {
				fprintf(stderr, "Invalid sample point: %s (valid: 750-875)\n", c->arg);
				exit(EXIT_FAILURE);
			}
			n = snprintf(buf, sizeof(buf), "%dp%ld\r", c->channel, sp);
			if (write(fd, buf, n) > 0)
				syslogger(LOG_INFO, "Channel %d: Set classic SP %ld/1000", c->channel, sp);
			break;
		}
		
		case 'P': { /* CANFD sample point */
			long sp = strtol(c->arg, NULL, 10);
			if (sp < 750 || sp > 875) {
				fprintf(stderr, "Invalid CANFD sample point: %s (valid: 750-875)\n", c->arg);
				exit(EXIT_FAILURE);
			}
			n = snprintf(buf, sizeof(buf), "%dP%ld\r", c->channel, sp);
			if (write(fd, buf, n) > 0)
				syslogger(LOG_INFO, "Channel %d: Set CANFD SP %ld/1000", c->channel, sp);
			break;
		}
		
		case 'a': { /* Classic timing */
			int us = 0;
			for (const char *p = c->arg; *p; ++p)
				if (*p == '_') us++;
			if (us != 5) {
				fprintf(stderr, "Invalid timing (need 6 fields): %s\n", c->arg);
				exit(EXIT_FAILURE);
			}
			n = snprintf(buf, sizeof(buf), "%da%s\r", c->channel, c->arg);
			if (write(fd, buf, n) > 0)
				syslogger(LOG_INFO, "Channel %d: Set classic timing %s", c->channel, c->arg);
			break;
		}
		
		case 'A': { /* CANFD timing */
			int us = 0;
			for (const char *p = c->arg; *p; ++p)
				if (*p == '_') us++;
			if (us != 5) {
				fprintf(stderr, "Invalid CANFD timing (need 6 fields): %s\n", c->arg);
				exit(EXIT_FAILURE);
			}
			n = snprintf(buf, sizeof(buf), "%dA%s\r", c->channel, c->arg);
			if (write(fd, buf, n) > 0)
				syslogger(LOG_INFO, "Channel %d: Set CANFD timing %s", c->channel, c->arg);
			break;
		}
		
		case 'b': /* Legacy BTR */
			if (strlen(c->arg) > 8) {
				fprintf(stderr, "BTR too long: %s\n", c->arg);
				exit(EXIT_FAILURE);
			}
			n = snprintf(buf, sizeof(buf), "%ds%s\r", c->channel, c->arg);
			if (write(fd, buf, n) > 0)
				syslogger(LOG_INFO, "Channel %d: Set BTR %s", c->channel, c->arg);
			break;
			
		case 'q': { /* Query nominal */
			n = snprintf(buf, sizeof(buf), "%dq\r", c->channel);
			if (write(fd, buf, n) <= 0) break;
			char rbuf[128];
			int rl = read_slcan_reply(fd, 'q', rbuf, sizeof(rbuf), 500);
			if (rl > 0) {
				rbuf[rl] = '\0';
				syslogger(LOG_INFO, "Channel %d nominal: %s", c->channel, rbuf + 1);
				printf("Channel %d: ", c->channel);
				parse_and_print_config_line('q', rbuf + 1);
			}
			break;
		}
		
		case 'Q': { /* Query CANFD */
			n = snprintf(buf, sizeof(buf), "%dQ\r", c->channel);
			if (write(fd, buf, n) <= 0) break;
			char rbuf2[128];
			int rl2 = read_slcan_reply(fd, 'Q', rbuf2, sizeof(rbuf2), 500);
			if (rl2 > 0) {
				rbuf2[rl2] = '\0';
				syslogger(LOG_INFO, "Channel %d CANFD: %s", c->channel, rbuf2 + 1);
				printf("Channel %d: ", c->channel);
				parse_and_print_config_line('Q', rbuf2 + 1);
			}
			break;
		}
		
		case 'N': { /* Query UUID */
			n = snprintf(buf, sizeof(buf), "%dN\r", c->channel);
			if (write(fd, buf, n) <= 0) break;
			char rbuf3[128];
			int rl3 = read_slcan_reply(fd, 'N', rbuf3, sizeof(rbuf3), 500);
			if (rl3 > 0) {
				rbuf3[rl3] = '\0';
				syslogger(LOG_INFO, "Channel %d UUID: %s", c->channel, rbuf3 + 1);
				printf("Channel %d UUID: %s\n", c->channel, rbuf3 + 1);
			}
			break;
		}
		
		default:
			fprintf(stderr, "Unknown command: %c\n", c->cmd);
			break;
		}
		
		usleep(10000); /* 10ms delay between commands */
	}

	/* set slcan like discipline on given tty */
	if (ioctl(fd, TIOCSETD, &ldisc) < 0) {
		perror("ioctl TIOCSETD");
		exit(EXIT_FAILURE);
	}

	/* retrieve the name of the created CAN netdevice */
	if (ioctl(fd, SIOCGIFNAME, ifr.ifr_name) < 0) {
		if (name) {
			perror("ioctl SIOCGIFNAME");
			exit(EXIT_FAILURE);
		} else {
			/* Graceful degradation: we only needed the name for display. */
			snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "<unknown>");
		}
	}

	syslogger(LOG_NOTICE, "attached TTY %s to netdevice %s\n", ttypath, ifr.ifr_name);

	/* try to rename the created netdevice */
	if (name) {
		int s = socket(PF_INET, SOCK_DGRAM, 0);

		if (s < 0)
			perror("socket for interface rename");
		else {
			/* current slcan%d name is still in ifr.ifr_name */
			memset(ifr.ifr_newname, 0, sizeof(ifr.ifr_newname));
			strncpy(ifr.ifr_newname, name, sizeof(ifr.ifr_newname) - 1);

			if (ioctl(s, SIOCSIFNAME, &ifr) < 0) {
				syslogger(LOG_NOTICE, "netdevice %s rename to %s failed\n", buf, name);
				perror("ioctl SIOCSIFNAME rename");
				exit(EXIT_FAILURE);
			} else
				syslogger(LOG_NOTICE, "netdevice %s renamed to %s\n", buf, name);

			close(s);
		}
	}

	/* Daemonize */
	if (run_as_daemon) {
		if (daemon(0, 0)) {
			syslogger(LOG_ERR, "failed to daemonize");
			exit(EXIT_FAILURE);
		}
	} else {
		/* Trap signals that we expect to receive */
		signal(SIGINT, child_handler);
		signal(SIGTERM, child_handler);
	}

	slcanfd_running = 1;

	/* The Big Loop */
	while (slcanfd_running)
		sleep(1); /* wait 1 second */

	/* Reset line discipline */
	syslogger(LOG_INFO, "stopping on TTY device %s", ttypath);
	ldisc = N_TTY;
	if (ioctl(fd, TIOCSETD, &ldisc) < 0) {
		perror("ioctl TIOCSETD");
		exit(EXIT_FAILURE);
	}

	/* Send close commands for channels that had -[ch]c flag */
	for (int i = 0; i < cmd_count; i++) {
		if (commands[i].cmd == 'c') {
			int n = snprintf(buf, sizeof(buf), "%dC\r", commands[i].channel);
			if (write(fd, buf, n) > 0) {
				syslogger(LOG_INFO, "Channel %d: Close on exit", commands[i].channel);
			}
		}
	}

	/* Reset old rates */
	cfsetispeed(&tios, old_ispeed);
	cfsetospeed(&tios, old_ospeed);

	/* apply changes */
	if (tcsetattr(fd, TCSADRAIN, &tios) < 0)
		syslogger(LOG_NOTICE, "Cannot set attributes for device \"%s\": %s!\n", ttypath, strerror(errno));

	/* Finish up */
	syslogger(LOG_NOTICE, "terminated on %s", ttypath);
	closelog();
	return exit_code;
}