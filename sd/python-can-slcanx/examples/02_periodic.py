import time
import can
from common import get_parser
from slcanx import SlcanxBus

def main():
    parser = get_parser('Periodic Send Example')
    parser.add_argument('--channel', '-c', type=int, default=0, help='SLCANX Channel (0-3)')
    args = parser.parse_args()

    print(f"Opening {args.port} Channel {args.channel} @ {args.bitrate}bps...")
    bus = SlcanxBus(channel=args.port, slcanx_channel=args.channel, bitrate=args.bitrate)

    try:
        # Create a periodic task
        msg = can.Message(arbitration_id=0x100, data=[0, 1, 2, 3], is_extended_id=False)
        print(f"Starting periodic send of {msg} every 1s")
        
        # python-can supports periodic sending
        task = bus.send_periodic(msg, period=1.0)
        
        if not isinstance(task, can.LimitedDurationCyclicSendTaskABC):
             print("Periodic task started.")

        # Receive messages while sending
        print("Listening... (Ctrl+C to exit)")
        while True:
            msg = bus.recv(1.0)
            if msg:
                print(f"Received: {msg}")
                
    except KeyboardInterrupt:
        print("\nExiting...")
    finally:
        if 'task' in locals():
            task.stop()
        bus.shutdown()

if __name__ == "__main__":
    main()
