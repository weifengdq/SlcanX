import time
import can
from common import get_parser
from slcanx import SlcanxBus

def main():
    parser = get_parser('Single Channel CAN FD Example')
    parser.add_argument('--channel', '-c', type=int, default=0, help='SLCANX Channel (0-3)')
    args = parser.parse_args()

    # Arb 500K, Data 2M
    arb_rate = 500000
    data_rate = 2000000
    
    print(f"Opening {args.port} Channel {args.channel} FD")
    print(f"Arb: {arb_rate}bps (80%), Data: {data_rate}bps (80%)")
    
    bus = SlcanxBus(channel=args.port, slcanx_channel=args.channel, 
                    fd=True, bitrate=arb_rate, data_bitrate=data_rate)
    
    # Set sample points
    bus.set_sample_point(nominal=80.0, data=80.0)
    # Re-open to apply changes if needed, though set_sample_point sends commands immediately.
    # The firmware applies params on 'O' command. SlcanxBus opens channel in __init__.
    # So we should close and reopen or send 'O' again.
    bus.close_channel()
    bus.open_channel()

    try:
        # Send FD message with BRS
        msg = can.Message(arbitration_id=0x123, 
                          is_fd=True, bitrate_switch=True,
                          data=[i for i in range(64)], 
                          is_extended_id=False)
        print(f"Sending FD frame (64 bytes)...")
        bus.send(msg)

        print("Listening...")
        while True:
            msg = bus.recv(1.0)
            if msg:
                print(f"Received: {msg}")
                
    except KeyboardInterrupt:
        print("\nExiting...")
    finally:
        bus.shutdown()

if __name__ == "__main__":
    main()
