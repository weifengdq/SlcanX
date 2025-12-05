import time
import threading
import can
from common import get_parser
from slcanx import SlcanxBus

def apply_custom_timing(bus):
    # Clock 80M
    
    # 500K 80% + 2M 80%
    # Arb: Pre 2, Seg1 63, Seg2 16, SJW 16 -> a80_2_63_16_16_0
    # Data: Pre 2, Seg1 15, Seg2 4, SJW 4, TDC on -> A80_2_15_4_4_1
    
    # 1M 80% + 8M 80%
    # Arb: Pre 2, Seg1 31, Seg2 8, SJW 8 -> a80_2_31_8_8_0
    # Data: Pre 1, Seg1 7, Seg2 2, SJW 2, TDC on -> A80_1_7_2_2_1
    
    bus.send_cmd("a80_2_31_8_8_0")
    bus.send_cmd("A80_1_7_2_2_1")
    bus.close_channel()
    bus.open_channel()

def rx_worker(bus_idx, bus):
    print(f"[Ch{bus_idx}] Listener started")
    while True:
        try:
            msg = bus.recv(1.0)
            if msg:
                print(f"[Ch{bus_idx}] Rx: {msg}")
        except Exception:
            break

def tx_worker(bus_idx, bus):
    print(f"[Ch{bus_idx}] Transmitter started")
    while True:
        try:
            msg = can.Message(arbitration_id=0x700 + bus_idx, 
                              is_fd=True, bitrate_switch=True,
                              data=[bus_idx] * 12, 
                              is_extended_id=False)
            bus.send(msg)
            time.sleep(1.0)
        except Exception:
            break

def main():
    parser = get_parser('4-Channel Custom Timing Threading Example')
    args = parser.parse_args()

    buses = []
    threads = []

    try:
        for i in range(4):
            print(f"Opening Channel {i} Custom Timing...")
            bus = SlcanxBus(channel=args.port, slcanx_channel=i, fd=True)
            apply_custom_timing(bus)
            buses.append(bus)

            t_rx = threading.Thread(target=rx_worker, args=(i, bus), daemon=True)
            t_rx.start()
            threads.append(t_rx)

            t_tx = threading.Thread(target=tx_worker, args=(i, bus), daemon=True)
            t_tx.start()
            threads.append(t_tx)

        print("All channels running. Ctrl+C to exit.")
        while True:
            time.sleep(1.0)

    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        for bus in buses:
            bus.shutdown()

if __name__ == "__main__":
    main()
