using System;
using System.Collections.Generic;
using System.Text;
using Google.Protobuf;

namespace Corpc
{
    public class ProtoMessage
    {
        public int Type { get; }
        public ushort Tag { get; }
        public IMessage Data { get; }

        // NeedCrypter只用于发送时
        public bool NeedCrypter { get; }

        public ProtoMessage(int type, ushort tag, IMessage data, bool needCrypter)
        {
            Type = type;
            Tag = tag;
            Data = data;
            NeedCrypter = needCrypter;
        }
    }
}
