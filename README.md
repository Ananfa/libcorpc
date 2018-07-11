libcorpc
========
- 在腾讯开源项目libco基础上开发，拥有libco的特性
- 开发过程中对libco做了少量修改，在项目的co目录中
- 使用protobuf定义rpc服务和方法
- 支持进程间和进程内的rpc
- 结合了协程和rpc的特点，进行rpc调用时不用担心阻塞线程执行，提升线程使用效率
- RpcClient和InnerRpcClient实现是线程安全的，可在主线程创建对象然后在多个线程中并发调用rpc
- 全双工模式的RPC框架，比连接池实现方式效率更高且需要连接数更少
- 为非rpc数据传输提供了Tcp/Udp消息服务：TcpMessageServer和UdpMessageServer
- 以连接池方式提供对MySQL,Mongodb,Redis,Memcached等服务的访问
- 目前支持macOS和linux系统

***

### 架构图
![Alt 架构图](res/libcorpc架构图.png "libcorpc架构图")

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
- 进程间rpc调用benchmark程序：example/example_interRpc，在一台机器中测试，平均每秒8万+次rpc
- 进程内rpc调用benchmark程序：example/example_innerRpc，平均每秒30万+次

***

### build
- 需要GCC 4.8.3以上版本
- 使用cmake（在MacOS下可通过xcode工程生成，但xcode工程不会进行install步骤）
- 安装libco库
```libco
$ cd co && mkdir build && cd build && cmake .. && make install && cd ../..
```
- 安装libcorpc库（需要先装libprotobuf 2.6）
```libcorpc
$ cd corpc && mkdir build && cd build && cmake .. && make install && cd ../..
```
- 根据需要安装libcorpc_memcached库（需要先装libmemcached 1.0）
```libcorpc_memcached
$ cd corpc_memcached && mkdir build && cd build && cmake .. && make install && cd ../..
```
- 根据需要安装libcorpc_mongodb库（需要先装libmongoc-1.0和libbson-1.0）
```libcorpc_mongodb
$ cd corpc_mongodb && mkdir build && cd build && cmake .. && make install && cd ../..
```
- 根据需要安装libcorpc_redis库（需要先装libhiredis）
```libcorpc_redis
$ cd corpc_redis && mkdir build && cd build && cmake .. && make install && cd ../..
```
- 根据需要安装libcorpc_mysql库（需要先装libmysqlclient）
```libcorpc_mysql
$ cd corpc_mysql && mkdir build && cd build && cmake .. && make install && cd ../..
```
- Tutorial生成
```tutorial
$ cd tutorial/tutorial1 && mkdir build && cd build && cmake .. && make && cd ../../..
$ cd tutorial/tutorial2 && mkdir build && cd build && cmake .. && make && cd ../../..
$ cd tutorial/tutorial3 && mkdir build && cd build && cmake .. && make && cd ../../..
$ cd tutorial/tutorial4 && mkdir build && cd build && cmake .. && make && cd ../../..
$ cd tutorial/tutorial5 && mkdir build && cd build && cmake .. && make && cd ../../..
```
- Example生成
```example
$ cd example/example_interRpc && mkdir build && cd build && cmake .. && make && cd ../../..
$ cd example/example_innerRpc && mkdir build && cd build && cmake .. && make && cd ../../..
$ cd example/example_echoTcp && mkdir build && cd build && cmake .. && make && cd ../../..
$ cd example/example_echoUdp && mkdir build && cd build && cmake .. && make && cd ../../..
$ cd example/example_memcached && mkdir build && cd build && cmake .. && make && cd ../../..
$ cd example/example_mongodb && mkdir build && cd build && cmake .. && make && cd ../../..
$ cd example/example_mysql && mkdir build && cd build && cmake .. && make && cd ../../..
$ cd example/example_redis && mkdir build && cd build && cmake .. && make && cd ../../..
```

***

### 使用libcorpc开发一些要注意的地方
- socket族IO操作、某些第三方库函数（如：mysqlclient）、sleep方法、co_yield_ct方法等会产生协程切换
- 协程的切换实际是掌握在程序员手中
- 一个线程中的多个协程对线程资源的访问和修改不需要对资源上锁
- 如果一协程中有死循环并且循环中没有能产生协程切换的方法调用，其他协程将不会被调度执行
- 在使用第三方库时（如：mysqlclient），需要在启动程序时加上“LD_PRELOAD=<libco库路径>”（如果在mac OS中加“DYLD_INSERT_LIBRARIES=<HOOK了系统接口的动态库> DYLD_FORCE_FLAT_NAMESPACE=y”）

