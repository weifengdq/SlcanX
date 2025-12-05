import time
import threading
import can
from common import get_parser
from slcanx import SlcanxBus

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
    cnt = 0
    while True:
        try:
            msg = can.Message(arbitration_id=0x200 + bus_idx, data=[bus_idx, cnt % 256], is_extended_id=False)
            bus.send(msg)
            cnt += 1
            time.sleep(1.0)
        except Exception:
            break

def main():
    parser = get_parser('4-Channel Standard CAN Threading Example')
    args = parser.parse_args()

    buses = []
    threads = []

    try:
        # Open all 4 channels
        for i in range(4):
            print(f"Opening Channel {i}...")
            bus = SlcanxBus(channel=args.port, slcanx_channel=i, bitrate=args.bitrate)
            buses.append(bus)

            # Start Rx Thread
            t_rx = threading.Thread(target=rx_worker, args=(i, bus), daemon=True)
            t_rx.start()
            threads.append(t_rx)

            # Start Tx Thread
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
