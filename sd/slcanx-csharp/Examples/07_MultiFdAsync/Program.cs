using System;
using System.Threading.Tasks;
using Slcanx;

class Program
{
    static async Task Main(string[] args)
    {
        if (args.Length < 1) { Console.WriteLine("Usage: 07_MultiFdAsync <COM Port>"); return; }
        using (var slcanx = new Slcanx.Slcanx(args[0]))
        {
            slcanx.Open();
            var ch0 = slcanx.GetChannel(0);
            var ch1 = slcanx.GetChannel(1);
            
            ch0.Close(); ch0.SetRate(6); ch0.SetFdRate(2); ch0.Open();
            ch1.Close(); ch1.SetRate(6); ch1.SetFdRate(2); ch1.Open();

            var t1 = Task.Run(async () => {
                while (true) {
                    var data = new byte[64];
                    data[0] = 0xAA;
                    ch0.Send(new CanFrame(0x100, data, isFd: true, isBrs: true));
                    await Task.Delay(100);
                }
            });

            var t2 = Task.Run(async () => {
                while (true) {
                    var data = new byte[32];
                    data[0] = 0xBB;
                    ch1.Send(new CanFrame(0x200, data, isFd: true, isBrs: true));
                    await Task.Delay(200);
                }
            });

            Console.WriteLine("Running FD async tasks. Press Ctrl+C to exit.");
            await Task.Delay(-1);
        }
    }
}
