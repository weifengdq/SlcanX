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
            # Send FD frame
            msg = can.Message(arbitration_id=0x400 + bus_idx, 
                              is_fd=True, bitrate_switch=True,
                              data=[bus_idx] * 16, 
                              is_extended_id=False)
            bus.send(msg)
            cnt += 1
            time.sleep(1.0)
        except Exception:
            break

def main():
    parser = get_parser('4-Channel CAN FD Threading Example')
    args = parser.parse_args()

    buses = []
    threads = []
    
    arb_rate = 1000000
    data_rate = 8000000

    try:
        for i in range(4):
            print(f"Opening Channel {i} FD...")
            bus = SlcanxBus(channel=args.port, slcanx_channel=i, 
                            fd=True, bitrate=arb_rate, data_bitrate=data_rate)
            
            # Set sample points and re-open
            bus.set_sample_point(nominal=80.0, data=80.0)
            bus.close_channel()
            bus.open_channel()
            
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
