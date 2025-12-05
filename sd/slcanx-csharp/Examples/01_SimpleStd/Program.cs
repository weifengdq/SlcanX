using System;
using System.Threading;
using Slcanx;

class Program
{
    static void Main(string[] args)
    {
        if (args.Length < 1)
        {
            Console.WriteLine("Usage: 01_SimpleStd <COM Port>");
            return;
        }

        string port = args[0];
        using (var slcanx = new Slcanx.Slcanx(port))
        {
            slcanx.Open();
            var channel = slcanx.GetChannel(0);
            
            channel.FrameReceived += (frame) =>
            {
                Console.WriteLine($"RX Ch0: {frame}");
            };

            // 500kbps
            channel.Close();
            channel.SetRate(6); 
            channel.Open();

            Console.WriteLine($"Connected to {port}. Press Ctrl+C to exit.");
            
            while (true)
            {
                var frame = new CanFrame(0x123, new byte[] { 0x11, 0x22, 0x33 });
                channel.Send(frame);
                Console.WriteLine($"TX Ch0: {frame}");
                Thread.Sleep(1000);
            }
        }
    }
}
