using System;
using System.Collections.Generic;
using System.Text;
using ProtoBuf;

namespace Corpc
{
    public class ProtoMessage
    {
        readonly int type;
        readonly IExtensible data;

        public int Type
        {
            get
            {
                return type;
            }
        }

        public IExtensible Data
        {
            get
            {
                return data;
            }
        }

        public ProtoMessage(int type, IExtensible data)
        {
            this.type = type;
            this.data = data;
        }
    }
}
