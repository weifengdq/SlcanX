# Makefile for slcandx multi-channel version

CC = gcc
CFLAGS = -Wall -Wextra -O2
TARGET = slcandx
SRC = slcandx.c

.PHONY: all clean install test help

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)
	@echo "Build complete: $(TARGET)"
	@echo "Run 'make install' to install to /usr/local/bin (requires sudo)"
	@echo "Run 'make help' for usage examples"

clean:
	rm -f $(TARGET)
	@echo "Cleaned build artifacts"

install: $(TARGET)
	@if [ "$(shell id -u)" != "0" ]; then \
		echo "Error: Installation requires root privileges. Use 'sudo make install'"; \
		exit 1; \
	fi
	install -m 755 $(TARGET) /usr/local/bin/
	@echo "Installed $(TARGET) to /usr/local/bin/"

test: $(TARGET)
	@echo "Running multi-channel test (requires device connected)..."
	@if [ -z "$(TTY)" ]; then \
		echo "Usage: make test TTY=/dev/ttyUSB0"; \
		exit 1; \
	fi
	./test_multi_channel.sh $(TTY)

help:
	@echo "SLCANDX - Multi-channel CAN over Serial Line"
	@echo ""
	@echo "Build commands:"
	@echo "  make          - Build slcandx"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make install  - Install to /usr/local/bin (requires sudo)"
	@echo "  make test TTY=/dev/ttyUSB0 - Run multi-channel test"
	@echo ""
	@echo "Usage examples:"
	@echo ""
	@echo "  Single channel (channel 0, default):"
	@echo "    sudo ./slcandx -o -c -f -s6 /dev/ttyUSB0 can0"
	@echo ""
	@echo "  Multi-channel (4 channels):"
	@echo "    sudo ./slcandx -0o -0c -0f -0s6 /dev/ttyUSB0 can0"
	@echo "    sudo ./slcandx -1o -1c -1f -1s4 /dev/ttyUSB0 can1"
	@echo "    sudo ./slcandx -2o -2c -2f -2s6 -2Y4 /dev/ttyUSB0 can2"
	@echo "    sudo ./slcandx -3o -3c -3f -3y250000 /dev/ttyUSB0 can3"
	@echo ""
	@echo "  Query configuration:"
	@echo "    sudo ./slcandx -0q -0Q -0N -F /dev/ttyUSB0"
	@echo ""
	@echo "  Custom timing (80MHz, prescaler=4, seg1=31, seg2=8):"
	@echo "    sudo ./slcandx -0o -0c -0a80_4_31_8_8_0 /dev/ttyUSB0 can0"
	@echo ""
	@echo "Parameters:"
	@echo "  clean         - Kill all running slcandx processes"
	@echo "  -s <0-8>      - Nominal speed index (0=10k...8=1000k)"
	@echo "  -y <baud>     - Nominal speed value (5000-1000000)"
	@echo "  -Y <1-F>      - CANFD data speed (1=1M...F=16M, 0=disable)"
	@echo "  -p <750-875>  - Classic sample point (*10)"
	@echo "  -P <750-875>  - CANFD sample point (*10)"
	@echo "  -a <timing>   - Custom classic timing CLK_PRE_SEG1_SEG2_SJW_TDC"
	@echo "  -A <timing>   - Custom CANFD timing CLK_PRE_SEG1_SEG2_SJW_TDC"
	@echo "  -o            - Open channel"
	@echo "  -c            - Close channel on exit"
	@echo "  -f            - Read status flags"
	@echo "  -l/-l1        - Enable listen-only mode"
	@echo "  -l0           - Disable listen-only mode"
	@echo "  -q            - Query nominal config"
	@echo "  -Q            - Query CANFD config"
	@echo "  -N            - Query device UUID"
	@echo "  -F            - Run in foreground"
	@echo ""
	@echo "For more details, see README.md"
