using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.IO.Ports;
using System.Text;
using System.Threading;

namespace Slcanx
{
    public class Slcanx : IDisposable
    {
        private SerialPort _serialPort;
        private Thread? _readThread;
        private Thread? _writeThread;
        private volatile bool _running;
        private readonly ConcurrentQueue<string> _writeQueue = new ConcurrentQueue<string>();
        private readonly AutoResetEvent _writeSignal = new AutoResetEvent(false);
        private readonly SlcanxChannel[] _channels = new SlcanxChannel[4];

        public Slcanx(string portName)
        {
            _serialPort = new SerialPort(portName, 115200, Parity.None, 8, StopBits.One);
            _serialPort.NewLine = "\r";
            _serialPort.DtrEnable = true;
            _serialPort.RtsEnable = false;
            
            for (int i = 0; i < 4; i++)
            {
                _channels[i] = new SlcanxChannel(this, i);
            }
        }

        public void Open()
        {
            if (!_serialPort.IsOpen)
            {
                _serialPort.Open();
                _running = true;
                
                _readThread = new Thread(ReadLoop) { IsBackground = true };
                _readThread.Start();
                
                _writeThread = new Thread(WriteLoop) { IsBackground = true };
                _writeThread.Start();
            }
        }

        public void Close()
        {
            _running = false;
            _writeSignal.Set(); 
            if (_serialPort.IsOpen)
            {
                // Give threads a moment to stop
                Thread.Sleep(10);
                try { _serialPort.Close(); } catch { }
            }
        }

        public SlcanxChannel GetChannel(int index)
        {
            if (index < 0 || index > 3) throw new ArgumentOutOfRangeException(nameof(index));
            return _channels[index];
        }

        public void EnqueueCommand(string cmd)
        {
            _writeQueue.Enqueue(cmd);
            _writeSignal.Set();
        }

        private void ReadLoop()
        {
            while (_running && _serialPort.IsOpen)
            {
                try
                {
                    string line = _serialPort.ReadLine();
                    ProcessLine(line);
                }
                catch (Exception)
                {
                    if (!_running) break;
                }
            }
        }

        private void ProcessLine(string line)
        {
            if (string.IsNullOrEmpty(line)) return;
            char ch = line[0];
            int channelIndex = -1;
            string payload = line;

            if (ch >= '0' && ch <= '3')
            {
                channelIndex = ch - '0';
                payload = line.Substring(1);
            }
            else
            {
                channelIndex = 0; 
            }
            
            if (channelIndex >= 0 && channelIndex < 4)
            {
                _channels[channelIndex].OnMessageReceived(payload);
            }
        }

        private void WriteLoop()
        {
            StringBuilder batch = new StringBuilder();
            long ticksLimit = (Stopwatch.Frequency * 125) / 1000000; // 125us

            while (_running)
            {
                _writeSignal.WaitOne(); 
                if (!_running) break;

                batch.Clear();
                
                if (_writeQueue.TryDequeue(out string? first))
                {
                    batch.Append(first);
                    batch.Append('\r');
                    
                    long startTicks = Stopwatch.GetTimestamp();
                    
                    // Try to group more writes for up to 125us
                    while (Stopwatch.GetTimestamp() - startTicks < ticksLimit && batch.Length < 2048)
                    {
                        if (_writeQueue.TryDequeue(out string? next))
                        {
                            batch.Append(next);
                            batch.Append('\r');
                        }
                        else
                        {
                            Thread.SpinWait(10);
                        }
                    }
                }

                if (batch.Length > 0 && _serialPort.IsOpen)
                {
                    try
                    {
                        _serialPort.Write(batch.ToString());
                    }
                    catch { }
                }
            }
        }

        public void Dispose()
        {
            Close();
        }
    }
}
