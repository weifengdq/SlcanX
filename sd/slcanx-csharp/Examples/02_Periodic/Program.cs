using System;
using System.Threading;
using Slcanx;

class Program
{
    static void Main(string[] args)
    {
        if (args.Length < 1) { Console.WriteLine("Usage: 02_Periodic <COM Port>"); return; }
        using (var slcanx = new Slcanx.Slcanx(args[0]))
        {
            slcanx.Open();
            var ch = slcanx.GetChannel(0);
            ch.Close();
            ch.SetRate(6);
            ch.Open();
            
            // Send every 10ms
            var timer = new Timer(_ => {
                ch.Send(new CanFrame(0x100, new byte[] { 0xAA, 0xBB }));
            }, null, 0, 10);

            Console.WriteLine("Sending every 10ms. Press Enter to exit.");
            Console.ReadLine();
        }
    }
}
