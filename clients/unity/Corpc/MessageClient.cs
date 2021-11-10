using System;
using System.Collections;
using System.Collections.Generic;
using System.Threading;
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

	public abstract class MessageClient
	{
		protected LockProtoMessageQueue _recvMsgQueue = new LockProtoMessageQueue(); // 接收消息队列
		protected BlockingDequeueProtoMessageQueue _sendMsgQueue = new BlockingDequeueProtoMessageQueue(); // 发送消息队列

		protected Thread _recvMsgThread = null; // 接收消息线程
		protected Thread _sendMsgThread = null; // 发送消息线程

		protected bool _needHB;           // 是否需要心跳
		protected bool _enableSendCRC;    // 是否需要发包时校验CRC码
		protected bool _enableRecvCRC;    // 是否需要收包时校验CRC码
		protected bool _enableSerial;     // 是否需要消息序号

		protected int _lastRecvSerial = 0;
		protected int _lastSendSerial = 0;

		protected long _lastRecvHBTime;   // 最后一次收到心跳的时间
		protected long _lastSendHBTime;   // 最后一次发送心跳的时间

		protected bool _running = false;

		protected ICrypter _crypter = null;

		Dictionary<short, MessageRegInfo> _registerTable = new Dictionary<short, MessageRegInfo>();

        public bool Running
        {
            get
            {
                //Some other code
                return _running;
            }
        }

        public ICrypter Crypter {
            set
            {
                _crypter = value;
            }
        }

		public MessageClient(bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial)
		{
			_needHB = needHB;
			_enableSendCRC = enableSendCRC;
			_enableRecvCRC = enableRecvCRC;
			_enableSerial = enableSerial;
		}

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

		protected IMessage Deserialize(short type, byte[] data, int offset, int length)
		{
			if (!_registerTable.ContainsKey(type))
            {
				Debug.LogErrorFormat("Deserialize unknown handle message [%d]!!!", type);
				return null;
            }

			return _registerTable[type].Parser.ParseFrom(data, offset, length);
		}

        // 异步发送数据
        public void Send(short type, ushort tag, IMessage msg, bool needCrypter)
        {
            _sendMsgQueue.Enqueue(new ProtoMessage(type, tag, msg, needCrypter));
        }
	}
}

