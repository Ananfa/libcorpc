using System;
using UnityEngine;
using Corpc;
using Google.Protobuf;
using System.Threading.Tasks;
using System.Threading;

public class TcpTest : MonoBehaviour {

    public string _host;
    public int _port;
    public bool _needHB;

    private TcpMessageClient _client = null;
    private CancellationTokenSource _cts;

    private int _isConnectingFlag = 0;

    // Use this for initialization
    async void Start () {
        Debug.Log("TcpTest start");

        _cts = new CancellationTokenSource();
        
        if (_client == null) {
            _client = new TcpMessageClient (_host, _port, _needHB, true, true, true);
            _client.Crypter = new SimpleXORCrypter(System.Text.Encoding.UTF8.GetBytes("1234567fvxcvc"));
            _client.Register(1, FooResponse.Parser, FooResponseHandler);
            _client.Register(3, ServerReady.Parser, ServerReadyHandler);
            _client.Register(Constants.CORPC_MSG_TYPE_CONNECT, null, ConnectHandler);
            _client.Register(Constants.CORPC_MSG_TYPE_DISCONNECT, null, CloseHandler);

            await TryConnectAsync();
        }
    }
    
    // Update is called once per frame
    void Update () {
        if (_client != null && _client.Running)
        {
            // 调用客户端的Update方法处理消息
            _client.Update();

            // 定时发送消息
            //if (Time.time - _lastSendTime > SEND_INTERVAL)
            //{
            //    SendTestMessage();
            //    //_lastSendTime = Time.time;
            //}
        }
    }
    
    async Task TryConnectAsync()
    {
        if (Interlocked.CompareExchange(ref _isConnectingFlag, 1, 0) != 0)
        {
            Debug.Log("Already connecting, skip");
            return;
        }

        try
        {
            while (!_cts.IsCancellationRequested)
            {
                if (!_client.Running)
                {
                    try
                    {
                        bool connected = await _client.Start();
                        if (connected)
                        {
                            Debug.Log("Connected");
                            break;
                        }
                        else
                        {
                            Debug.Log("Connection failed, will retry in 2 seconds");
                        }
                    }
                    catch (Exception ex)
                    {
                        Debug.LogError($"Connection error: {ex.Message}");
                    }
                }
                else
                {
                    break;
                }

                // 等待2秒后重试
                try
                {
                    await Task.Delay(2000, _cts.Token);
                }
                catch (TaskCanceledException)
                {
                    break;
                }
            }
        }
        finally
        {
            // 确保总是重置标志
            Interlocked.Exchange(ref _isConnectingFlag, 0);
        }
    }

    public void FooResponseHandler(int type, IMessage msg)
    {
        // 已经在主线程，可以直接调用Unity API
        //Debug.Log("enter FooResponseHandler");
        
        //if (msg is FooResponse response)
        //{
        //    Debug.Log($"Received response: {response.Text}, Result: {response.Result}");
        //}

        SendTestMessage();
    }

    public void ServerReadyHandler(int type, IMessage msg)
    {
        // 已经在主线程，可以直接调用Unity API
        Debug.Log("enter ServerReadyHandler");
        
        //if (msg is ServerReady response)
        //{
        //    Debug.Log($"Received response: {response.Text}, Result: {response.Result}");
        //}

        SendTestMessage();
    }

    public void ConnectHandler(int type, IMessage msg)
    {
        // 已经在主线程
        Debug.Log("connected");
        
        SendReadyMessage();
    }

    public void CloseHandler(int type, IMessage msg)
    {
        // 已经在主线程
        Debug.Log("Disconnected");
        
        if (!_cts.IsCancellationRequested)
        {
            // 延迟2秒后重连
            _ = DelayedReconnectAsync(2);
        }
    }

    private async Task DelayedReconnectAsync(int delaySeconds)
    {
        try
        {
            await Task.Delay(delaySeconds * 1000, _cts.Token);
            await TryConnectAsync();
        }
        catch (TaskCanceledException)
        {
            // 取消是正常的
        }
    }

    private void SendReadyMessage()
    {
        try
        {
            ServerReady request = new ServerReady();
            request.Status = 1;
            _client.Send(3, 0, request, true);
        }
        catch (Exception ex)
        {
            Debug.LogError($"Error sending message: {ex.Message}");
        }
    }

    private void SendTestMessage()
    {
        try
        {
            FooRequest request = new FooRequest();
            request.Text = "helloworld";
            request.Times = 1;
            _client.Send(1, 0, request, true);
        }
        catch (Exception ex)
        {
            Debug.LogError($"Error sending message: {ex.Message}");
        }
    }

    async void OnDestroy()
    {
        Debug.Log("KcpTest OnDestroy");
        
        _cts?.Cancel();
        
        if (_client != null)
        {
            try
            {
                // 等待一小段时间让任务有机会清理
                await Task.Delay(100);
                
                // 关闭客户端
                _client.Close();
            }
            catch (Exception ex)
            {
                Debug.LogError($"Error during cleanup: {ex.Message}");
            }
            finally
            {
                _client = null;
            }
        }
        
        _cts?.Dispose();
    }

    // 可选：添加暂停/恢复处理
    void OnApplicationPause(bool pauseStatus)
    {
        if (!enabled) return;

        if (pauseStatus)
        {
            // 应用进入后台
            Debug.Log("Application paused, disconnecting");
            Disconnect();
        }
        else
        {
            // 应用回到前台
            Debug.Log("Application resumed, reconnecting");
            Reconnect();
        }
    }

    private void Disconnect()
    {
        if (_client != null && _client.Running)
        {
            try
            {
                _client.Close();
            }
            catch (Exception ex)
            {
                Debug.LogError($"Error disconnecting: {ex.Message}");
            }
        }
    }

    private void Reconnect()
    {
        _ = TryConnectAsync();
    }

    // 可选：添加UI按钮控制
    public void OnConnectButtonClick()
    {
        if (_client == null || !_client.Running)
        {
            _ = TryConnectAsync();
        }
    }

    public void OnDisconnectButtonClick()
    {
        if (_client != null && _client.Running)
        {
            _client.Close();
        }
    }

    public void OnSendTestMessageButtonClick()
    {
        if (_client != null && _client.Running)
        {
            SendTestMessage();
        }
        else
        {
            Debug.LogWarning("Client not connected");
        }
    }
}
