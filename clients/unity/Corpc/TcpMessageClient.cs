using System;
using System.Collections.Generic;
using System.Text;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using System.IO;
using Google.Protobuf;
using UnityEngine;

namespace Corpc
{   
    public class TcpMessageClient : MessageClient
    {
        private TcpClient tcpClient_ = null;
        private NetworkStream stream_ = null;
        private CancellationTokenSource cts_ = null;

        private string host_;
        private int port_;

        private byte[] heartbeatmsg_;

        public TcpMessageClient(string host, int port, bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial): base(needHB, enableSendCRC, enableRecvCRC, enableSerial)
        {
            host_ = host;
            port_ = port;

            heartbeatmsg_ = new byte[Constants.CORPC_MESSAGE_HEAD_SIZE];
            heartbeatmsg_[4] = (byte)((Constants.CORPC_MSG_TYPE_HEARTBEAT >> 24) & 0xFF);
            heartbeatmsg_[5] = (byte)((Constants.CORPC_MSG_TYPE_HEARTBEAT >> 16) & 0xFF);
            heartbeatmsg_[6] = (byte)((Constants.CORPC_MSG_TYPE_HEARTBEAT >> 8) & 0xFF);
            heartbeatmsg_[7] = (byte)(Constants.CORPC_MSG_TYPE_HEARTBEAT & 0xFF);
        }

        public async Task<bool> Start()
        {
            if (tcpClient_ == null) {
                try {
                    tcpClient_ = new TcpClient();
                    await tcpClient_.ConnectAsync(host_, port_);
                    tcpClient_.NoDelay = true;
                    stream_ = tcpClient_.GetStream();
                } catch (System.Exception ex) {
                    Debug.LogError("Connect error!!!");
                    Debug.LogError(ex.ToString());
                    Debug.LogError(ex.StackTrace);

                    tcpClient_?.Close();
                    tcpClient_ = null;
                    return false;
                }
            }

            if (!running_) {
                cts_ = new CancellationTokenSource();
                
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

        public void Close()
        {
            if (cts_ != null && !cts_.IsCancellationRequested) {
                // 发送断开消息
                sendMsgQueue_.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                
                // 取消任务
                cts_.Cancel();
                cts_.Dispose();
                cts_ = null;
            }

            if (tcpClient_ != null) {
                tcpClient_.Close();
                tcpClient_ = null;
            }

            stream_ = null;
            running_ = false;

            // 注意：不能在这里清理收发队列
            //recvMsgQueue_.Clear();
            //sendMsgQueue_.Clear();

            HandleMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, null);
        }

        // Update方法由外部MonoBehaviour对象调用
        public void Update()
        {
            if (running_) {
                if (needHB_) {
                    long nowms = DateTime.UtcNow.Ticks / TimeSpan.TicksPerMillisecond;

                    if (nowms - lastRecvHBTime_ > Constants.CORPC_MAX_NO_HEARTBEAT_TIME) {
                        // 心跳超时，断开连接
                        Debug.LogError("heartbeat timeout");
                        Close();

                    } else if (nowms - lastSendHBTime_ > Constants.CORPC_HEARTBEAT_PERIOD) {
                        Send(Constants.CORPC_MSG_TYPE_HEARTBEAT, 0, null, false);
                        lastSendHBTime_ = nowms;
                    }
                }

                // 处理接收到的消息
                ProtoMessage pmsg = recvMsgQueue_.DequeueWithTimeout(0);
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

                    pmsg = recvMsgQueue_.DequeueWithTimeout(0);
                }
            }
        }

        private async Task RecvMsgLoop(CancellationToken ct)
        {
            byte[] head = new byte[Constants.CORPC_MESSAGE_HEAD_SIZE];
            
            while (!ct.IsCancellationRequested) {
                try {
                    // 读取消息头部
                    int remainLen = Constants.CORPC_MESSAGE_HEAD_SIZE;
                    while (remainLen > 0) {
                        int bytesRead = await stream_.ReadAsync(head, Constants.CORPC_MESSAGE_HEAD_SIZE - remainLen, remainLen, ct);
                        if (bytesRead == 0) {
                            // 连接已关闭
                            recvMsgQueue_.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                            return;
                        }
                        remainLen -= bytesRead;
                    }

                    uint msgLen = (uint)((head[0] << 24) | (head[1] << 16) | (head[2] << 8) | head[3]);
                    int msgType = (int)((head[4] << 24) | (head[5] << 16) | (head[6] << 8) | head[7]);
                    ushort tag = (ushort)((head[8] << 8) | head[9]);
                    ushort flag = (ushort)((head[10] << 8) | head[11]);

                    // 读取消息体
                    byte[] data = null;
                    if (msgLen > 0) {
                        data = new byte[msgLen];
                        remainLen = (int)msgLen;
                        while (remainLen > 0) {
                            int bytesRead = await stream_.ReadAsync(data, (int)msgLen - remainLen, remainLen, ct);
                            if (bytesRead == 0) {
                                // 连接已关闭
                                recvMsgQueue_.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                                return;
                            }
                            remainLen -= bytesRead;
                        }
                    }

                    if (msgType == Constants.CORPC_MSG_TYPE_JUMP_SERIAL) {
                        int serial = (int)((head[16] << 24) | (head[17] << 16) | (head[18] << 8) | head[19]);
                        if (serial <= lastRecvSerial_) {
                            Debug.LogErrorFormat("serial check failed! need {0}, recv {1}", lastRecvSerial_, serial);
                            recvMsgQueue_.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                            return;
                        }

                        lastRecvSerial_ = serial;
                        continue;
                    }

                    IMessage protoData = null;
                    if (msgType > 0) {
                        // 校验序号
                        if (enableSerial_) {
                            int serial = (int)((head[16] << 24) | (head[17] << 16) | (head[18] << 8) | head[19]);
                            if (serial != 0 && serial != lastRecvSerial_ + 1) {
                                Debug.LogErrorFormat("serial check failed! need {0}, recv {1}", lastRecvSerial_, serial);
                                recvMsgQueue_.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                                return;
                            }

                            if (serial != 0) {
                                lastRecvSerial_ = serial;
                            }
                        }

                        if (msgLen > 0) {
                            // 校验CRC
                            if (enableRecvCRC_) {
                                ushort crc = (ushort)((head[20] << 8) | head[21]);
                                ushort crc1 = CRC.CheckSum(data, 0, 0xFFFF, msgLen);

                                if (crc != crc1) {
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

                                crypter_.decrypt(data, 0, data, 0, msgLen);
                            }

                            protoData = Deserialize(msgType, data, 0, (int)msgLen);
                        }
                    }

                    recvMsgQueue_.Enqueue(new ProtoMessage(msgType, tag, protoData, false));
                } catch (OperationCanceledException) {
                    // 任务被取消，正常退出
                    return;
                } catch (System.Exception ex) {
                    if (!ct.IsCancellationRequested) {
                        Debug.LogError("ReceiveMsgLoop error!!!");
                        Debug.LogError(ex.ToString());
                        Debug.LogError(ex.StackTrace);
                        
                        recvMsgQueue_.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                    }
                    return;
                }
            }
        }

        private async Task SendMsgLoop(CancellationToken ct)
        {
            while (!ct.IsCancellationRequested) {
                try {
                    // 异步等待消息
                    ProtoMessage msg = await sendMsgQueue_.DequeueAsync(ct);//await Task.Run(() => sendMsgQueue_.DequeueWithTimeout(Timeout.Infinite));

                    if (msg == null) {
                        continue;
                    }

                    switch (msg.Type) {
                    case Constants.CORPC_MSG_TYPE_DISCONNECT:
                        {
                            return;
                        }
                    case Constants.CORPC_MSG_TYPE_HEARTBEAT:
                        {
                            await stream_.WriteAsync(heartbeatmsg_, 0, Constants.CORPC_MESSAGE_HEAD_SIZE, ct);
                            break;
                        }
                    default:
                        {
                            uint dataLength = 0;
                            byte[] buf = null;
                            ushort flag = 0;
                            if (msg.Data != null) {
                                // 构造要发出的消息数据
                                byte[] data = Serialize(msg);

                                dataLength = (uint)data.Length;
                                if (Constants.CORPC_MESSAGE_HEAD_SIZE + dataLength > Constants.CORPC_MAX_MESSAGE_SIZE) {
                                    Debug.LogError("send message size too large!!!");
                                    recvMsgQueue_.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                                    return;
                                }

                                buf = new byte[Constants.CORPC_MESSAGE_HEAD_SIZE + dataLength];

                                // 加密
                                if (msg.NeedCrypter) {
                                    crypter_.encrypt(data, 0, buf, Constants.CORPC_MESSAGE_HEAD_SIZE, dataLength);
                                    flag |= Constants.CORPC_MESSAGE_FLAG_CRYPT;
                                } else {
                                    Array.Copy(data, 0, buf, Constants.CORPC_MESSAGE_HEAD_SIZE, dataLength);
                                }
                            } else {
                                buf = new byte[Constants.CORPC_MESSAGE_HEAD_SIZE];
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

                            if (enableSerial_) {
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

                            if (enableSendCRC_) {
                                ushort crc = CRC.CheckSum(buf, Constants.CORPC_MESSAGE_HEAD_SIZE, 0xFFFF, dataLength);
                                buf[20] = (byte)((crc >> 8) & 0xFF);
                                buf[21] = (byte)(crc & 0xFF);
                            }

                            await stream_.WriteAsync(buf, 0, Constants.CORPC_MESSAGE_HEAD_SIZE + (int)dataLength, ct);
                            break;
                        }
                    }
                } catch (OperationCanceledException) {
                    // 任务被取消，正常退出
                    return;
                } catch (System.Exception ex) {
                    if (!ct.IsCancellationRequested) {
                        Debug.LogError("SendMsgLoop error!!! --- ");
                        Debug.LogError(ex.ToString());
                        Debug.LogError(ex.StackTrace);
                        recvMsgQueue_.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                    }
                    return;
                }
            }
        }
    }
}