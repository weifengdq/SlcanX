import time
import can
from common import get_parser
from slcanx import SlcanxBus

def main():
    parser = get_parser('Single Channel Standard CAN Example')
    parser.add_argument('--channel', '-c', type=int, default=0, help='SLCANX Channel (0-3)')
    args = parser.parse_args()

    print(f"Opening {args.port} Channel {args.channel} @ {args.bitrate}bps...")
    
    # Initialize the bus
    # Note: We can use interface='slcanx' if installed, or import class directly
    bus = SlcanxBus(channel=args.port, slcanx_channel=args.channel, bitrate=args.bitrate)

    try:
        # Send a message
        msg = can.Message(arbitration_id=0x123, data=[0x11, 0x22, 0x33, 0x44], is_extended_id=False)
        print(f"Sending: {msg}")
        bus.send(msg)

        # Receive messages
        print("Listening for messages (Ctrl+C to exit)...")
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
