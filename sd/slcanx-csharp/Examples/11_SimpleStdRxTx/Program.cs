using System;
using System.Threading;
using Slcanx;

class Program
{
    static void Main(string[] args)
    {
        if (args.Length < 1)
        {
            Console.WriteLine("Usage: 11_SimpleStdRxTx <COM Port>");
            return;
        }

        string port = args[0];
        using (var slcanx = new Slcanx.Slcanx(port))
        {
            slcanx.Open();
            var channel = slcanx.GetChannel(0);
            
            // Setup Receive Handler
            channel.FrameReceived += (frame) =>
            {
                Console.WriteLine($"[RX] Ch0: {frame}");
            };

            // Configure and Open
            channel.Close();
            channel.SetRate(6); // 500kbps
            channel.Open();

            Console.WriteLine($"Connected to {port}. RX and TX enabled. Press Ctrl+C to exit.");
            
            // Send Loop
            long counter = 0;
            while (true)
            {
                counter++;
                var data = BitConverter.GetBytes(counter);
                var frame = new CanFrame(0x123, data);
                
                channel.Send(frame);
                Console.WriteLine($"[TX] Ch0: {frame}");
                
                Thread.Sleep(1000);
            }
        }
    }
}
