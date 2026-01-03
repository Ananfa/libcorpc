using System;
using System.Collections.Generic;
using System.Text;
using System.Net.Sockets;
using System.Threading;
using System.IO;
using Google.Protobuf;
using UnityEngine;

namespace Corpc
{   
    public class TcpMessageClient : MessageClient
    {
        private TcpClient tcpClient_ = null; // 与服务器的连接通道
        private Stream stream_ = null; // 与服务器间的数据流

        private string host_;
        private int port_;

        private byte[] heartbeatmsg_;

        // private constructor
        public TcpMessageClient(string host, int port, bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial): base(needHB, enableSendCRC, enableRecvCRC, enableSerial)
        {
            host_ = host;
            port_ = port;
            needHB_ = needHB;
            enableSendCRC_ = enableSendCRC;
            enableRecvCRC_ = enableRecvCRC;
            enableSerial_ = enableSerial;

            heartbeatmsg_ = new byte[Constants.CORPC_MESSAGE_HEAD_SIZE];
            heartbeatmsg_[4] = (byte)((Constants.CORPC_MSG_TYPE_HEARTBEAT >> 24) & 0xFF);
            heartbeatmsg_[5] = (byte)((Constants.CORPC_MSG_TYPE_HEARTBEAT >> 16) & 0xFF);
            heartbeatmsg_[6] = (byte)((Constants.CORPC_MSG_TYPE_HEARTBEAT >> 8) & 0xFF);
            heartbeatmsg_[7] = (byte)(Constants.CORPC_MSG_TYPE_HEARTBEAT & 0xFF);
        }

        public bool Start()
        {
            if (tcpClient_ == null) {
                try {
                    tcpClient_ = new TcpClient(host_, port_);
                    tcpClient_.NoDelay = true;
                    stream_ = tcpClient_.GetStream();
                } catch (System.Exception ex) {
                    Debug.LogError("Connect error!!!");
                    Debug.LogError(ex.ToString());
                    Debug.LogError(ex.StackTrace);

                    Debug.Assert(tcpClient_ == null);

                    return false;
                }
            }

            if (!running_) {
                if (needHB_) {
                    lastRecvHBTime_ = lastSendHBTime_ = DateTime.UtcNow.Ticks / TimeSpan.TicksPerMillisecond;
                }

                lastSendSerial_ = 0;
    
                // 启动收发线程
                recvMsgThread_ = new Thread(new ThreadStart(RecvMsgLoop));
                sendMsgThread_ = new Thread(new ThreadStart(SendMsgLoop));
                
                recvMsgThread_.Start();
                sendMsgThread_.Start();

                running_ = true;
                return true;
            } else {
                return false;
            }
        }

        public void Close()
        {
            if (sendMsgThread_ != null) {
                // send close msg to sendMsgThread
                sendMsgQueue_.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                sendMsgThread_.Join();
                sendMsgThread_ = null;
            }

            if (tcpClient_ != null) {
                tcpClient_.Close();
                tcpClient_ = null;
            }

            if (recvMsgThread_ != null) {
                recvMsgThread_.Join();
                recvMsgThread_ = null;
            }

            stream_ = null;
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
                if (needHB_) {
                    long nowms = DateTime.UtcNow.Ticks / TimeSpan.TicksPerMillisecond;

                    if (nowms - lastRecvHBTime_ > Constants.CORPC_MAX_NO_HEARTBEAT_TIME) {
                        // 无心跳，断线
                        Debug.LogError("heartbeat timeout");
                        Close();

                    } else if (nowms - lastSendHBTime_ > Constants.CORPC_HEARTBEAT_PERIOD) {
                        Send(Constants.CORPC_MSG_TYPE_HEARTBEAT, 0, null, false);
                        lastSendHBTime_ = nowms;
                    }
                }

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

        private void RecvMsgLoop()
        {
            byte[] head = new byte[Constants.CORPC_MESSAGE_HEAD_SIZE];
            while (true) {
                try {
                    // 读出消息头部（4字节类型+4字节长度）
                    // |body size(4 bytes)|message type(4 bytes)|tag(2 byte)|flag(2 byte)|req serial number(4 bytes)|serial number(4 bytes)|crc(2 bytes)|
                    int remainLen = Constants.CORPC_MESSAGE_HEAD_SIZE;
                    while (remainLen > 0) {
                        remainLen -= stream_.Read(head, Constants.CORPC_MESSAGE_HEAD_SIZE - remainLen, remainLen);
                    }

                    uint msgLen = (uint)((head[0] << 24) | (head[1] << 16) | (head[2] << 8) | head[3]);
                    int msgType = (int)((head[4] << 24) | (head[5] << 16) | (head[6] << 8) | head[7]);
                    ushort tag = (ushort)((head[8] << 8) | head[9]);
                    ushort flag = (ushort)((head[10] << 8) | head[11]);

                    // 读取消息体
                    byte[] data = null;
                    if (msgLen > 0) {
                        // TODO: 校验服务器发给客户端消息大小，超过阈值报警
                        // 注意：目前每个消息分配内存的实现会产生较多的回收垃圾，可以改为分配一个最大空间重复使用
                        data = new byte[msgLen];
                        remainLen = (int)msgLen;
                        while (remainLen > 0) {
                            remainLen -= stream_.Read(data, (int)msgLen - remainLen, remainLen);
                            //Console.WriteLine("remainLen = " + remainLen + " : " + (DateTime.UtcNow.Ticks/TimeSpan.TicksPerMillisecond-startTimeStamp));
                        }
                    }

                    if (msgType == Constants.CORPC_MSG_TYPE_JUMP_SERIAL) {
                        uint serial = (uint)((head[16] << 24) | (head[17] << 16) | (head[18] << 8) | head[19]);
                        if (serial <= lastRecvSerial_) {
                            Debug.LogErrorFormat("serial check failed! need {0}, recv {1}", lastRecvSerial_, serial);
                            recvMsgQueue_.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                            return;
                        }

                        lastRecvSerial_ = serial;
                        return;
                    }

                    IMessage protoData = null;
                    if (msgType > 0) {
                        // 校验序号
                        if (enableSerial_) {
                            uint serial = (uint)((head[16] << 24) | (head[17] << 16) | (head[18] << 8) | head[19]);
                            if (serial != 0 && serial != lastRecvSerial_+1) {
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
                            stream_.Write(heartbeatmsg_, 0, Constants.CORPC_MESSAGE_HEAD_SIZE);
                            break;
                        }
                    default:
                        {
                            uint dataLength = 0;
                            byte[] buf = null;
                            ushort flag = 0;
                            if (msg.Data != null)
                            {
                                // 构造要发出的消息数据
                                byte[] data = Serialize(msg);

                                dataLength = (uint)data.Length;
                                if (Constants.CORPC_MESSAGE_HEAD_SIZE + dataLength > Constants.CORPC_MAX_MESSAGE_SIZE)
                                {
                                    Debug.LogError("send message size too large!!!");
                                    recvMsgQueue_.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                                    return;
                                }

                                buf = new byte[Constants.CORPC_MESSAGE_HEAD_SIZE + dataLength];

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
                            else
                            {
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

                            stream_.Write(buf, 0, Constants.CORPC_MESSAGE_HEAD_SIZE + (int)dataLength);
                            break;
                        }
                    }
                } catch (System.Exception ex) {
                    Debug.LogError("SendMsgLoop error!!! --- ");
                    Debug.LogError(ex.ToString());
                    Debug.LogError(ex.StackTrace);
                    recvMsgQueue_.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                    return;
                }
            }
        }

    }
}
