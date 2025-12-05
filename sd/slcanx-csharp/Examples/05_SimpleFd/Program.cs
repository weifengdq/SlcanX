using System;
using System.Threading;
using Slcanx;

class Program
{
    static void Main(string[] args)
    {
        if (args.Length < 1) { Console.WriteLine("Usage: 05_SimpleFd <COM Port>"); return; }
        using (var slcanx = new Slcanx.Slcanx(args[0]))
        {
            slcanx.Open();
            var ch = slcanx.GetChannel(0);
            
            ch.Close();
            ch.SetRate(6); // 500k
            ch.SetFdRate(2); // 2M
            ch.Open();

            while (true)
            {
                // FD Frame with BRS, 64 bytes
                var data = new byte[64];
                for(int i=0; i<64; i++) data[i] = (byte)i;
                var frame = new CanFrame(0x123, data, isFd: true, isBrs: true);
                ch.Send(frame);
                Console.WriteLine("Sent FD frame");
                Thread.Sleep(1000);
            }
        }
    }
}
