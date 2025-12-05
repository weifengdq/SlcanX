using System;

namespace Slcanx
{
    public class SlcanxChannel
    {
        private readonly Slcanx _parent;
        private readonly int _index;

        public event Action<CanFrame>? FrameReceived;

        public SlcanxChannel(Slcanx parent, int index)
        {
            _parent = parent;
            _index = index;
        }

        public void Open()
        {
            SendCommand("O");
        }

        public void Close()
        {
            SendCommand("C");
        }

        public void SetRate(int index)
        {
            SendCommand($"S{index}");
        }
        
        public void SetBitrate(int bitrate)
        {
             SendCommand($"y{bitrate}");
        }

        public void SetFdRate(int index)
        {
            SendCommand($"Y{index}");
        }

        public void SetCustomTiming(string timing)
        {
            // timing string like "a..." or "A..."
            // The user passes the full command suffix usually, or we prepend 'a'/'A'?
            // The protocol says "aCLK..." or "A...".
            // Let's assume user passes the full string starting with 'a' or 'A'.
            SendCommand(timing);
        }

        public void Send(CanFrame frame)
        {
            char cmd;
            if (frame.IsFd)
            {
                if (frame.IsBrs) cmd = 'b'; 
                else cmd = 'd'; 
                
                if (frame.IsExtended) cmd = char.ToUpper(cmd);
            }
            else
            {
                cmd = frame.IsExtended ? 'T' : 't';
                if (frame.IsRemote) cmd = frame.IsExtended ? 'R' : 'r';
            }

            string idStr = frame.IsExtended ? $"{frame.Id:X8}" : $"{frame.Id:X3}";
            string dlcStr = $"{DlcToHex(frame.Dlc)}";
            string dataStr = BitConverter.ToString(frame.Data).Replace("-", "");

            string msg = $"{_index}{cmd}{idStr}{dlcStr}{dataStr}";
            _parent.EnqueueCommand(msg);
        }

        private void SendCommand(string cmd)
        {
            _parent.EnqueueCommand($"{_index}{cmd}");
        }

        internal void OnMessageReceived(string msg)
        {
            if (string.IsNullOrEmpty(msg)) return;
            char cmd = msg[0];
            
            if ("tTdDbBrR".IndexOf(cmd) >= 0)
            {
                ParseFrame(msg);
            }
        }

        private void ParseFrame(string msg)
        {
            try 
            {
                if (msg.Length < 5) return; // Minimum length for standard frame (t1230)

                char cmd = msg[0];
                bool isExtended = char.IsUpper(cmd);
                bool isFd = "dDbB".IndexOf(char.ToLower(cmd)) >= 0;
                bool isBrs = "bB".IndexOf(char.ToLower(cmd)) >= 0;
                bool isRemote = "rR".IndexOf(char.ToLower(cmd)) >= 0;

                int idLen = isExtended ? 8 : 3;
                if (msg.Length < 1 + idLen + 1) return;

                string idStr = msg.Substring(1, idLen);
                uint id = Convert.ToUInt32(idStr, 16);

                int dlcIndex = 1 + idLen;
                char dlcChar = msg[dlcIndex];
                int len = HexToLen(dlcChar);

                byte[] data = new byte[len];
                if (!isRemote && len > 0)
                {
                    if (msg.Length < dlcIndex + 1 + len * 2) return;
                    string dataStr = msg.Substring(dlcIndex + 1);
                    for (int i = 0; i < len; i++)
                    {
                        data[i] = Convert.ToByte(dataStr.Substring(i * 2, 2), 16);
                    }
                }

                var frame = new CanFrame(id, data, isExtended, isRemote, isFd, isBrs);
                FrameReceived?.Invoke(frame);
            }
            catch 
            {
                // Ignore parse errors
            }
        }

        private char DlcToHex(int len)
        {
            if (len <= 8) return (char)('0' + len);
            if (len <= 12) return '9';
            if (len <= 16) return 'A';
            if (len <= 20) return 'B';
            if (len <= 24) return 'C';
            if (len <= 32) return 'D';
            if (len <= 48) return 'E';
            return 'F';
        }

        private int HexToLen(char hex)
        {
            if (hex >= '0' && hex <= '8') return hex - '0';
            switch (char.ToUpper(hex))
            {
                case '9': return 12;
                case 'A': return 16;
                case 'B': return 20;
                case 'C': return 24;
                case 'D': return 32;
                case 'E': return 48;
                case 'F': return 64;
                default: return 0;
            }
        }
    }
}
