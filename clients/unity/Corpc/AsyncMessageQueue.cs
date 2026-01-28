using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace Corpc
{
    // 为了支持异步等待，需要扩展消息队列
    // 这是一个简单的带超时的Dequeue扩展
    public class AsyncMessageQueue<T>
    {
        private readonly Queue<T> queue = new Queue<T>();
        private readonly SemaphoreSlim semaphore = new SemaphoreSlim(0, int.MaxValue);
        private readonly object lockObject = new object();

        public void Enqueue(T item)
        {
            lock (lockObject)
            {
                queue.Enqueue(item);
            }
            semaphore.Release();
        }

        public T DequeueWithTimeout(int timeout)
        {
            if (semaphore.Wait(timeout))
            {
                lock (lockObject)
                {
                    if (queue.Count > 0)
                    {
                        return queue.Dequeue();
                    }
                }
            }
            return default(T);
        }

        public async Task<T> DequeueAsync(CancellationToken ct = default)
        {
            await semaphore.WaitAsync(ct);
            lock (lockObject)
            {
                if (queue.Count > 0)
                {
                    return queue.Dequeue();
                }
            }
            return default(T);
        }

        public void Clear()
        {
            lock (lockObject)
            {
                queue.Clear();
            }
        }
    }
}