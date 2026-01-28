using System;
using System.Net;
using System.Buffers;
using System.Collections.Generic;
using System.Text;
using System.Net.Sockets;
using System.Net.Sockets.Kcp;
using System.Threading;
using System.Threading.Tasks;
using System.IO;
using Google.Protobuf;
using UnityEngine;

namespace Corpc
{   
    public class KcpMessageClient : MessageClient, IKcpCallback
    {
        private Socket udpSocket_ = null; // 与服务器的连接socket
        private CancellationTokenSource cts_ = null;

        private string host_;
        private int port_;
        public int localPort_ { get; set; }

        private byte[] handshake1msg_;
        private byte[] handshake3msg_;

        private byte[] heartbeatmsg_;

        public SimpleSegManager.Kcp kcp { get; set; }

        // private constructor
        public KcpMessageClient(string host, int port, bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial): base(needHB, enableSendCRC, enableRecvCRC, enableSerial)
        {
            host_ = host;
            port_ = port;

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
        }

        public async Task<bool> Start()
        {
            if (localPort_ == 0) {
                Debug.LogError("The localport value is not set.");
                return false;
            }

            if (udpSocket_ == null) {
                try {
                    udpSocket_ = new Socket (AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
                    udpSocket_.Bind(new IPEndPoint(IPAddress.Any, localPort_));
                    udpSocket_.Connect(host_, port_);

                    // handshake with server
                    if (!await Handshake()) {
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
                cts_ = new CancellationTokenSource();
                kcp = new SimpleSegManager.Kcp(1, this);
                kcp.Interval(0);

                // 注意：关闭的时候清理会导致send任务卡死，因此改为在重连时清理
                recvMsgQueue_.Clear();
                sendMsgQueue_.Clear();

                if (needHB_) {
                    lastRecvHBTime_ = lastSendHBTime_ = DateTime.UtcNow.Ticks / TimeSpan.TicksPerMillisecond;
                }
    
                lastSendSerial_ = 0;
                lastRecvSerial_ = 0;
    
                HandleMessage(Constants.CORPC_MSG_TYPE_CONNECT, null);
    
                // 启动收发任务
                _ = Task.Run(() => RecvMsgLoop(cts_.Token));
                _ = Task.Run(() => SendMsgLoop(cts_.Token));

                running_ = true;
                return true;
            } else {
                return false;
            }
        }

        private async Task<bool> Handshake()
        {
            byte[] buf = new byte[Constants.CORPC_MAX_UDP_MESSAGE_SIZE];
            int step = 1;
            
            while (true) {
                if (step == 1) {
                    // 发送handshake1消息
                    Debug.Log("send handshake1");
                    int sent = await udpSocket_.SendAsync(new ArraySegment<byte>(handshake1msg_), SocketFlags.None);
                    if (sent != Constants.CORPC_UDP_HANDSHAKE_SIZE) {
                        Debug.LogError("Can't send handshake1");
                        return false;
                    }

                    Debug.Log("recving handshake2");
                    // 接收handshake2消息，带超时
                    var receiveTask = udpSocket_.ReceiveAsync(new ArraySegment<byte>(buf), SocketFlags.None);
                    var timeoutTask = Task.Delay(1000);
                    var completedTask = await Task.WhenAny(receiveTask, timeoutTask);
                    
                    if (completedTask == receiveTask) {
                        int received = receiveTask.Result;
                        if (received != Constants.CORPC_UDP_HANDSHAKE_SIZE) {
                            Debug.LogError("Receive handshake2 message size error");
                            return false;
                        }

                        int msgType = (int)((buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3]);
                        if (msgType != Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_2) {
                            Debug.LogError("Receive message not handshake2, msgType:" + msgType);
                            return false;
                        }

                        Debug.Log("recved handshake2");
                        step = 2;
                    }
                } else {
                    Debug.Log("send handshake3");
                    // 发送handshake3消息
                    int sent = await udpSocket_.SendAsync(new ArraySegment<byte>(handshake3msg_), SocketFlags.None);
                    if (sent != Constants.CORPC_UDP_HANDSHAKE_SIZE) {
                        Debug.LogError("Can't send handshake3");
                        return false;
                    }

                    Debug.Log("recving handshake4");
                    // 接收handshake4消息，带超时
                    var receiveTask = udpSocket_.ReceiveAsync(new ArraySegment<byte>(buf), SocketFlags.None);
                    var timeoutTask = Task.Delay(1000);
                    var completedTask = await Task.WhenAny(receiveTask, timeoutTask);
                    
                    if (completedTask == receiveTask) {
                        int received = receiveTask.Result;
                        if (received != Constants.CORPC_UDP_HANDSHAKE_SIZE) {
                            Debug.LogError("Receive handshake4 message size error");
                            return false;
                        }

                        int msgType = (int)((buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3]);
                        if (msgType != Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_4) {
                            Debug.LogError("Receive message not handshake4, msgType:" + msgType);
                            return false;
                        }

                        Debug.Log("recved handshake4");
                        break;
                    }
                }
            }

            return true;
        }

        public void Close()
        {
            running_ = false;

            if (cts_ != null && !cts_.IsCancellationRequested) {
                // 注意：这里要先close udp socket，不然receiveTask_会被卡死
                if (udpSocket_ != null) {
                    try {
                        udpSocket_.Close();  // 直接在主线程调用
                    } catch (System.Exception ex) {
                        Debug.LogError("Close socket error!!!");
                        Debug.LogError(ex.ToString());
                        Debug.LogError(ex.StackTrace);
                    }
                }

                // 发送断开连接消息
                sendMsgQueue_.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));

                cts_.Cancel();
                cts_.Dispose();
                cts_ = null;
                udpSocket_ = null;
            }

            kcp.Dispose();

            // 注意：不能在这里清理收发队列
            //recvMsgQueue_.Clear();
            //sendMsgQueue_.Clear();

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
                    return;
                } else if (nowms - lastSendHBTime_ > Constants.CORPC_HEARTBEAT_PERIOD) {
                    Send(Constants.CORPC_MSG_TYPE_HEARTBEAT, 0, null, false);
                    lastSendHBTime_ = nowms;
                }

                DateTimeOffset dateTimeOffset = DateTimeOffset.FromUnixTimeMilliseconds(nowms);
                kcp.Update(dateTimeOffset);

                // 心跳和断线消息需要特殊处理
                ProtoMessage pmsg = recvMsgQueue_.DequeueWithTimeout(0);
                while (pmsg != null) {
                    switch (pmsg.Type) {
                    case Constants.CORPC_MSG_TYPE_DISCONNECT:
                        {
                            Debug.Log("disconnect");
                            Close();
                            return;
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

                    pmsg = recvMsgQueue_.DequeueWithTimeout(0);
                }
            }
        }

        private async Task RecvMsgLoop(CancellationToken ct)
        {
            byte[] buf = new byte[Constants.CORPC_MAX_UDP_MESSAGE_SIZE];

            while (!ct.IsCancellationRequested) {
                try {
                    // 注意： udp socket的ReceiveAsync在socket close后不会退出，因此需要先Poll查看是否有数据
                    if (udpSocket_.Poll(100, SelectMode.SelectRead)) {
                        var receiveTask = udpSocket_.ReceiveAsync(new ArraySegment<byte>(buf), SocketFlags.None, ct);
                        int i = await receiveTask;

                        // 如果连接被关闭，i可能为0
                        if (i == 0) {
                            Debug.Log("Connection closed gracefully");
                            recvMsgQueue_.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                            return;
                        }

                        if (i == Constants.CORPC_UDP_HANDSHAKE_SIZE) {
                            int mType = (int)((buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3]);
                            if (mType == Constants.CORPC_MSG_TYPE_UDP_UNSHAKE) {
                                Debug.LogErrorFormat("recv unshake");
                                recvMsgQueue_.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                                return;
                            }
                            continue;
                        }

                        //Debug.LogErrorFormat("kcpInput {0}", i);
                        int ret = kcp.Input(buf.AsSpan(0, i));
                        if (ret < 0) {
                            Debug.LogErrorFormat("kcpInput failed {0} {1}", i, ret);
                            //return;
                        }

                        while (true) {
                            var (bufferOwner, avalidLength) = kcp.TryRecv();
                            if (bufferOwner == null) {
                                break;
                            }

                            Debug.Assert(avalidLength >= Constants.CORPC_MESSAGE_HEAD_SIZE);

                            int offset = 0;

                            while (offset < avalidLength) {
                                byte[] headbuf = bufferOwner.Memory.Slice(offset, Constants.CORPC_MESSAGE_HEAD_SIZE).ToArray();

                                //byte[] buffer = bufferOwner.Memory.Slice(0, avalidLength).ToArray();

                                uint msgLen = (uint)((headbuf[0] << 24) | (headbuf[1] << 16) | (headbuf[2] << 8) | headbuf[3]);
                                int msgType = (int)((headbuf[4] << 24) | (headbuf[5] << 16) | (headbuf[6] << 8) | headbuf[7]);
                                ushort tag = (ushort)((headbuf[8] << 8) | headbuf[9]);
                                ushort flag = (ushort)((headbuf[10] << 8) | headbuf[11]);

                                offset += Constants.CORPC_MESSAGE_HEAD_SIZE;

                                Debug.Assert(avalidLength >= offset + (int)msgLen);

                                //Debug.Assert(avalidLength == Constants.CORPC_MESSAGE_HEAD_SIZE + (int)msgLen);
                                if (avalidLength != Constants.CORPC_MESSAGE_HEAD_SIZE + (int)msgLen) {
                                    Debug.LogErrorFormat("avalidLength {0} msgLen {1} msgType {2} tag {3} flag {4}", avalidLength, msgLen, msgType, tag, flag);
                                }

                                IMessage protoData = null;
                                if (msgType > 0) {
                                    // 校验序号
                                    if (enableSerial_) {
                                        uint serial = (uint)((headbuf[16] << 24) | (headbuf[17] << 16) | (headbuf[18] << 8) | headbuf[19]);
                                        if (serial != 0 && serial != lastRecvSerial_+1) {
                                            Debug.LogErrorFormat("serial check failed! need {0}, recv {1}", lastRecvSerial_, serial);
                                            recvMsgQueue_.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                                            return;
                                        }

                                        lastRecvSerial_++;
                                    }

                                    if (msgLen > 0) {
                                        byte[] buffer = bufferOwner.Memory.Slice(offset, (int)msgLen).ToArray();
                                        offset += (int)msgLen;

                                        // 校验CRC
                                        if (enableRecvCRC_) {
                                            ushort crc = (ushort)((headbuf[20] << 8) | headbuf[21]);
                                            ushort crc1 = CRC.CheckSum(buffer, 0, 0xFFFF, (uint)msgLen);

                                            if (crc != crc1)
                                            {
                                                Debug.LogErrorFormat("crc check failed, msgType:{0}, size:{1}, recv:{2}, cal:{3}\n", msgType, msgLen, crc, crc1);
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

                                            crypter_.decrypt(buffer, 0, buffer, 0, (uint)msgLen);
                                        }

                                        protoData = Deserialize(msgType, buffer, 0, (int)msgLen);
                                    }
                                }

                                recvMsgQueue_.Enqueue(new ProtoMessage(msgType, tag, protoData, false));
                            }
                        }
                    }
                } catch (OperationCanceledException) {
                    // 任务被取消，正常退出
                    Debug.Log("Receive task cancelled");
                    break;
                } catch (ObjectDisposedException ex) {
                    // Socket已被释放
                    Debug.LogError("Socket disposed, stopping receive loop");
                    Debug.LogError(ex.ToString());
                    Debug.LogError(ex.StackTrace);
                    break;
                } catch (SocketException ex) {
                    // Socket错误
                    Debug.LogError($"Socket error in RecvMsgLoop: {ex.SocketErrorCode}");
                    recvMsgQueue_.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                    break;
                } catch (System.Exception ex) {
                    Debug.LogError("RecvThread error!!!");
                    Debug.LogError(ex.ToString());
                    Debug.LogError(ex.StackTrace);
                    
                    recvMsgQueue_.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                    break;
                }
            }
Debug.LogError("RecvMsgLoop ----- finished");
        }

        private async Task SendMsgLoop(CancellationToken ct)
        {
            byte[] buf = new byte[Constants.CORPC_MAX_UDP_MESSAGE_SIZE];
            
            while (!ct.IsCancellationRequested) {
                try {
                    // 使用异步方式等待消息
                    ProtoMessage msg = await sendMsgQueue_.DequeueAsync(ct);//await Task.Run(() => sendMsgQueue_.DequeueWithTimeout(1000), ct.Token);
                    
                    if (msg == null) continue;

                    switch (msg.Type) {
                        case Constants.CORPC_MSG_TYPE_DISCONNECT:
Debug.LogError("SendMsgLoop ----- finished 1");
                            return;
                        case Constants.CORPC_MSG_TYPE_HEARTBEAT:
                            Debug.Log("send heartbeat");
                            kcp.Send(heartbeatmsg_.AsSpan());
                            //await udpSocket_.SendAsync(new ArraySegment<byte>(heartbeatmsg_, 0, Constants.CORPC_MESSAGE_HEAD_SIZE), SocketFlags.None);
                            break;
                        default:
                            //Debug.Log($"send msg: {msg.Type}");
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
                                ushort crc = CRC.CheckSum(buf, Constants.CORPC_MESSAGE_HEAD_SIZE, 0xFFFF, dataLength);
                                buf[20] = (byte)((crc >> 8) & 0xFF);
                                buf[21] = (byte)(crc & 0xFF);
                            }

                            kcp.Send(buf.AsSpan(0, (int)(Constants.CORPC_MESSAGE_HEAD_SIZE + dataLength)));
                            //await udpSocket_.SendAsync(new ArraySegment<byte>(buf, 0, (int)(Constants.CORPC_MESSAGE_HEAD_SIZE + dataLength)), SocketFlags.None);
                            break;
                    }
                } catch (OperationCanceledException) {
                    // 任务被取消，正常退出
Debug.LogError("SendMsgLoop ----- finished 2");
                    break;
                } catch (System.Exception ex) {
                    Debug.LogError("SendMsgLoop error!!! --- ");
                    Debug.LogError(ex.ToString());
                    Debug.LogError(ex.StackTrace);
Debug.LogError("SendMsgLoop ----- finished 3");
                    return;
                }
            }
        }

        public void Output(IMemoryOwner<byte> buffer, int avalidLength)
        {
            using (buffer)
            {
                try
                {
                    var span = buffer.Memory.Span.Slice(0, avalidLength);
                    
                    // 同步发送
                    int bytesSent = udpSocket_.Send(span, SocketFlags.None);
                    
                    if (bytesSent != avalidLength)
                    {
                        Debug.LogWarning($"Sent {bytesSent} bytes, expected {avalidLength}");
                    }
                }
                catch (Exception ex)
                {
                    Debug.LogError($"Send error: {ex.Message}");
                    throw;
                }
            }
        }
    }
}