using System.IO;
using ProtoBuf;

namespace Corpc
{
    public interface IMessageParser
    {
		IExtensible parse(int type, byte[] data, int index, int count);
    }
}