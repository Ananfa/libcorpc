using System;
using UnityEngine;
using ProtoBuf;
using Corpc;
using echo;

public class UdpTest : MonoBehaviour {

    public string _host;
    public int _port;

    private UdpMessageClient _client = null;
    private System.Random _rnd = new System.Random();

    // Use this for initialization
    void Start () {
        DebugConsole.Log("UdpTest start");
        if (_client == null) {
            _client = new UdpMessageClient (_host, _port, 10000+_rnd.Next(10000), new MyMessageParser ());

            _client.Register (1, FooResponseHandler);
            _client.Register (-1, CloseHandler);

            TryConnect ();
        }
    }
    
    // Update is called once per frame
    void Update () {
        if (_client.Running) {
            FooRequest request = new FooRequest ();
            request.text = "helloworld";
            request.times = 1;

            _client.Send (1, request);

            _client.Update ();
        }
    }
    
    void TryConnect() {
        if (!_client.Running) {
            if (!_client.Start ()) {
                Invoke ("TryConnect", 2);
            } else {
                DebugConsole.Log ("Connected");
            }
        }
    }

    public void FooResponseHandler(int type, IExtensible msg) {
        //Debug.Log ("enter FooResponseHandler");
    }

    public void CloseHandler(int type, IExtensible msg) {
        DebugConsole.Log("Disconnected");
        Invoke ("TryConnect", 2);
    }
 
}
