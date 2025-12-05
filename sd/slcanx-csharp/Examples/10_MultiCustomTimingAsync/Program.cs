using System;
using System.Threading.Tasks;
using Slcanx;

class Program
{
    static async Task Main(string[] args)
    {
        if (args.Length < 1) { Console.WriteLine("Usage: 10_MultiCustomTimingAsync <COM Port>"); return; }
        using (var slcanx = new Slcanx.Slcanx(args[0]))
        {
            slcanx.Open();
            var ch0 = slcanx.GetChannel(0);
            var ch1 = slcanx.GetChannel(1);
            
            ch0.Close(); ch0.SetCustomTiming("a80_4_31_8_8_0"); ch0.SetCustomTiming("A80_2_15_4_4_1"); ch0.Open();
            ch1.Close(); ch1.SetCustomTiming("a80_4_31_8_8_0"); ch1.SetCustomTiming("A80_2_15_4_4_1"); ch1.Open();

            var t1 = Task.Run(async () => {
                while (true) {
                    ch0.Send(new CanFrame(0x100, new byte[] { 0x00 }));
                    await Task.Delay(100);
                }
            });

            var t2 = Task.Run(async () => {
                while (true) {
                    ch1.Send(new CanFrame(0x200, new byte[] { 0x11 }));
                    await Task.Delay(200);
                }
            });

            await Task.Delay(-1);
        }
    }
}
