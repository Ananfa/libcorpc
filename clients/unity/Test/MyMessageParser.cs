using System;
using System.IO;
using ProtoBuf;
using Corpc;
using echo;

public class MyMessageParser : IMessageParser
{

    public IExtensible parse(int type, byte[] data, int index, int count)
	{
		IExtensible result = null;

		switch (type)
		{
		case 1: 
			{result = Serializer.Deserialize<FooResponse>(new MemoryStream(data, index, count));
				break;
			}

		}

		return result;
	}
}


