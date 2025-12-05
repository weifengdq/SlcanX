import logging
import threading
import time
import queue
import serial
from can import BusABC, Message
from can.util import len2dlc, dlc2len

logger = logging.getLogger(__name__)

# Shared serial ports: { "COM3": SerialHandlerInstance }
_handlers = {}
_handlers_lock = threading.Lock()

class SerialHandler:
    def __init__(self, port, baudrate=115200):
        self.port = port
        self.baudrate = baudrate
        self.serial = serial.Serial(port, baudrate=baudrate, timeout=0.1)
        self.lock = threading.Lock()
        self.buses = {} # { channel_id (0-3): [SlcanxBus, ...] }
        self.running = True
        self.thread = threading.Thread(target=self._reader_thread, daemon=True)
        self.thread.start()
        self.refs = 0

    def add_bus(self, bus, channel):
        with self.lock:
            if channel not in self.buses:
                self.buses[channel] = []
            self.buses[channel].append(bus)
            self.refs += 1

    def remove_bus(self, bus, channel):
        with self.lock:
            if channel in self.buses:
                if bus in self.buses[channel]:
                    self.buses[channel].remove(bus)
            self.refs -= 1
            return self.refs == 0

    def write(self, data):
        with self.lock:
            self.serial.write(data)

    def close(self):
        self.running = False
        if self.thread.is_alive():
            self.thread.join(timeout=1.0)
        if self.serial.is_open:
            self.serial.close()

    def _reader_thread(self):
        buffer = b""
        while self.running:
            try:
                if self.serial.in_waiting > 0:
                    data = self.serial.read(self.serial.in_waiting)
                    buffer += data
                    
                    while b'\r' in buffer:
                        line, buffer = buffer.split(b'\r', 1)
                        self._process_line(line)
                else:
                    time.sleep(0.001)
            except Exception as e:
                logger.error(f"Serial read error: {e}")
                if not self.serial.is_open:
                    break
                time.sleep(0.1)

    def _process_line(self, line_bytes):
        try:
            line = line_bytes.decode('ascii', errors='ignore')
        except:
            return

        if not line:
            return

        # Determine channel
        channel = 0
        cmd_start = 0
        
        first_char = line[0]
        if '0' <= first_char <= '3':
            channel = int(first_char)
            cmd_start = 1
        
        if cmd_start >= len(line):
            return
            
        # Dispatch to buses listening on this channel
        with self.lock:
            if channel in self.buses:
                for bus in self.buses[channel]:
                    bus.on_message_received(line[cmd_start:])

class SlcanxBus(BusABC):
    """
    Interface for SLCANX (4-channel SLCAN) protocol.
    
    :param channel: Serial port (e.g. 'COM3', '/dev/ttyACM0')
    :param tty_baudrate: Serial baudrate (default 115200)
    :param slcanx_channel: Logical channel 0-3 (default 0)
    :param bitrate: CAN bitrate
    :param data_bitrate: CAN FD data bitrate
    :param fd: Enable CAN FD
    """
    
    def __init__(self, channel, tty_baudrate=115200, bitrate=None, fd=False, data_bitrate=None, slcanx_channel=0, **kwargs):
        self.slcanx_channel = int(slcanx_channel)
        if not (0 <= self.slcanx_channel <= 3):
            raise ValueError("slcanx_channel must be 0-3")
            
        self.queue = queue.Queue()
        
        # Get or create handler
        with _handlers_lock:
            if channel not in _handlers:
                _handlers[channel] = SerialHandler(channel, tty_baudrate)
            self.handler = _handlers[channel]
            self.handler.add_bus(self, self.slcanx_channel)

        super().__init__(channel=channel, bitrate=bitrate, fd=fd, **kwargs)
        
        # Configure bitrate
        if bitrate:
            self.set_bitrate(bitrate)
        
        if fd and data_bitrate:
            self.set_data_bitrate(data_bitrate)
            
        # Open channel
        self.open_channel()

    def send(self, msg, timeout=None):
        cmd = ""
        is_fd = msg.is_fd
        is_ext = msg.is_extended_id
        is_rtr = msg.is_remote_frame
        
        if is_fd:
            if msg.bitrate_switch:
                cmd = 'b' if not is_ext else 'B'
            else:
                cmd = 'd' if not is_ext else 'D'
        else:
            if is_rtr:
                cmd = 'r' if not is_ext else 'R'
            else:
                cmd = 't' if not is_ext else 'T'
        
        # ID
        if is_ext:
            id_str = f"{msg.arbitration_id:08X}"
        else:
            id_str = f"{msg.arbitration_id:03X}"
            
        # DLC
        dlc = len2dlc(msg.dlc)
        dlc_str = f"{dlc:X}"
        
        # Data
        data_str = ""
        if not is_rtr:
            data_str = "".join(f"{b:02X}" for b in msg.data)
            
        line = f"{self.slcanx_channel}{cmd}{id_str}{dlc_str}{data_str}\r"
        
        self.handler.write(line.encode('ascii'))

    def _recv_internal(self, timeout):
        try:
            return self.queue.get(timeout=timeout)
        except queue.Empty:
            return None, False

    def on_message_received(self, line):
        if not line:
            return
            
        cmd = line[0]
        if cmd in 'tTrRdDbB':
            msg = self._parse_frame(line)
            if msg:
                self.queue.put((msg, True))
        elif cmd == 'E':
            # Error frame: Eslffttss
            # s: Bus Status
            # l: Last Protocol Error
            # ff: Firmware Error Flags
            # tt: Tx Error Count
            # rr: Rx Error Count
            # We could log this or create an ErrorFrame
            pass

    def _parse_frame(self, line):
        try:
            cmd = line[0]
            is_ext = cmd in 'TRDB'
            is_fd = cmd in 'dDbB'
            is_brs = cmd in 'bB'
            is_rtr = cmd in 'rR'
            
            idx = 1
            if is_ext:
                arb_id = int(line[idx:idx+8], 16)
                idx += 8
            else:
                arb_id = int(line[idx:idx+3], 16)
                idx += 3
                
            dlc_char = int(line[idx], 16)
            idx += 1
            
            dlc = dlc2len(dlc_char)
            
            data = bytearray()
            if not is_rtr:
                data_str = line[idx:]
                # In case there are extra chars (though there shouldn't be)
                # data_str length should be 2 * dlc
                # But for FD, dlc can be large.
                # Just parse what's there
                for i in range(0, len(data_str), 2):
                    if i+2 <= len(data_str):
                        data.append(int(data_str[i:i+2], 16))
            
            msg = Message(
                arbitration_id=arb_id,
                is_extended_id=is_ext,
                is_remote_frame=is_rtr,
                is_fd=is_fd,
                bitrate_switch=is_brs,
                dlc=dlc,
                data=data,
                timestamp=time.time()
            )
            return msg
        except Exception as e:
            logger.warning(f"Failed to parse line '{line}': {e}")
            return None

    def set_bitrate(self, bitrate):
        # Map bitrate to index or use yNNNNN
        # Sx: 0=10k, 1=20k, 2=50k, 3=100k, 4=125k, 5=250k, 6=500k, 7=800k, 8=1M
        rates = {
            10000: 0, 20000: 1, 50000: 2, 100000: 3,
            125000: 4, 250000: 5, 500000: 6, 800000: 7, 1000000: 8
        }
        
        if bitrate in rates:
            self.send_cmd(f"S{rates[bitrate]}")
        else:
            self.send_cmd(f"y{bitrate}")

    def set_data_bitrate(self, bitrate):
        # Yx: 1=1M ... 15=15M
        # x = bitrate / 1000000
        if bitrate % 1000000 == 0:
            idx = bitrate // 1000000
            if 1 <= idx <= 15:
                self.send_cmd(f"Y{idx}")
                return
        
        logger.warning(f"Unsupported data bitrate {bitrate}, must be integer multiple of 1M (1-15M)")

    def set_listen_only(self, enable):
        """
        Set listen-only mode.
        :param enable: True to enable listen-only, False to disable.
        """
        self.send_cmd(f"L{1 if enable else 0}")

    def set_sample_point(self, nominal=None, data=None):
        """
        Set sample point.
        :param nominal: Nominal sample point in percent (e.g. 75.0 for 75%)
        :param data: Data sample point in percent (e.g. 80.0 for 80%)
        """
        # pNNN: nominal sample point (0.1%)
        # PNNN: data sample point (0.1%)
        if nominal is not None:
            val = int(nominal * 10)
            self.send_cmd(f"p{val}")
        if data is not None:
            val = int(data * 10)
            self.send_cmd(f"P{val}")

    def open_channel(self):
        self.send_cmd('O')
        
    def close_channel(self):
        self.send_cmd('C')
        
    def send_cmd(self, cmd_str):
        line = f"{self.slcanx_channel}{cmd_str}\r"
        self.handler.write(line.encode('ascii'))

    def shutdown(self):
        self.close_channel()
        with _handlers_lock:
            if self.handler.remove_bus(self, self.slcanx_channel):
                self.handler.close()
                if self.channel in _handlers:
                    del _handlers[self.channel]
        super().shutdown()
