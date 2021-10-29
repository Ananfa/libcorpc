using System;
using System.Net;
using System.Collections.Generic;
using System.Text;
using System.Net.Sockets;
using System.Threading;
using System.IO;
using Google.Protobuf;
using UnityEngine;

namespace Corpc
{   
    public class UdpMessageClient : MessageDispatcher
    {
        private LockProtoMessageQueue _recvMsgQueue = new LockProtoMessageQueue (); // 接收消息队列
        private BlockingDequeueProtoMessageQueue _sendMsgQueue = new BlockingDequeueProtoMessageQueue (); // 发送消息队列
        private Socket _udpSocket = null; // 与服务器的连接socket

        private Thread _recvMsgThread = null; // 接收消息线程
        private Thread _sendMsgThread = null; // 发送消息线程

        private string _host;
        private int _port;
        private int _localPort;

        private long _lastRecvHBTime; // 最后一次收到心跳的时间
        private long _lastSendHBTime; // 最后一次发送心跳的时间

        private bool _running = false;

        private IMessageParser _parser = null;

        // private constructor
        public UdpMessageClient(string host, int port, int localPort, IMessageParser parser)
        {
            _host = host;
            _port = port;
            _localPort = localPort;
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
            if (_udpSocket == null) {
                try {
                    _udpSocket = new Socket (AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
                    _udpSocket.Bind(new IPEndPoint(IPAddress.Any, _localPort));
                    _udpSocket.Connect(_host, _port);

                    // handshake with server
                    if (!Handshake()) {
                        Debug.LogError("Handshake error!!!");

                        _udpSocket.Shutdown(SocketShutdown.Both);
                        _udpSocket.Close();
                        _udpSocket = null;
                        return false;
                    }

                } catch (System.Exception ex) {
                    Debug.LogError("Connect error!!!");
                    Debug.LogError(ex.ToString());
                    Debug.LogError(ex.StackTrace);

                    if (_udpSocket != null) {
                        _udpSocket.Close();
                        _udpSocket = null;
                    }

                    return false;
                }
            }

            if (!_running) {
                _lastRecvHBTime = _lastSendHBTime = DateTime.Now.Ticks / TimeSpan.TicksPerMillisecond;
    
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

        private bool Handshake()
        {
            byte[] buf = new byte[Constants.CORPC_MAX_UDP_MESSAGE_SIZE];
            byte[] handshake1msg = new byte[Constants.CORPC_MESSAGE_HEAD_SIZE];
            byte[] handshake3msg = new byte[Constants.CORPC_MESSAGE_HEAD_SIZE];

            handshake1msg[4] = (byte)((Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_1 >> 24) & 0xFF);
            handshake1msg[5] = (byte)((Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_1 >> 16) & 0xFF);
            handshake1msg[6] = (byte)((Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_1 >> 8) & 0xFF);
            handshake1msg[7] = (byte)(Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_1 & 0xFF);

            handshake3msg[4] = (byte)((Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_3 >> 24) & 0xFF);
            handshake3msg[5] = (byte)((Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_3 >> 16) & 0xFF);
            handshake3msg[6] = (byte)((Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_3 >> 8) & 0xFF);
            handshake3msg[7] = (byte)(Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_3 & 0xFF);

            // 发送handshake1消息，然后等待handshake2消息到来，超时未到则重发handshake1消息
            while (true) {
                // 发送handshake1消息
                if (_udpSocket.Send(handshake1msg) != Constants.CORPC_MESSAGE_HEAD_SIZE) {
                    Debug.LogError("Can't send handshake1");
                    return false;
                }

                if (_udpSocket.Poll(1000000, SelectMode.SelectRead)) {
                    // 接收handshake2消息
                    if (_udpSocket.Receive(buf) != Constants.CORPC_MESSAGE_HEAD_SIZE) {
                        Debug.LogError("Receive handshake2 message size error");
                        return false;
                    }

                    int msgType = (buf[4] << 24) + (buf[5] << 16) + (buf[6] << 8) + buf[7];
                    if (msgType != Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_2) {
                        Debug.LogError("Receive message not handshake2, msgType:" + msgType);
                        return false;
                    }

                    // 发hankshake3消息
                    if (_udpSocket.Send(handshake3msg) != Constants.CORPC_MESSAGE_HEAD_SIZE) {
                        Debug.LogError("Can't send handshake3");
                        return false;
                    }

                    break;
                }
            }

            return true;
        }

        public void Close()
        {
            if (_sendMsgThread != null) {
                // send close msg to sendMsgThread
                _sendMsgQueue.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, null));
                _sendMsgThread.Join();
                _sendMsgThread = null;
            }

            if (_udpSocket != null) {
                try {
                    _udpSocket.Shutdown(SocketShutdown.Both);
                    _udpSocket.Close();
                } catch (System.Exception ex) {
                    Debug.LogError("Shutdown socket error!!!");
                    Debug.LogError(ex.ToString());
                    Debug.LogError(ex.StackTrace);
                }
                _udpSocket = null;
            }

            if (_recvMsgThread != null) {
                _recvMsgThread.Join();
                _recvMsgThread = null;
            }

            _running = false;
        }

        // Update method is called by outside MonoBehaviour object's Update method
        public void Update()
        {
            // 定期检查心跳，分发消息处理，由外部的MonoBehaviour对象驱动
            if (_running) {
                long nowms = DateTime.Now.Ticks / TimeSpan.TicksPerMillisecond;

                if (nowms - _lastRecvHBTime > Constants.CORPC_MAX_NO_HEARTBEAT_TIME) {
                    // 无心跳，断线
                    Debug.LogError("heartbeat timeout");
                    Close();
                } else if (nowms - _lastSendHBTime > Constants.CORPC_HEARTBEAT_PERIOD) {
                    Send(Constants.CORPC_MSG_TYPE_HEARTBEAT, null);
                    _lastSendHBTime = nowms;
                }

                // 心跳和断线消息需要特殊处理
                ProtoMessage pmsg = _recvMsgQueue.Dequeue();
                while (pmsg != null) {
                    switch (pmsg.Type) {
                        case Constants.CORPC_MSG_TYPE_DISCONNECT:
                        case Constants.CORPC_MSG_TYPE_UDP_UNSHAKE:
                        {
                            Close();

                            // 清理收发队列（主要是发送队列）
                            // 注意：此时收发线程已经退出，对收发队列的处理没有线程同步的问题
                            _recvMsgQueue.Clear();
                            _sendMsgQueue.Clear();

                            Dispatch(Constants.CORPC_MSG_TYPE_DISCONNECT, null);
                            break;
                        }
                        case Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_2:
                        {
                            // 重发handshake_3
                            Send(Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_3, null);
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
            byte[] buf = new byte[Constants.CORPC_MAX_UDP_MESSAGE_SIZE];

            while (true) {
                try {
                    int i = _udpSocket.Receive(buf);
                    Debug.Assert(i >= Constants.CORPC_MESSAGE_HEAD_SIZE);

                    int msgLen = (buf[0] << 24) + (buf[1] << 16) + (buf[2] << 8) + buf[3];
                    int msgType = (buf[4] << 24) + (buf[5] << 16) + (buf[6] << 8) + buf[7];

                    Debug.Assert(i == Constants.CORPC_MESSAGE_HEAD_SIZE + msgLen);

                    IMessage protoData = null;
                    if (msgLen > 0)
                    {
                        protoData = Deserialize(msgType, buf, Constants.CORPC_MESSAGE_HEAD_SIZE, msgLen);
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

                        _udpSocket.Send(buf);
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
        public void Send (int type, IMessage msg)
        {
            _sendMsgQueue.Enqueue (new ProtoMessage (type, msg));
        }

        private void Serialize (MemoryStream ms, ProtoMessage msg)
        {
            if (msg.Data != null) {
                Serializer.Serialize<IMessage> (ms, msg.Data);
            }
        }

        private IMessage Deserialize (int type, byte[] data, int index, int count)
        {
            return _parser.parse (type, data, index, count);
        }

    }
}
