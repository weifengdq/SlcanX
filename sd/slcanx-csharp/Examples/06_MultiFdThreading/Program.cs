using System;
using System.Threading;
using Slcanx;

class Program
{
    static void Main(string[] args)
    {
        if (args.Length < 1) { Console.WriteLine("Usage: 06_MultiFdThreading <COM Port>"); return; }
        using (var slcanx = new Slcanx.Slcanx(args[0]))
        {
            slcanx.Open();
            var ch0 = slcanx.GetChannel(0);
            var ch1 = slcanx.GetChannel(1);
            
            ch0.Close(); ch0.SetRate(6); ch0.SetFdRate(2); ch0.Open();
            ch1.Close(); ch1.SetRate(6); ch1.SetFdRate(2); ch1.Open();

            new Thread(() => {
                while (true) {
                    var data = new byte[64];
                    data[0] = 0xAA;
                    ch0.Send(new CanFrame(0x100, data, isFd: true, isBrs: true));
                    Thread.Sleep(100);
                }
            }).Start();

            new Thread(() => {
                while (true) {
                    var data = new byte[32];
                    data[0] = 0xBB;
                    ch1.Send(new CanFrame(0x200, data, isFd: true, isBrs: true));
                    Thread.Sleep(200);
                }
            }).Start();

            Console.WriteLine("Running FD threads. Press Enter to exit.");
            Console.ReadLine();
        }
    }
}
