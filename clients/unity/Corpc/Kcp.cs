using System;
using System.Collections.Concurrent;
using System.Runtime.InteropServices;

namespace Corpc
{
    public delegate int KcpOutput(IntPtr buf, int len, IntPtr kcp, IntPtr user);

    public class Kcp
    {
#if UNITY_IPHONE && !UNITY_EDITOR
        const string KcpDLL = "__Internal";
#else
        const string KcpDLL = "kcp";
#endif

        #region 原生导入
        [DllImport(KcpDLL, CallingConvention=CallingConvention.Cdecl)]
        private static extern uint ikcp_check(IntPtr kcp, uint current);
        [DllImport(KcpDLL, CallingConvention=CallingConvention.Cdecl)]
        private static extern IntPtr ikcp_create(uint conv, IntPtr user);
        [DllImport(KcpDLL, CallingConvention=CallingConvention.Cdecl)]
        private static extern void ikcp_flush(IntPtr kcp);
        [DllImport(KcpDLL, CallingConvention=CallingConvention.Cdecl)]
        private static extern uint ikcp_getconv(IntPtr ptr);
        [DllImport(KcpDLL, CallingConvention=CallingConvention.Cdecl)]
        private static extern int ikcp_input(IntPtr kcp, byte[] data, int offset, int size);
        [DllImport(KcpDLL, CallingConvention=CallingConvention.Cdecl)]
        private static extern int ikcp_nodelay(IntPtr kcp, int nodelay, int interval, int resend, int nc);
        [DllImport(KcpDLL, CallingConvention=CallingConvention.Cdecl)]
        private static extern int ikcp_peeksize(IntPtr kcp);
        [DllImport(KcpDLL, CallingConvention=CallingConvention.Cdecl)]
        private static extern int ikcp_recv(IntPtr kcp, byte[] buffer, int len);
        [DllImport(KcpDLL, CallingConvention=CallingConvention.Cdecl)]
        private static extern void ikcp_release(IntPtr kcp);
        [DllImport(KcpDLL, CallingConvention=CallingConvention.Cdecl)]
        private static extern int ikcp_send(IntPtr kcp, byte[] buffer, int len);
        [DllImport(KcpDLL, CallingConvention = CallingConvention.Cdecl)]
        private static extern int ikcp_send(IntPtr kcp, IntPtr buffer, int len);
        [DllImport(KcpDLL, CallingConvention=CallingConvention.Cdecl)]
        private static extern void ikcp_setminrto(IntPtr ptr, int minrto);
        [DllImport(KcpDLL, CallingConvention=CallingConvention.Cdecl)]
        private static extern int ikcp_setmtu(IntPtr kcp, int mtu);
        [DllImport(KcpDLL, CallingConvention=CallingConvention.Cdecl)]
        private static extern void ikcp_setoutput(IntPtr kcp, KcpOutput output);
        [DllImport(KcpDLL, CallingConvention=CallingConvention.Cdecl)]
        private static extern void ikcp_update(IntPtr kcp, uint current);
        [DllImport(KcpDLL, CallingConvention=CallingConvention.Cdecl)]
        private static extern int ikcp_waitsnd(IntPtr kcp);
        [DllImport(KcpDLL, CallingConvention=CallingConvention.Cdecl)]
        private static extern int ikcp_wndsize(IntPtr kcp, int sndwnd, int rcvwnd);
        #endregion

        /* ① 把 kcp 指针和锁映射起来；ConcurrentDictionary 本身线程安全 */
        private static readonly ConcurrentDictionary<IntPtr, object> s_lockMap
            = new ConcurrentDictionary<IntPtr, object>();

        /* ② 辅助：取得对应锁，不存在就新建 */
        private static object GetLock(IntPtr kcp)
        {
            return s_lockMap.GetOrAdd(kcp, _ => new object());
        }

        /* ③ 辅助：在 release 时把锁记录也删掉（可选） */
        public static void UnregisterLock(IntPtr kcp)
        {
            s_lockMap.TryRemove(kcp, out _);
        }

        public static uint KcpCheck(IntPtr kcp, uint current)
        {
            if (kcp == IntPtr.Zero) throw new Exception("kcp pointer is zero");
            lock (GetLock(kcp)) return ikcp_check(kcp, current);
        }

        public static IntPtr KcpCreate(uint conv, IntPtr user)
        {
            var kcp = ikcp_create(conv, user);
            if (kcp != IntPtr.Zero)
            {
                /* 新建时注册一条锁记录 */
                s_lockMap.TryAdd(kcp, new object());
            }
            return kcp;
        }

        public static void KcpFlush(IntPtr kcp)
        {
            if (kcp == IntPtr.Zero) throw new Exception("kcp pointer is zero");
            lock (GetLock(kcp)) ikcp_flush(kcp);
        }

        public static int KcpInput(IntPtr kcp, byte[] data, int offset, int size)
        {
            if (kcp == IntPtr.Zero) throw new Exception("kcp pointer is zero");
            lock (GetLock(kcp)) return ikcp_input(kcp, data, offset, size);
        }

        public static int KcpSend(IntPtr kcp, byte[] buffer, int len)
        {
            if (kcp == IntPtr.Zero) throw new Exception("kcp pointer is zero");
            lock (GetLock(kcp)) return ikcp_send(kcp, buffer, len);
        }

        public static int KcpSend(IntPtr kcp, IntPtr buffer, int len)
        {
            if (kcp == IntPtr.Zero) throw new Exception("kcp pointer is zero");
            lock (GetLock(kcp)) return ikcp_send(kcp, buffer, len);
        }

        public static int KcpRecv(IntPtr kcp, byte[] buffer, int len)
        {
            if (kcp == IntPtr.Zero) throw new Exception("kcp pointer is zero");
            lock (GetLock(kcp)) return ikcp_recv(kcp, buffer, len);
        }

        public static void KcpRelease(IntPtr kcp)
        {
            if (kcp == IntPtr.Zero) throw new Exception("kcp pointer is zero");
            lock (GetLock(kcp))
            {
                ikcp_release(kcp);
                /* 释放后把锁记录也清掉，防止字典无限增长 */
                UnregisterLock(kcp);
            }
        }

        public static int KcpPeeksize(IntPtr kcp)
        {
            if (kcp == IntPtr.Zero) throw new Exception("kcp pointer is zero");
            lock (GetLock(kcp)) return ikcp_peeksize(kcp);
        }

        public static void KcpUpdate(IntPtr kcp, uint current)
        {
            if (kcp == IntPtr.Zero) throw new Exception("kcp pointer is zero");
            lock (GetLock(kcp)) ikcp_update(kcp, current);
        }

        public static int KcpNodelay(IntPtr kcp, int nodelay, int interval, int resend, int nc)
        {
            if (kcp == IntPtr.Zero) throw new Exception("kcp pointer is zero");
            lock (GetLock(kcp)) return ikcp_nodelay(kcp, nodelay, interval, resend, nc);
        }

        public static int KcpSetmtu(IntPtr kcp, int mtu)
        {
            if (kcp == IntPtr.Zero) throw new Exception("kcp pointer is zero");
            lock (GetLock(kcp)) return ikcp_setmtu(kcp, mtu);
        }

        public static void KcpSetoutput(IntPtr kcp, KcpOutput output)
        {
            if (kcp == IntPtr.Zero) throw new Exception("kcp pointer is zero");
            lock (GetLock(kcp)) ikcp_setoutput(kcp, output);
        }

        public static int KcpWndsize(IntPtr kcp, int sndwnd, int rcvwnd)
        {
            if (kcp == IntPtr.Zero) throw new Exception("kcp pointer is zero");
            lock (GetLock(kcp)) return ikcp_wndsize(kcp, sndwnd, rcvwnd);
        }

        public static int KcpWaitsnd(IntPtr kcp)
        {
            if (kcp == IntPtr.Zero) throw new Exception("kcp pointer is zero");
            lock (GetLock(kcp)) return ikcp_waitsnd(kcp);
        }

        public static void KcpSetminrto(IntPtr kcp, int minrto)
        {
            if (kcp == IntPtr.Zero) throw new Exception("kcp pointer is zero");
            lock (GetLock(kcp)) ikcp_setminrto(kcp, minrto);
        }

        public static uint KcpGetconv(IntPtr ptr)
        {
            if (ptr == IntPtr.Zero) throw new Exception("kcp pointer is zero");
            lock (GetLock(ptr)) return ikcp_getconv(ptr);
        }
    }
}

