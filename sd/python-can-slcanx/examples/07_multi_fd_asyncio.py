import asyncio
import can
from common import get_parser
from slcanx import SlcanxBus

async def bus_handler(idx, bus):
    reader = can.AsyncBufferedReader()
    notifier = can.Notifier(bus, [reader], loop=asyncio.get_running_loop())
    
    print(f"[Ch{idx}] Handler started")
    
    tx_task = asyncio.create_task(periodic_sender(idx, bus))
    
    try:
        while True:
            msg = await reader.get_message()
            print(f"[Ch{idx}] Rx: {msg}")
    except asyncio.CancelledError:
        pass
    finally:
        tx_task.cancel()
        notifier.stop()

async def periodic_sender(idx, bus):
    try:
        while True:
            msg = can.Message(arbitration_id=0x500 + idx, 
                              is_fd=True, bitrate_switch=True,
                              data=[idx] * 32, 
                              is_extended_id=False)
            bus.send(msg)
            await asyncio.sleep(1.0)
    except asyncio.CancelledError:
        pass

async def main_async(args):
    buses = []
    tasks = []
    
    arb_rate = 1000000
    data_rate = 8000000
    
    try:
        for i in range(4):
            print(f"Opening Channel {i} FD...")
            bus = SlcanxBus(channel=args.port, slcanx_channel=i, 
                            fd=True, bitrate=arb_rate, data_bitrate=data_rate)
            
            bus.set_sample_point(nominal=80.0, data=80.0)
            bus.close_channel()
            bus.open_channel()
            
            buses.append(bus)
            tasks.append(asyncio.create_task(bus_handler(i, bus)))
            
        print("All channels running. Ctrl+C to exit.")
        await asyncio.gather(*tasks)
        
    except asyncio.CancelledError:
        print("Cancelled")
    finally:
        for bus in buses:
            bus.shutdown()

def main():
    parser = get_parser('4-Channel CAN FD Asyncio Example')
    args = parser.parse_args()
    
    try:
        asyncio.run(main_async(args))
    except KeyboardInterrupt:
        pass

if __name__ == "__main__":
    main()
