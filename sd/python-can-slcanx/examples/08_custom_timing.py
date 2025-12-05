import time
import can
from common import get_parser
from slcanx import SlcanxBus

def apply_custom_timing(bus):
    # Clock 80M
    # Arb: Pre 2, Seg1 63, Seg2 16, SJW 16 -> a80_2_63_16_16_0
    # Data: Pre 2, Seg1 15, Seg2 4, SJW 4, TDC on -> A80_2_15_4_4_1
    
    # 500K 80% + 2M 80%
    # cmd_arb = "a80_2_63_16_16_0"
    # cmd_data = "A80_2_15_4_4_1"
    
    # 1M 80% + 8M 80%
    cmd_arb = "a80_2_31_8_8_0"
    cmd_data = "A80_1_7_2_2_1"
    
    print(f"Applying custom timing: {cmd_arb}, {cmd_data}")
    bus.send_cmd(cmd_arb)
    bus.send_cmd(cmd_data)
    
    # Must re-open to apply
    bus.close_channel()
    bus.open_channel()

def main():
    parser = get_parser('Single Channel Custom Timing Example')
    parser.add_argument('--channel', '-c', type=int, default=0, help='SLCANX Channel (0-3)')
    args = parser.parse_args()

    print(f"Opening {args.port} Channel {args.channel}...")
    
    # Initialize with defaults, then override
    bus = SlcanxBus(channel=args.port, slcanx_channel=args.channel, fd=True)
    
    apply_custom_timing(bus)

    try:
        msg = can.Message(arbitration_id=0x600, 
                          is_fd=True, bitrate_switch=True,
                          data=[0xCC] * 8, 
                          is_extended_id=False)
        print(f"Sending custom timing frame...")
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
