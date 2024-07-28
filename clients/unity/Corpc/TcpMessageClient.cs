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
        private TcpClient _tcpClient = null; // 与服务器的连接通道
        private Stream _stream = null; // 与服务器间的数据流

        private string _host;
        private int _port;

        private byte[] _heartbeatmsg;

        // private constructor
        public TcpMessageClient(string host, int port, bool needHB, bool enableSendCRC, bool enableRecvCRC, bool enableSerial): base(needHB, enableSendCRC, enableRecvCRC, enableSerial)
        {
            _host = host;
            _port = port;
            _needHB = needHB;
            _enableSendCRC = enableSendCRC;
            _enableRecvCRC = enableRecvCRC;
            _enableSerial = enableSerial;

            _heartbeatmsg = new byte[Constants.CORPC_MESSAGE_HEAD_SIZE];
            _heartbeatmsg[4] = (byte)((Constants.CORPC_MSG_TYPE_HEARTBEAT >> 24) & 0xFF);
            _heartbeatmsg[5] = (byte)((Constants.CORPC_MSG_TYPE_HEARTBEAT >> 16) & 0xFF);
            _heartbeatmsg[6] = (byte)((Constants.CORPC_MSG_TYPE_HEARTBEAT >> 8) & 0xFF);
            _heartbeatmsg[7] = (byte)(Constants.CORPC_MSG_TYPE_HEARTBEAT & 0xFF);
        }

        public bool Start()
        {
            if (_tcpClient == null) {
                try {
                    _tcpClient = new TcpClient(_host, _port);
                    _tcpClient.NoDelay = true;
                    _stream = _tcpClient.GetStream();
                } catch (System.Exception ex) {
                    Debug.LogError("Connect error!!!");
                    Debug.LogError(ex.ToString());
                    Debug.LogError(ex.StackTrace);

                    Debug.Assert(_tcpClient == null);

                    return false;
                }
            }

            if (!_running) {
                if (_needHB) {
                    _lastRecvHBTime = _lastSendHBTime = DateTime.Now.Ticks / TimeSpan.TicksPerMillisecond;
                }

                _lastSendSerial = 0;
    
                // 启动收发线程
                _recvMsgThread = new Thread(new ThreadStart(RecvMsgLoop));
                _sendMsgThread = new Thread(new ThreadStart(SendMsgLoop));
                
                _recvMsgThread.Start();
                _sendMsgThread.Start();

                _running = true;
                return true;
            } else {
                return false;
            }
        }

        public void Close()
        {
            if (_sendMsgThread != null) {
                // send close msg to sendMsgThread
                _sendMsgQueue.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                _sendMsgThread.Join();
                _sendMsgThread = null;
            }

            if (_tcpClient != null) {
                _tcpClient.Close();
                _tcpClient = null;
            }

            if (_recvMsgThread != null) {
                _recvMsgThread.Join();
                _recvMsgThread = null;
            }

            _stream = null;
            _running = false;

            // 清理收发队列（主要是发送队列）
            // 注意：此时收发线程已经退出，对收发队列的处理没有线程同步的问题
            _recvMsgQueue.Clear();
            _sendMsgQueue.Clear();

            HandleMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, null);
        }

        // Update method is called by outside MonoBehaviour object's Update method
        public void Update()
        {
            // 定期检查心跳，分发消息处理，由外部的MonoBehaviour对象驱动
            if (_running) {
                if (_needHB) {
                    long nowms = DateTime.Now.Ticks / TimeSpan.TicksPerMillisecond;

                    if (nowms - _lastRecvHBTime > Constants.CORPC_MAX_NO_HEARTBEAT_TIME) {
                        // 无心跳，断线
                        Debug.LogError("heartbeat timeout");
                        Close();

                    } else if (nowms - _lastSendHBTime > Constants.CORPC_HEARTBEAT_PERIOD) {
                        Send(Constants.CORPC_MSG_TYPE_HEARTBEAT, 0, null, false);
                        _lastSendHBTime = nowms;
                    }
                }

                // 心跳和断线消息需要特殊处理
                ProtoMessage pmsg = _recvMsgQueue.Dequeue();
                while (pmsg != null) {
                    switch (pmsg.Type) {
                    case Constants.CORPC_MSG_TYPE_DISCONNECT:
                        {
                            Close();
                            break;
                        }
                    case Constants.CORPC_MSG_TYPE_HEARTBEAT:
                        {
                            _lastRecvHBTime = DateTime.Now.Ticks / TimeSpan.TicksPerMillisecond;
                            break;
                        }
                    default:
                        {
                            HandleMessage(pmsg.Type, pmsg.Data);
                            break;
                        }
                    }

                    pmsg = _recvMsgQueue.Dequeue();
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
                    while (remainLen > 0)
                    {
                        remainLen -= _stream.Read(head, Constants.CORPC_MESSAGE_HEAD_SIZE - remainLen, remainLen);
                    }

                    uint msgLen = (uint)((head[0] << 24) | (head[1] << 16) | (head[2] << 8) | head[3]);
                    int msgType = (int)((head[4] << 24) | (head[5] << 16) | (head[6] << 8) | head[7]);
                    ushort tag = (ushort)((head[8] << 8) | head[9]);
                    ushort flag = (ushort)((head[10] << 8) | head[11]);

                    // 读取消息体
                    byte[] data = null;
                    if (msgLen > 0)
                    {
                        // TODO: 校验服务器发给客户端消息大小，超过阈值报警
                        // 注意：目前每个消息分配内存的实现会产生较多的回收垃圾，可以改为分配一个最大空间重复使用
                        data = new byte[msgLen];
                        remainLen = (int)msgLen;
                        while (remainLen > 0)
                        {
                            remainLen -= _stream.Read(data, (int)msgLen - remainLen, remainLen);
                            //Console.WriteLine("remainLen = " + remainLen + " : " + (DateTime.Now.Ticks/TimeSpan.TicksPerMillisecond-startTimeStamp));
                        }
                    }

                    IMessage protoData = null;
                    if (msgType > 0) {
                        // 校验序号
                        if (_enableSerial) {
                            uint serial = (uint)((head[16] << 24) | (head[17] << 16) | (head[18] << 8) | head[19]);
                            if (serial != 0 && serial != _lastRecvSerial+1) {
                                Debug.LogErrorFormat("serial check failed! need {0}, recv {1}", _lastRecvSerial, serial);
                                _recvMsgQueue.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                                return;
                            }

                            _lastRecvSerial++;
                        }

                        if (msgLen > 0) {
                            // 校验CRC
                            if (_enableRecvCRC)
                            {
                                ushort crc = (ushort)((head[20] << 8) | head[21]);
                                ushort crc1 = CRC.CheckSum(data, 0, 0xFFFF, msgLen);

                                if (crc != crc1)
                                {
                                    Debug.LogErrorFormat("crc check failed, msgType:{0}, size:{1}, recv:{2}, cal:{3}\n", msgType, msgLen, crc, crc1);
                                    _recvMsgQueue.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                                    return;
                                }
                            }

                            // 解密
                            if ((flag & Constants.CORPC_MESSAGE_FLAG_CRYPT) != 0)
                            {
                                if (_crypter == null)
                                {
                                    Debug.LogError("cant decrypt message for crypter not exist\n");
                                    _recvMsgQueue.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                                    return;
                                }

                                _crypter.decrypt(data, 0, data, 0, msgLen);
                            }

                            protoData = Deserialize(msgType, data, 0, (int)msgLen);
                        }
                    }

                    _recvMsgQueue.Enqueue(new ProtoMessage(msgType, tag, protoData, false));
                } catch (System.Exception ex) {
                    Debug.LogError("RecvThread error!!!");
                    Debug.LogError(ex.ToString());
                    Debug.LogError(ex.StackTrace);
                    
                    _recvMsgQueue.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                    
                    return;
                }
            }
        }

        private void SendMsgLoop()
        {
            while (true) {
                ProtoMessage msg = null;
                try {
                    msg = _sendMsgQueue.Dequeue();

                    Debug.Assert(msg != null);

                    switch (msg.Type) {
                    case Constants.CORPC_MSG_TYPE_DISCONNECT:
                        {
                            return;
                        }
                    case Constants.CORPC_MSG_TYPE_HEARTBEAT:
                        {
                            _stream.Write(_heartbeatmsg, 0, Constants.CORPC_MESSAGE_HEAD_SIZE);
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
                                    _recvMsgQueue.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                                    return;
                                }

                                buf = new byte[Constants.CORPC_MESSAGE_HEAD_SIZE + dataLength];

                                // 加密
                                if (msg.NeedCrypter)
                                {
                                    _crypter.encrypt(data, 0, buf, Constants.CORPC_MESSAGE_HEAD_SIZE, dataLength);
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

                            if (_enableSerial)
                            {
                                // _lastRecvSerial是否会导致线程同步问题？
                                buf[12] = (byte)((_lastRecvSerial >> 24) & 0xFF);
                                buf[13] = (byte)((_lastRecvSerial >> 16) & 0xFF);
                                buf[14] = (byte)((_lastRecvSerial >> 8) & 0xFF);
                                buf[15] = (byte)(_lastRecvSerial & 0xFF);
                                _lastSendSerial++;
                                buf[16] = (byte)((_lastSendSerial >> 24) & 0xFF);
                                buf[17] = (byte)((_lastSendSerial >> 16) & 0xFF);
                                buf[18] = (byte)((_lastSendSerial >> 8) & 0xFF);
                                buf[19] = (byte)(_lastSendSerial & 0xFF);
                            }

                            if (_enableSendCRC)
                            {
                                //ushort crc = CRC.CheckSum(buf, 0, 0xFFFF, Constants.CORPC_MESSAGE_HEAD_SIZE - 2);
                                //crc = CRC.CheckSum(buf, Constants.CORPC_MESSAGE_HEAD_SIZE, crc, dataLength);
                                ushort crc = CRC.CheckSum(buf, Constants.CORPC_MESSAGE_HEAD_SIZE, 0xFFFF, dataLength);

                                buf[20] = (byte)((crc >> 8) & 0xFF);
                                buf[21] = (byte)(crc & 0xFF);
                            }

                            _stream.Write(buf, 0, Constants.CORPC_MESSAGE_HEAD_SIZE + (int)dataLength);
                            break;
                        }
                    }
                } catch (System.Exception ex) {
                    Debug.LogError("SendMsgLoop error!!! --- ");
                    Debug.LogError(ex.ToString());
                    Debug.LogError(ex.StackTrace);
                    _recvMsgQueue.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                    return;
                }
            }
        }

    }
}
