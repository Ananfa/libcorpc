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
    public class UdpMessageClient : MessageClient
    {
        private Socket _udpSocket = null; // 与服务器的连接socket

        private string _host;
        private int _port;
        private int _localPort;

        private bool _shakeOK;

        private byte[] _handshake1msg;
        private byte[] _handshake3msg;

        // private constructor
        public UdpMessageClient(string host, int port, int localPort, bool needHB, bool enableSendCRC, bool enableRecvCRC): base(needHB, enableSendCRC, enableRecvCRC, false)
        {
            _host = host;
            _port = port;
            _localPort = localPort;

            _shakeOK = false;

            _handshake1msg = new byte[Constants.CORPC_MESSAGE_HEAD_SIZE];
            _handshake3msg = new byte[Constants.CORPC_MESSAGE_HEAD_SIZE];

            _handshake1msg[4] = (byte)((Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_1 >> 8) & 0xFF);
            _handshake1msg[5] = (byte)(Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_1 & 0xFF);

            _handshake3msg[4] = (byte)((Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_3 >> 8) & 0xFF);
            _handshake3msg[5] = (byte)(Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_3 & 0xFF);
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
                if (_needHB) {
                    _lastRecvHBTime = _lastSendHBTime = DateTime.Now.Ticks / TimeSpan.TicksPerMillisecond;
                }
    
                // 启动收发线程
                _recvMsgThread = new Thread(new ThreadStart(RecvMsgLoop));
                
                _recvMsgThread.Start();

                _running = true;
                return true;
            } else {
                return false;
            }
        }

        private bool Handshake()
        {
            byte[] buf = new byte[Constants.CORPC_MAX_UDP_MESSAGE_SIZE];

            // 发送handshake1消息，然后等待handshake2消息到来，超时未到则重发handshake1消息
            while (true) {
                // 发送handshake1消息
                if (_udpSocket.Send(_handshake1msg) != Constants.CORPC_MESSAGE_HEAD_SIZE) {
                    Debug.LogError("Can't send handshake1");
                    return false;
                }

                if (_udpSocket.Poll(1000000, SelectMode.SelectRead)) {
                    // 接收handshake2消息
                    if (_udpSocket.Receive(buf) != Constants.CORPC_MESSAGE_HEAD_SIZE) {
                        Debug.LogError("Receive handshake2 message size error");
                        return false;
                    }

                    short msgType = (short)((buf[4] << 8) + buf[5]);
                    if (msgType != Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_2) {
                        Debug.LogError("Receive message not handshake2, msgType:" + msgType);
                        return false;
                    }

                    // 发hankshake3消息
                    _udpSocket.Send(_handshake3msg);

                    break;
                }
            }

            return true;
        }

        public void Close()
        {
            if (_sendMsgThread != null) {
                // send close msg to sendMsgThread
                _sendMsgQueue.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
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

            _shakeOK = false;

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
                long nowms = DateTime.Now.Ticks / TimeSpan.TicksPerMillisecond;

                if (nowms - _lastRecvHBTime > Constants.CORPC_MAX_NO_HEARTBEAT_TIME) {
                    // 无心跳，断线
                    Debug.LogError("heartbeat timeout");
                    Close();
                } else if (nowms - _lastSendHBTime > Constants.CORPC_HEARTBEAT_PERIOD) {
                    if (_shakeOK) {
                        Send(Constants.CORPC_MSG_TYPE_HEARTBEAT, 0, null, false);
                        _lastSendHBTime = nowms;
                    }
                }

                // 心跳和断线消息需要特殊处理
                ProtoMessage pmsg = _recvMsgQueue.Dequeue();
                while (pmsg != null) {
                    switch (pmsg.Type) {
                        case Constants.CORPC_MSG_TYPE_DISCONNECT:
                        case Constants.CORPC_MSG_TYPE_UDP_UNSHAKE:
                        {
                            Close();
                            break;
                        }
                        case Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_2:
                        {
                            if (!_shakeOK) {
                                // 重发handshake_3
                                try {
                                    _udpSocket.Send(_handshake3msg);
                                }  catch (System.Exception ex) {
                                    Debug.LogError("Send handshake3 failed");
                                    Debug.LogError(ex.ToString());
                                    Debug.LogError(ex.StackTrace);
                                    Close();
                                    break;
                                }
                            }
                            break;
                        }
                        case Constants.CORPC_MSG_TYPE_UDP_HANDSHAKE_4:
                        {
                            _shakeOK = true;
                            Debug.Log("shake succeed");

                            // 启动发送线程
                            _sendMsgThread = new Thread(new ThreadStart(SendMsgLoop));
                            _sendMsgThread.Start();

                            break;
                        }
                        case Constants.CORPC_MSG_TYPE_HEARTBEAT:
                        {
                            _lastRecvHBTime = DateTime.Now.Ticks / TimeSpan.TicksPerMillisecond;
                            break;
                        }
                        default:
                        {
                            if (!_shakeOK) {
                                Debug.LogError("recv msg when unshaked");
                                Close();
                                break;
                            }

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
            byte[] buf = new byte[Constants.CORPC_MAX_UDP_MESSAGE_SIZE];

            while (true) {
                try {
                    int i = _udpSocket.Receive(buf);
                    Debug.Assert(i >= Constants.CORPC_MESSAGE_HEAD_SIZE);

                    int msgLen = (buf[0] << 24) + (buf[1] << 16) + (buf[2] << 8) + buf[3];
                    short msgType = (short)((buf[4] << 8) + buf[5]);
                    ushort tag = (ushort)((buf[6] << 8) + buf[7]);
                    ushort flag = (ushort)((buf[8] << 8) + buf[9]);

                    Debug.Assert(i == Constants.CORPC_MESSAGE_HEAD_SIZE + msgLen);

                    IMessage protoData = null;
                    if (msgType > 0)
                    {
                        // 校验序号
                        if (_enableSerial) {
                            uint serial = (uint)((buf[14] << 24) + (buf[15] << 16) + (buf[16] << 8) + buf[17]);
                            if (serial != 0 && serial != _lastRecvSerial+1) {
                                Debug.LogErrorFormat("serial check failed! need %d, recv %d", _lastRecvSerial, serial);
                                _recvMsgQueue.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                                return;
                            }

                            _lastRecvSerial++;
                        }

                        if (msgLen > 0) {
                            // 校验CRC
                            if (_enableRecvCRC)
                            {
                                ushort crc = (ushort)((buf[18] << 8) + buf[19]);
                                ushort crc1 = CRC.CheckSum(buf, Constants.CORPC_MESSAGE_HEAD_SIZE, 0xFFFF, (uint)msgLen);

                                if (crc != crc1)
                                {
                                    Debug.LogErrorFormat("crc check failed, msgType:%d, size:%d, recv:%d, cal:%d\n", msgType, msgLen, crc, crc1);
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

                                _crypter.decrypt(buf, Constants.CORPC_MESSAGE_HEAD_SIZE, buf, Constants.CORPC_MESSAGE_HEAD_SIZE, (uint)msgLen);
                            }

                            protoData = Deserialize(msgType, buf, Constants.CORPC_MESSAGE_HEAD_SIZE, msgLen);
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
            byte[] buf = new byte[Constants.CORPC_MAX_UDP_MESSAGE_SIZE];
            while (true) {
                ProtoMessage msg = null;
                try {
                    msg = _sendMsgQueue.Dequeue();

                    Debug.Assert(msg != null);

                    if (msg.Type == Constants.CORPC_MSG_TYPE_DISCONNECT) {
                        return;
                    } else {
                        // 构造要发出的消息数据
                        byte[] data = Serialize(msg);

                        uint dataLength = (uint)data.Length;
                        if (Constants.CORPC_MESSAGE_HEAD_SIZE + dataLength > Constants.CORPC_MAX_UDP_MESSAGE_SIZE)
                        {
                            Debug.LogError("send message size too large!!!");
                            _recvMsgQueue.Enqueue(new ProtoMessage(Constants.CORPC_MSG_TYPE_DISCONNECT, 0, null, false));
                            return;
                        }

                        ushort flag = 0;
                        if (dataLength > 0)
                        {
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

                        // 设置头部
                        buf[0] = (byte)((dataLength >> 24) & 0xFF);
                        buf[1] = (byte)((dataLength >> 16) & 0xFF);
                        buf[2] = (byte)((dataLength >> 8) & 0xFF);
                        buf[3] = (byte)(dataLength & 0xFF);
                        buf[4] = (byte)((msg.Type >> 8) & 0xFF);
                        buf[5] = (byte)(msg.Type & 0xFF);
                        buf[6] = (byte)((msg.Tag >> 8) & 0xFF);
                        buf[7] = (byte)(msg.Tag & 0xFF);
                        buf[8] = (byte)((flag >> 8) & 0xFF);
                        buf[9] = (byte)(flag & 0xFF);

                        if (_enableSerial)
                        {
                            // _lastRecvSerial是否会导致线程同步问题？
                            buf[10] = (byte)((_lastRecvSerial >> 24) & 0xFF);
                            buf[11] = (byte)((_lastRecvSerial >> 16) & 0xFF);
                            buf[12] = (byte)((_lastRecvSerial >> 8) & 0xFF);
                            buf[13] = (byte)(_lastRecvSerial & 0xFF);
                            _lastSendSerial++;
                            buf[14] = (byte)((_lastSendSerial >> 24) & 0xFF);
                            buf[15] = (byte)((_lastSendSerial >> 16) & 0xFF);
                            buf[16] = (byte)((_lastSendSerial >> 8) & 0xFF);
                            buf[17] = (byte)(_lastSendSerial & 0xFF);
                        }

                        if (_enableSendCRC)
                        {
                            ushort crc = CRC.CheckSum(buf, 0, 0xFFFF, Constants.CORPC_MESSAGE_HEAD_SIZE - 2);
                            crc = CRC.CheckSum(buf, Constants.CORPC_MESSAGE_HEAD_SIZE, crc, dataLength);

                            buf[18] = (byte)((crc >> 8) & 0xFF);
                            buf[19] = (byte)(crc & 0xFF);
                        }

                        _udpSocket.Send(buf, (int)(Constants.CORPC_MESSAGE_HEAD_SIZE + dataLength), SocketFlags.None);
                    }
                } catch (System.Exception ex) {
                    Debug.LogError("SendMsgLoop error!!! --- ");
                    Debug.LogError(ex.ToString());
                    Debug.LogError(ex.StackTrace);
                    return;
                }
            }
        }
    }
}
