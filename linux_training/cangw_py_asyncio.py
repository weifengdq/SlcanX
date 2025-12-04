import asyncio
import can
import os
import signal

# Cleanup existing rules and background jobs
# os.system("sudo cangw -F > /dev/null 2>&1")
# os.system("pkill -f \"candump -L can0 can1 can2\" > /dev/null 2>&1")

async def forward_worker(src_bus_name, dst_bus):
    """
    Reads from src_bus_name and forwards to dst_bus with modification.
    """
    print(f"Starting forwarder: {src_bus_name} -> {dst_bus.channel_info}")
    
    # Create the bus
    # fd=True enables CAN FD support on the socket
    bus = can.Bus(interface='socketcan', channel=src_bus_name, fd=True)
    
    # Use AsyncBufferedReader to integrate with asyncio
    reader = can.AsyncBufferedReader()
    notifier = can.Notifier(bus, [reader], loop=asyncio.get_running_loop())

    try:
        async for msg in reader:
            # Modify frame: Force CAN FD and BRS
            # Note: python-can handles DLC automatically based on data length
            
            # Create a new message to ensure clean state
            # If original was remote frame, we convert to data frame (CAN FD doesn't support RTR)
            new_msg = can.Message(
                arbitration_id=msg.arbitration_id,
                data=msg.data,
                is_extended_id=msg.is_extended_id,
                is_fd=True,             # Force CAN FD
                bitrate_switch=True,    # Force BRS
                check=False
            )
            
            try:
                dst_bus.send(new_msg)
            except can.CanError as e:
                print(f"TX Error on {dst_bus.channel_info}: {e}")

    finally:
        notifier.stop()
        bus.shutdown()

async def main():
    # Destination bus (can3)
    # We use a single bus instance for sending
    dst_bus = can.Bus(interface='socketcan', channel='can3', fd=True)

    print("cangw_py_asyncio: Forwarding can0, can1, can2 -> can3 (CAN FD BRS)")

    # Create tasks for each source interface
    src_channels = ['can0', 'can1', 'can2']
    tasks = []
    
    for channel in src_channels:
        tasks.append(asyncio.create_task(forward_worker(channel, dst_bus)))

    try:
        # Wait for all tasks (they run forever until cancelled)
        await asyncio.gather(*tasks)
    except asyncio.CancelledError:
        pass
    finally:
        dst_bus.shutdown()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nStopped.")
