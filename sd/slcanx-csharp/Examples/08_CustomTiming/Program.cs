using System;
using System.Threading;
using Slcanx;

class Program
{
    static void Main(string[] args)
    {
        if (args.Length < 1) { Console.WriteLine("Usage: 08_CustomTiming <COM Port>"); return; }
        using (var slcanx = new Slcanx.Slcanx(args[0]))
        {
            slcanx.Open();
            var ch = slcanx.GetChannel(0);
            
            // Custom timing string example (needs valid values for the MCU)
            ch.Close();
            ch.SetCustomTiming("a80_4_31_8_8_0");
            ch.Open();

            while (true)
            {
                ch.Send(new CanFrame(0x123, new byte[] { 0x11 }));
                Console.WriteLine("Sent frame with custom timing");
                Thread.Sleep(1000);
            }
        }
    }
}
