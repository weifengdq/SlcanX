using System;
using System.Threading.Tasks;
using Slcanx;

class Program
{
    static async Task Main(string[] args)
    {
        if (args.Length < 1) { Console.WriteLine("Usage: 12_MultiCustomTimingAsyncRxTx <COM Port>"); return; }
        using (var slcanx = new Slcanx.Slcanx(args[0]))
        {
            slcanx.Open();
            var ch0 = slcanx.GetChannel(0);
            var ch1 = slcanx.GetChannel(3);

            // Setup Receive Handlers
            ch0.FrameReceived += (frame) => Console.WriteLine($"[RX] Ch0: {frame}");
            ch1.FrameReceived += (frame) => Console.WriteLine($"[RX] Ch1: {frame}");

            // Configure with Custom Timing
            // Example timing string, replace with valid one for your setup
            string timing = "a80_4_31_8_8_0";
            string data_timing = "A80_2_15_4_4_1";
            int delay_ms = 1;
            ch0.Close();
            await Task.Delay(delay_ms);
            ch0.SetCustomTiming(timing);
            await Task.Delay(delay_ms);
            ch0.SetCustomTiming(data_timing); 
            await Task.Delay(delay_ms); 
            ch0.Open();
            await Task.Delay(delay_ms); 
            ch1.Close();
            await Task.Delay(delay_ms);
            ch1.SetCustomTiming(timing);
            await Task.Delay(delay_ms);
            ch1.SetCustomTiming(data_timing); 
            await Task.Delay(delay_ms); 
            ch1.Open();

            Console.WriteLine("Connected. RX handlers attached. Starting TX tasks...");

            var t1 = Task.Run(async () =>
            {
                while (true)
                {
                    ch0.Send(new CanFrame(0x100, new byte[] { 0x00 }));
                    Console.WriteLine("[TX] Ch0 sent frame");
                    await Task.Delay(500);
                }
            });

            var t2 = Task.Run(async () =>
            {
                while (true)
                {
                    ch1.Send(new CanFrame(0x200, new byte[] { 0x11 }));
                    Console.WriteLine("[TX] Ch1 sent frame");
                    await Task.Delay(1000);
                }
            });

            await Task.Delay(-1);
        }
    }
}
