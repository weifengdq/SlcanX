using System;
using System.Text;

namespace Slcanx
{
    public struct CanFrame
    {
        public uint Id { get; set; }
        public byte[] Data { get; set; }
        public int Dlc { get; set; }
        public bool IsExtended { get; set; }
        public bool IsRemote { get; set; }
        public bool IsFd { get; set; }
        public bool IsBrs { get; set; }

        public CanFrame(uint id, byte[] data, bool isExtended = false, bool isRemote = false, bool isFd = false, bool isBrs = false)
        {
            Id = id;
            Data = data ?? new byte[0];
            Dlc = Data.Length; // Simplified DLC handling
            IsExtended = isExtended;
            IsRemote = isRemote;
            IsFd = isFd;
            IsBrs = isBrs;
        }

        public override string ToString()
        {
            var sb = new StringBuilder();
            sb.Append($"ID: {Id:X}");
            if (IsExtended) sb.Append(" (EXT)");
            if (IsRemote) sb.Append(" (RTR)");
            if (IsFd) sb.Append(" (FD)");
            if (IsBrs) sb.Append(" (BRS)");
            sb.Append($" DLC: {Dlc} Data: ");
            foreach (var b in Data)
            {
                sb.Append($"{b:X2} ");
            }
            return sb.ToString();
        }
    }
}
