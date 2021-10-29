using System;
using System.Collections;
using System.Collections.Generic;
using Google.Protobuf;
using UnityEngine;

namespace Corpc
{

	// 消息回调定义
	public delegate void MessageHandler(int type, IMessage msg);

	public class MessageRegInfo
	{
		public short Type { get; }
		public MessageParser Parser { get; }
		public MessageHandler Handler { get; }

		public MessageRegInfo(short type, MessageParser parser, MessageHandler handler)
        {
			Type = type;
			Parser = parser;
			Handler = handler;
        }
	}

	public abstract class MessageManager
	{
		Dictionary<short, MessageRegInfo> _registerTable = new Dictionary<short, MessageRegInfo>();

		public bool Register(short type, MessageParser parser, MessageHandler handler)
		{
			if (_registerTable.ContainsKey(type))
            {
				Debug.LogErrorFormat("duplicate register message [%d]!!!", type);
				return false;
            }

			_registerTable.Add(type, new MessageRegInfo(type, parser, handler));
			return true;
		}

		public void Unregister(short type)
		{
			if (_registerTable.ContainsKey(type)) {
				_registerTable.Remove(type);
			}
		}

		protected void HandleMessage(short type, IMessage msg)
		{
			if (!_registerTable.ContainsKey(type))
			{
				Debug.LogWarningFormat("Handle unknown message [%d]!!!", type);
				return;
			}

			_registerTable[type].Handler(type, msg);
		}

		protected byte[] Serialize(ProtoMessage msg)
		{
			if (msg.Data != null)
			{
				return msg.Data.ToByteArray();
			}

			Debug.LogErrorFormat("Serialize empty msg [%d]!!!", msg.Type);
			return null;
		}

		protected IMessage Deserialize(short type, byte[] data, int count)
		{
			if (!_registerTable.ContainsKey(type))
            {
				Debug.LogErrorFormat("Deserialize unknown handle message [%d]!!!", type);
				return null;
            }

			return _registerTable[type].Parser.ParseFrom(data, 0, count);
		}

	}
}

