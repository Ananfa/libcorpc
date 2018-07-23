using System;
using System.Collections.Generic;
using System.Text;
using System.Net.Sockets;
using System.Threading;
using System.IO;
using ProtoBuf;
using UnityEngine;

namespace Corpc
{   
    public class TcpMessageClient : MessageDispatcher
    {
        private LockProtoMessageQueue _recvMsgQueue = new LockProtoMessageQueue (); // 接收消息队列
        private BlockingDequeueProtoMessageQueue _sendMsgQueue = new BlockingDequeueProtoMessageQueue (); // 发送消息队列
        private TcpClient _tcpClient = null; // 与服务器的连接通道
        private Stream _stream = null; // 与服务器间的数据流
        private Thread _recvMsgThread = null; // 接收消息线程
        private Thread _sendMsgThread = null; // 发送消息线程

        private string _host;
        private int _port;

        private bool _needHB;
        private long _lastRecvHBTime; // 最后一次收到心跳的时间
        private long _lastSendHBTime; // 最后一次发送心跳的时间

        private bool _running = false;

        private IMessageParser _parser = null;

        // private constructor
        public TcpMessageClient(string host, int port, bool needHB, IMessageParser parser)
        {
            _host = host;
            _port = port;
            _needHB = needHB;
            _parser = parser;
        }

        public bool Running
        {
            get
            {
                //Some other code
                return _running;
            }
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
                _sendMsgQueue.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, null));
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
                        Send(Constants.CORPC_MSG_TYPE_HEARTBEAT, null);
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

                            // 清理收发队列（主要是发送队列）
                            // 注意：此时收发线程已经退出，对收发队列的处理没有线程同步的问题
                            _recvMsgQueue.Clear();
                            _sendMsgQueue.Clear();

                            Dispatch(Constants.CORPC_MSG_TYPE_DISCONNECT, null);
                            break;
                        }
                    case Constants.CORPC_MSG_TYPE_HEARTBEAT:
                        {
                            _lastRecvHBTime = DateTime.Now.Ticks / TimeSpan.TicksPerMillisecond;
                            break;
                        }
                    default:
                        {
                            Dispatch(pmsg.Type, pmsg.Data);
                            break;
                        }
                    }

                    pmsg = _recvMsgQueue.Dequeue();
                }
            }
        }

        private void RecvMsgLoop()
        {
            byte[] head = new byte[8];
            while (true) {
                try {
                    // 读出消息头部8字节（4字节类型+4字节长度）
                    int remainLen = 8;
                    while (remainLen > 0)
                    {
                        remainLen -= _stream.Read(head, 8 - remainLen, remainLen);
                    }

                    int msgLen = (head[0] << 24) + (head[1] << 16) + (head[2] << 8) + head[3];
                    int msgType = (head[4] << 24) + (head[5] << 16) + (head[6] << 8) + head[7];

                    IExtensible protoData = null;
                    if (msgLen > 0)
                    {
                        byte[] data = new byte[msgLen];
                        remainLen = msgLen;
                        while (remainLen > 0)
                        {
                            remainLen -= _stream.Read(data, msgLen - remainLen, remainLen);
                            //Console.WriteLine("remainLen = " + remainLen + " : " + (DateTime.Now.Ticks/TimeSpan.TicksPerMillisecond-startTimeStamp));
                        }

                        protoData = Deserialize(msgType, data, 0, msgLen);
                    }

                    _recvMsgQueue.Enqueue(new ProtoMessage(msgType, protoData));
                } catch (System.Exception ex) {
                    Debug.LogError("RecvThread error!!!");
                    Debug.LogError(ex.ToString());
                    Debug.LogError(ex.StackTrace);
                    
                    _recvMsgQueue.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, null));
                    
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

                    if (msg.Type == Constants.CORPC_MSG_TYPE_DISCONNECT) {
                        return;
                    } else {
                        // 构造要发出的消息数据
                        MemoryStream ms = new MemoryStream();
                        Serialize(ms, msg);

                        int dataLength = (int)ms.Length;
                        byte[] buf = new byte[8 + dataLength];

                        // 设置头部
                        buf[0] = (byte)((dataLength >> 24) & 0xFF);
                        buf[1] = (byte)((dataLength >> 16) & 0xFF);
                        buf[2] = (byte)((dataLength >> 8) & 0xFF);
                        buf[3] = (byte)(dataLength & 0xFF);
                        buf[4] = (byte)((msg.Type >> 24) & 0xFF);
                        buf[5] = (byte)((msg.Type >> 16) & 0xFF);
                        buf[6] = (byte)((msg.Type >> 8) & 0xFF);
                        buf[7] = (byte)(msg.Type & 0xFF);

                        ms.Position = 0;
                        int remainLen = dataLength;
                        while (remainLen > 0)
                        {
                            remainLen -= ms.Read(buf, 8 + dataLength - remainLen, remainLen);
                        }
                        _stream.Write(buf, 0, 8 + dataLength);
                    }
                } catch (System.Exception ex) {
                    Debug.LogError("SendMsgLoop error!!! --- ");
                    Debug.LogError(ex.ToString());
                    Debug.LogError(ex.StackTrace);
                    return;
                }
            }
        }

        // 异步发送数据
        public void Send(int type, IExtensible msg)
        {
            _sendMsgQueue.Enqueue(new ProtoMessage(type, msg));
        }

        private void Serialize(MemoryStream ms, ProtoMessage msg)
        {
            if (msg.Data != null) {
                Serializer.Serialize<IExtensible>(ms, msg.Data);
            }
        }

        private IExtensible Deserialize(int type, byte[] data, int index, int count)
        {
            return _parser.parse(type, data, index, count);
        }

    }
}
