using System;
using System.Collections;
using System.Collections.Generic;
using ProtoBuf;

namespace Corpc
{

	// 消息回调定义
	public delegate void MessageHandler(int type, IExtensible msg);

	public abstract class MessageDispatcher
	{
		Dictionary<int, List<MessageHandler>> _registerTable = new Dictionary<int, List<MessageHandler>>();

		public void Register(int type, MessageHandler handler)
		{
			if (!_registerTable.ContainsKey(type)) {
				_registerTable.Add(type, new List<MessageHandler>());
			}

			List<MessageHandler> registerList = _registerTable[type];
			if (!registerList.Contains(handler)) {
				registerList.Add(handler);                
			}
		}

		public void Unregister(int type, MessageHandler handler)
		{
			if (_registerTable.ContainsKey(type)) {
				List<MessageHandler> registerList = _registerTable[type];
				registerList.Remove(handler);
			}
		}

		protected void Dispatch(int type, IExtensible msg)
		{
			if (_registerTable.ContainsKey(type)) {
				List<MessageHandler> registerList = _registerTable[type];
				for (int i = 0; i < registerList.Count; i++) {
					((MessageHandler)registerList[i])(type, msg);
				}
			}
		}
	}
}

