using System;
using System.Collections.Generic;
using System.Threading;

namespace Corpc {
    public class LockProtoMessageQueue {
        Queue<ProtoMessage> q = new Queue<ProtoMessage>();

        public ProtoMessage Dequeue()
        {
            lock(this) {
                if (q.Count > 0)
                {
                    return q.Dequeue();
                }
                else 
                {
                    return null;
                }
            }
        }

        public void Enqueue(ProtoMessage msg)
        {
            lock(this) {
                q.Enqueue(msg);
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