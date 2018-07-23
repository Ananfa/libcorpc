using System;
using System.Collections.Generic;
using System.Threading;

namespace Corpc
{
    class BlockingDequeueProtoMessageQueue
    {
        Queue<ProtoMessage> q = new Queue<ProtoMessage>();

        public ProtoMessage Dequeue()
        {
            lock (this)
            {
                while (q.Count == 0)
                {
                    Monitor.Wait(this);
                }

                return q.Dequeue();
            }
        }

        public void Enqueue(ProtoMessage msg)
        {
            lock (this)
            {
                q.Enqueue(msg);
                Monitor.Pulse(this);
            }
        }

		public void Clear()
		{
			lock(this) {
				q.Clear();
			}
		}
    }
}
