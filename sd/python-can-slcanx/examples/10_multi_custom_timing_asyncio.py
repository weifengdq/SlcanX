import asyncio
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
            msg = can.Message(arbitration_id=0x800 + idx, 
                              is_fd=True, bitrate_switch=True,
                              data=[idx] * 20, 
                              is_extended_id=False)
            bus.send(msg)
            await asyncio.sleep(1.0)
    except asyncio.CancelledError:
        pass

async def main_async(args):
    buses = []
    tasks = []
    
    try:
        for i in range(4):
            print(f"Opening Channel {i} Custom Timing...")
            bus = SlcanxBus(channel=args.port, slcanx_channel=i, fd=True)
            apply_custom_timing(bus)
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
    parser = get_parser('4-Channel Custom Timing Asyncio Example')
    args = parser.parse_args()
    
    try:
        asyncio.run(main_async(args))
    except KeyboardInterrupt:
        pass

if __name__ == "__main__":
    main()
