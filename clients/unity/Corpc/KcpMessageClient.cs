using System;
using System.Net;
using System.Collections.Generic;
using System.Text;
using System.Net.Sockets;
using System.Threading;
using System.IO;
using System.Runtime.InteropServices;
using Google.Protobuf;
using UnityEngine;

namespace Corpc
{   
    public class KcpMessageClient : MessageClient
    {
        private Socket udpSocket_ = null; // 与服务器的连接socket

        private string host_;
        private int port_;
        private int localPort_;

        private IntPtr kcp_;

        private byte[] handshake1msg_;
        private byte[] handshake3msg_;

        private byte[] heartbeatmsg_;

        private readonly MemoryStream memoryStream_;

        // private constructor
        public KcpMessageClient(string host, int port, int localPort, bool needHB, bool enableSendCRC, bool enableRecvCRC): base(needHB, enableSendCRC, enableRecvCRC, false)
        {
            host_ = host;
            port_ = port;
            localPort_ = localPort;

            handshake1msg_ = new byte[Constants.CORPC_UDP_HANDSHAKE_SIZE];
            handshake3msg_ = new byte[Constants.CORPC_UDP_HANDSHAKE_SIZE];
            heartbeatmsg_ = new byte[Constants.CORPC_MESSAGE_HEAD_SIZE];

            handshake1msg_[0] = (byte)((Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_1 >> 24) & 0xFF);
            handshake1msg_[1] = (byte)((Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_1 >> 16) & 0xFF);
            handshake1msg_[2] = (byte)((Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_1 >> 8) & 0xFF);
            handshake1msg_[3] = (byte)(Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_1 & 0xFF);

            handshake3msg_[0] = (byte)((Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_3 >> 24) & 0xFF);
            handshake3msg_[1] = (byte)((Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_3 >> 16) & 0xFF);
            handshake3msg_[2] = (byte)((Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_3 >> 8) & 0xFF);
            handshake3msg_[3] = (byte)(Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_3 & 0xFF);

            heartbeatmsg_[4] = (byte)((Constants.CORPC_MSG_TYPE_HEARTBEAT >> 24) & 0xFF);
            heartbeatmsg_[5] = (byte)((Constants.CORPC_MSG_TYPE_HEARTBEAT >> 16) & 0xFF);
            heartbeatmsg_[6] = (byte)((Constants.CORPC_MSG_TYPE_HEARTBEAT >> 8) & 0xFF);
            heartbeatmsg_[7] = (byte)(Constants.CORPC_MSG_TYPE_HEARTBEAT & 0xFF);

            memoryStream_ = SharedPools.Instance.GetStream("message", ushort.MaxValue);

            kcp_ = Kcp.KcpCreate(1, new IntPtr(this));
            Kcp.KcpNodelay(kcp_, 1, 10, 2, 1);
            Kcp.KcpWndsize(kcp_, 32, 32);
            Kcp.KcpSetmtu(kcp_, Constants.CORPC_KCP_MTU);
            SetOutput();
        }

        public bool Start()
        {
            if (udpSocket_ == null) {
                try {
                    udpSocket_ = new Socket (AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
                    udpSocket_.Bind(new IPEndPoint(IPAddress.Any, localPort_));
                    udpSocket_.Connect(host_, port_);

                    // handshake with server
                    if (!Handshake()) {
                        Debug.LogError("Handshake error!!!");

                        udpSocket_.Shutdown(SocketShutdown.Both);
                        udpSocket_.Close();
                        udpSocket_ = null;
                        return false;
                    }

                } catch (System.Exception ex) {
                    Debug.LogError("Connect error!!!");
                    Debug.LogError(ex.ToString());
                    Debug.LogError(ex.StackTrace);

                    if (udpSocket_ != null) {
                        udpSocket_.Close();
                        udpSocket_ = null;
                    }

                    return false;
                }
            }

            if (!running_) {
                if (needHB_) {
                    lastRecvHBTime_ = lastSendHBTime_ = DateTime.UtcNow.Ticks / TimeSpan.TicksPerMillisecond;
                }
    
                // 启动收发线程
                recvMsgThread_ = new Thread(new ThreadStart(RecvMsgLoop));
                recvMsgThread_.Start();

                // 启动发送线程
                sendMsgThread_ = new Thread(new ThreadStart(SendMsgLoop));
                sendMsgThread_.Start();

                running_ = true;
                return true;
            } else {
                return false;
            }
        }

        private bool Handshake()
        {
            byte[] buf = new byte[Constants.CORPC_MAX_UDP_MESSAGE_SIZE];

            int step = 1;
            // 发送handshake1消息，然后等待handshake2消息到来，超时未到则重发handshake1消息
            while (true) {
                if (step == 1) {
                    // 发送handshake1消息
                    if (udpSocket_.Send(handshake1msg_) != Constants.CORPC_UDP_HANDSHAKE_SIZE) {
                        Debug.LogError("Can't send handshake1");
                        return false;
                    }

                    if (udpSocket_.Poll(1000000, SelectMode.SelectRead)) {
                        // 接收handshake2消息
                        if (udpSocket_.Receive(buf) != Constants.CORPC_UDP_HANDSHAKE_SIZE) {
                            Debug.LogError("Receive handshake2 message size error");
                            return false;
                        }

                        int msgType = (int)((buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3]);
                        if (msgType != Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_2) {
                            Debug.LogError("Receive message not handshake2, msgType:" + msgType);
                            return false;
                        }

                        step = 2;
                    }
                } else {
                    // 发送handshake3消息
                    if (udpSocket_.Send(handshake3msg_) != Constants.CORPC_UDP_HANDSHAKE_SIZE) {
                        Debug.LogError("Can't send handshake3");
                        return false;
                    }

                    if (udpSocket_.Poll(1000000, SelectMode.SelectRead)) {
                        // 接收handshake4消息
                        if (udpSocket_.Receive(buf) != Constants.CORPC_UDP_HANDSHAKE_SIZE) {
                            Debug.LogError("Receive handshake4 message size error");
                            return false;
                        }

                        int msgType = (int)((buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3]);
                        if (msgType != Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_2) {
                            continue;
                        }

                        if (msgType != Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_4) {
                            Debug.LogError("Receive message not handshake4, msgType:" + msgType);
                            return false;
                        }

                        break;
                    }
                }
            }

            return true;
        }

        public void Close()
        {
            if (sendMsgThread_ != null) {
                // send close msg to sendMsgThread
                sendMsgQueue_.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                sendMsgThread_.Join();
                sendMsgThread_ = null;
            }

            if (udpSocket_ != null) {
                try {
                    udpSocket_.Shutdown(SocketShutdown.Both);
                    udpSocket_.Close();
                } catch (System.Exception ex) {
                    Debug.LogError("Shutdown socket error!!!");
                    Debug.LogError(ex.ToString());
                    Debug.LogError(ex.StackTrace);
                }
                udpSocket_ = null;
            }

            if (recvMsgThread_ != null) {
                recvMsgThread_.Join();
                recvMsgThread_ = null;
            }

            running_ = false;

            // 清理收发队列（主要是发送队列）
            // 注意：此时收发线程已经退出，对收发队列的处理没有线程同步的问题
            recvMsgQueue_.Clear();
            sendMsgQueue_.Clear();

            HandleMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, null);
        }

        // Update method is called by outside MonoBehaviour object's Update method
        public void Update()
        {
            // 定期检查心跳，分发消息处理，由外部的MonoBehaviour对象驱动
            if (running_) {
                long nowms = DateTime.UtcNow.Ticks / TimeSpan.TicksPerMillisecond;

                if (nowms - lastRecvHBTime_ > Constants.CORPC_MAX_NO_HEARTBEAT_TIME) {
                    // 无心跳，断线
                    Debug.LogError("heartbeat timeout");
                    Close();
                } else if (nowms - lastSendHBTime_ > Constants.CORPC_HEARTBEAT_PERIOD) {
                    Send(Constants.CORPC_MSG_TYPE_HEARTBEAT, 0, null, false);
                    lastSendHBTime_ = nowms;
                }

                Kcp.KcpUpdate(kcp_, (uint)nowms);

                // 心跳和断线消息需要特殊处理
                ProtoMessage pmsg = recvMsgQueue_.Dequeue();
                while (pmsg != null) {
                    switch (pmsg.Type) {
                        case Constants.CORPC_MSG_TYPE_DISCONNECT:
                        {
                            Close();
                            break;
                        }
                        case Constants.CORPC_MSG_TYPE_HEARTBEAT:
                        {
                            lastRecvHBTime_ = DateTime.UtcNow.Ticks / TimeSpan.TicksPerMillisecond;
                            break;
                        }
                        default:
                        {
                            HandleMessage(pmsg.Type, pmsg.Data);
                            break;
                        }
                    }

                    pmsg = recvMsgQueue_.Dequeue();
                }
            }
        }

#if !ENABLE_IL2CPP
        private KcpOutput kcpOutput;
#endif

        public void SetOutput()
        {
#if ENABLE_IL2CPP
            Kcp.KcpSetoutput(kcp_, KcpOutput);
#else
            // 跟上一行一样写法，pc跟linux会出错, 保存防止被GC
            kcpOutput = KcpOutput;
            Kcp.KcpSetoutput(kcp_, kcpOutput);
#endif
        }

#if ENABLE_IL2CPP
        [AOT.MonoPInvokeCallback(typeof(KcpOutput))]
#endif
        public static int KcpOutput(IntPtr bytes, int len, IntPtr kcp, IntPtr user)
        {
            // 每条连接独享一个小数组，避免共享流
            byte[] tmp = ArrayPool<byte>.Shared.Rent(len);
            try
            {
                Marshal.Copy(bytes, tmp, 0, len);
                lock (udpSocket_)
                    udpSocket_.Send(tmp, 0, len, SocketFlags.None);
            }
            finally
            {
                ArrayPool<byte>.Shared.Return(tmp);
            }
            return len;
        }

        private void RecvMsgLoop()
        {
            //byte[] buf = new byte[Constants.CORPC_MAX_UDP_MESSAGE_SIZE];
            byte[] buf = memoryStream_.GetBuffer();
            memoryStream_.SetLength(Constants.CORPC_MAX_UDP_MESSAGE_SIZE);
            memoryStream_.Seek(0, SeekOrigin.Begin);

            while (true) {
                try {
                    int i = udpSocket_.Receive(buf);

                    if (i == Constants.CORPC_UDP_HANDSHAKE_SIZE) {
                        int msgType = (int)((buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3]);
                        if (msgType == Constants.CORPC_MSG_TYPE_UDP_UNSHAKE) {
                            Debug.LogErrorFormat("recv unshake");
                            recvMsgQueue_.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                            return;
                        }

                        continue;
                    }

                    int ret = Kcp.KcpInput(kcp_, buf, 0, i);
                    if (ret < 0) {
                        Debug.LogErrorFormat("kcpInput failed");
                        //return;
                    }

                    while (true) {
                        int n = Kcp.KcpPeeksize(kcp_);
                        if (n < 0)
                        {
                            break;
                        }
                        if (n == 0)
                        {
                            Debug.LogErrorFormat("network reset");
                            recvMsgQueue_.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                            return;
                        }

                        byte[] buffer = memoryStream_.GetBuffer();
                        memoryStream_.SetLength(n);
                        memoryStream_.Seek(0, SeekOrigin.Begin);
                        int count = Kcp.KcpRecv(kcp_, buffer, ushort.MaxValue);
                        if (count <= 0)
                        {
                            break;
                        }

                        Debug.Assert(count >= Constants.CORPC_MESSAGE_HEAD_SIZE);

                        uint msgLen = (uint)((buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3]);
                        int msgType = (int)((buffer[4] << 24) | (buffer[5] << 16) | (buffer[6] << 8) | buffer[7]);
                        ushort tag = (ushort)((buffer[8] << 8) | buffer[9]);
                        ushort flag = (ushort)((buffer[10] << 8) | buffer[11]);

                        Debug.Assert(count == Constants.CORPC_MESSAGE_HEAD_SIZE + (int)msgLen);

                        IMessage protoData = null;
                        if (msgType > 0) {
                            // 校验序号
                            if (enableSerial_) {
                                uint serial = (uint)((buffer[16] << 24) | (buffer[17] << 16) | (buffer[18] << 8) | buffer[19]);
                                if (serial != 0 && serial != lastRecvSerial_+1) {
                                    Debug.LogErrorFormat("serial check failed! need %d, recv %d", lastRecvSerial_, serial);
                                    recvMsgQueue_.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                                    return;
                                }

                                lastRecvSerial_++;
                            }

                            if (msgLen > 0) {
                                // 校验CRC
                                if (enableRecvCRC_) {
                                    ushort crc = (ushort)((buffer[20] << 8) | buffer[21]);
                                    ushort crc1 = CRC.CheckSum(buffer, Constants.CORPC_MESSAGE_HEAD_SIZE, 0xFFFF, (uint)msgLen);

                                    if (crc != crc1)
                                    {
                                        Debug.LogErrorFormat("crc check failed, msgType:%d, size:%d, recv:%d, cal:%d\n", msgType, msgLen, crc, crc1);
                                        recvMsgQueue_.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                                        return;
                                    }
                                }

                                // 解密
                                if ((flag & Constants.CORPC_MESSAGE_FLAG_CRYPT) != 0) {
                                    if (crypter_ == null) {
                                        Debug.LogError("cant decrypt message for crypter not exist\n");
                                        recvMsgQueue_.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                                        return;
                                    }

                                    crypter_.decrypt(buffer, Constants.CORPC_MESSAGE_HEAD_SIZE, buffer, Constants.CORPC_MESSAGE_HEAD_SIZE, (uint)msgLen);
                                }

                                protoData = Deserialize(msgType, buffer, Constants.CORPC_MESSAGE_HEAD_SIZE, (int)msgLen);
                            }
                        }

                        recvMsgQueue_.Enqueue(new ProtoMessage(msgType, tag, protoData, false));
                    }

                    Kcp.KcpFlush(kcp_);
                } catch (System.Exception ex) {
                    Debug.LogError("RecvThread error!!!");
                    Debug.LogError(ex.ToString());
                    Debug.LogError(ex.StackTrace);
                    
                    recvMsgQueue_.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                    
                    return;
                }
            }
        }

        private void SendMsgLoop()
        {
            byte[] buf = new byte[Constants.CORPC_MAX_UDP_MESSAGE_SIZE];
            while (true) {
                ProtoMessage msg = null;
                try {
                    msg = sendMsgQueue_.Dequeue();

                    Debug.Assert(msg != null);

                    switch (msg.Type) {
                    case Constants.CORPC_MSG_TYPE_DISCONNECT:
                        {
                            return;
                        }
                    case Constants.CORPC_MSG_TYPE_HEARTBEAT:
                        {
                            Write(heartbeatmsg_, (int)Constants.CORPC_MESSAGE_HEAD_SIZE);
                            break;
                        }
                    default:
                        {
                            // 构造要发出的消息数据
                            byte[] data = Serialize(msg);

                            uint dataLength = (uint)data.Length;
                            if (Constants.CORPC_MESSAGE_HEAD_SIZE + dataLength > Constants.CORPC_MAX_UDP_MESSAGE_SIZE)
                            {
                                Debug.LogError("send message size too large!!!");
                                recvMsgQueue_.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                                return;
                            }

                            ushort flag = 0;
                            if (dataLength > 0)
                            {
                                // 加密
                                if (msg.NeedCrypter)
                                {
                                    crypter_.encrypt(data, 0, buf, Constants.CORPC_MESSAGE_HEAD_SIZE, dataLength);
                                    flag |= Constants.CORPC_MESSAGE_FLAG_CRYPT;
                                }
                                else
                                {
                                    Array.Copy(data, 0, buf, Constants.CORPC_MESSAGE_HEAD_SIZE, dataLength);
                                }
                            }

                            // 设置头部
                            buf[0] = (byte)((dataLength >> 24) & 0xFF);
                            buf[1] = (byte)((dataLength >> 16) & 0xFF);
                            buf[2] = (byte)((dataLength >> 8) & 0xFF);
                            buf[3] = (byte)(dataLength & 0xFF);
                            buf[4] = (byte)((msg.Type >> 24) & 0xFF);
                            buf[5] = (byte)((msg.Type >> 16) & 0xFF);
                            buf[6] = (byte)((msg.Type >> 8) & 0xFF);
                            buf[7] = (byte)(msg.Type & 0xFF);
                            buf[8] = (byte)((msg.Tag >> 8) & 0xFF);
                            buf[9] = (byte)(msg.Tag & 0xFF);
                            buf[10] = (byte)((flag >> 8) & 0xFF);
                            buf[11] = (byte)(flag & 0xFF);

                            if (enableSerial_)
                            {
                                // _lastRecvSerial是否会导致线程同步问题？
                                buf[12] = (byte)((lastRecvSerial_ >> 24) & 0xFF);
                                buf[13] = (byte)((lastRecvSerial_ >> 16) & 0xFF);
                                buf[14] = (byte)((lastRecvSerial_ >> 8) & 0xFF);
                                buf[15] = (byte)(lastRecvSerial_ & 0xFF);
                                lastSendSerial_++;
                                buf[16] = (byte)((lastSendSerial_ >> 24) & 0xFF);
                                buf[17] = (byte)((lastSendSerial_ >> 16) & 0xFF);
                                buf[18] = (byte)((lastSendSerial_ >> 8) & 0xFF);
                                buf[19] = (byte)(lastSendSerial_ & 0xFF);
                            }

                            if (enableSendCRC_)
                            {
                                //ushort crc = CRC.CheckSum(buf, 0, 0xFFFF, Constants.CORPC_MESSAGE_HEAD_SIZE - 2);
                                //crc = CRC.CheckSum(buf, Constants.CORPC_MESSAGE_HEAD_SIZE, crc, dataLength);
                                ushort crc = CRC.CheckSum(buf, Constants.CORPC_MESSAGE_HEAD_SIZE, 0xFFFF, dataLength);

                                buf[20] = (byte)((crc >> 8) & 0xFF);
                                buf[21] = (byte)(crc & 0xFF);
                            }

                            Write(buf, (int)(Constants.CORPC_MESSAGE_HEAD_SIZE + dataLength));
                            break;
                        }
                    }
                } catch (System.Exception ex) {
                    Debug.LogError("SendMsgLoop error!!! --- ");
                    Debug.LogError(ex.ToString());
                    Debug.LogError(ex.StackTrace);
                    return;
                }
            }
        }

        unsafe int Write(byte[] buf, int nbyte)
        {
            if (buf == null || nbyte <= 0) return 0;

            int sent = 0, left = nbyte;
            const int MAX = 1200;

            fixed (byte* p = buf)               // 一次性固定整个数组
            {
                while (left > 0)
                {
                    int pkg = Math.Min(left, Constants.CORPC_MAX_KCP_PACKAGE_SIZE);
                    IntPtr ptr = new IntPtr(p + sent);   // 计算子段首地址

                    int ret = Kcp.KcpSend(kcp_, ptr, pkg);   // 见下方重载
                    if (ret < 0) return ret;

                    sent += pkg;
                    left -= pkg;
                }
            }

            Kcp.KcpFlush(kcp_);
            return nbyte;
        }
    }
}
