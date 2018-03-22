libcorpc
========
- libcorpc是在腾讯开源项目libco基础上开发的RPC库，拥有libco的特性
- libcorpc开发过程中对libco做了少量修改，在项目的co目录中
- libcorpc使用protobuf定义rpc服务和方法
- libcorpc支持进程间和进程内的rpc
- libcorpc结合了协程和rpc的特点，进行rpc调用时不用担心阻塞线程执行，提升线程使用效率
- libcorpc的IO层可支持其他非rpc的数据传输，需要继承IO::Connection实现相关Connection

***

### 架构图
![Alt 架构图](res/libcorpc架构图.png "libcorpc架构图")

TODO: 架构部件介绍
***

### 类图
![Alt 类图](res/libcorpc类图.png "libcorpc类图")

TODO: 类介绍
***

### proto
- 采用google protobuf 2.6.1
- corpc_option.proto

```protobuf
import "google/protobuf/descriptor.proto";

package corpc;

extend google.protobuf.ServiceOptions {
    optional uint32 global_service_id = 10000;  // 用于定义每个service的id
}

extend google.protobuf.MethodOptions {
    optional bool need_coroutine = 10002;       // 是否新开协程执行方法
    optional bool not_care_response = 10003;    // 是否关心结果，若不关心结果，则相当于单向消息发送且不知道对方是否成功接收
}

message Void {}
```

- 定义rpc服务proto文件时，需要先 import "corpc_option.proto";

- global_service_id为每个service定义id，注册到同一个CoRpc::Server中的service的id不能重复
- need_coroutine设置是否启动新协程来调用rpc服务实现方法，默认为false（tutorial3和tutorial5有关于该选项的用例）
- not_care_response设置为true时相当于单向消息传递，默认为false
- 例子：helloworld.proto

```protobuf
import "corpc_option.proto";

option cc_generic_services = true;

message FooRequest {
    required string msg1 = 1;
    required string msg2 = 2;
}

message FooResponse {
    required string msg = 1;
}

service HelloWorldService {
    option (corpc.global_service_id) = 1;

    rpc foo(FooRequest) returns(FooResponse);
}
```

***

### tutorial
- Tutorial1:一个简单的Hello World例子
- Tutorial2:在Tutorial1基础上加一个中间服务器
- Tutorial3:proto定义中“need_coroutine”选项的使用
- Tutorial4:进程内RPC
- Tutorial5:服务器间递归调用

***

### benchmark
- 测试环境：MacBook Pro, macOS 10.12.6, cpu: 2.6GHz i5, 一台机器
- 进程间rpc调用benchmark程序：服务器test/rpcsvr、客户端test/rpccli，在一台机器中测试，平均每秒7到8万次rpc
- 进程内rpc调用benchmark程序：test/innerRpc，平均每秒30万+次

***

### build
- 需要GCC 4.8.3以上版本