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
		public int Type { get; }
		public MessageParser Parser { get; }
		public MessageHandler Handler { get; }

		public MessageRegInfo(int type, MessageParser parser, MessageHandler handler)
        {
			Type = type;
			Parser = parser;
			Handler = handler;
        }
	}

	public abstract class MessageClient
	{
		protected LockProtoMessageQueue recvMsgQueue_ = new LockProtoMessageQueue(); // 接收消息队列
		protected BlockingDequeueProtoMessageQueue sendMsgQueue_ = new BlockingDequeueProtoMessageQueue(); // 发送消息队列

		protected Thread recvMsgThread_ = null; // 接收消息线程
		protected Thread sendMsgThread_ = null; // 发送消息线程

		protected bool needHB_;           // 是否需要心跳
		protected bool enableSendCRC_;    // 是否需要发包时校验CRC码
		protected bool enableRecvCRC_;    // 是否需要收包时校验CRC码
		protected bool enableSerial_;     // 是否需要消息序号

		protected int lastRecvSerial_ = 0;
		protected int lastSendSerial_ = 0;

		protected long lastRecvHBTime_;   // 最后一次收到心跳的时间
		protected long lastSendHBTime_;   // 最后一次发送心跳的时间

		protected bool running_ = false;

		protected ICrypter crypter_ = null;

		Dictionary<int, MessageRegInfo> registerTable_ = new Dictionary<int, MessageRegInfo>();

        public bool Running
        {
            get
            {
                //Some other code
                return running_;
            }
        }

        public ICrypter Crypter {
            set
            {
                crypter_ = value;
            }
        }

		public MessageClient(bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial)
		{
			needHB_ = needHB;
			enableSendCRC_ = enableSendCRC;
			enableRecvCRC_ = enableRecvCRC;
			enableSerial_ = enableSerial;
		}

		public bool Register(int type, MessageParser parser, MessageHandler handler)
		{
			if (registerTable_.ContainsKey(type))
            {
				Debug.LogErrorFormat("duplicate register message [{0}]!!!", type);
				return false;
            }

			registerTable_.Add(type, new MessageRegInfo(type, parser, handler));
			return true;
		}

		public void Unregister(int type)
		{
			if (registerTable_.ContainsKey(type)) {
				registerTable_.Remove(type);
			}
		}

		protected void HandleMessage(int type, IMessage msg)
		{
			if (!registerTable_.ContainsKey(type))
			{
				Debug.LogWarningFormat("Handle unknown message [{0}]!!!", type);
				return;
			}

			registerTable_[type].Handler(type, msg);
		}

		protected byte[] Serialize(ProtoMessage msg)
		{
			if (msg.Data != null)
			{
				return msg.Data.ToByteArray();
			}

			Debug.LogErrorFormat("Serialize empty msg [{0}]!!!", msg.Type);
			return null;
		}

		protected IMessage Deserialize(int type, byte[] data, int offset, int length)
		{
			if (!registerTable_.ContainsKey(type))
            {
				Debug.LogErrorFormat("Deserialize unknown handle message [{0}]!!!", type);
				return null;
            }

			return registerTable_[type].Parser.ParseFrom(data, offset, length);
		}

        // 异步发送数据
        public void Send(int type, ushort tag, IMessage msg, bool needCrypter)
        {
            sendMsgQueue_.Enqueue(new ProtoMessage(type, tag, msg, needCrypter));
        }
	}
}

