//
//  client.cpp
//  testtcp
//
//  Created by Xianke Liu on 2018/4/19.
//Copyright © 2018年 Dena. All rights reserved.
//

#include "corpc_routine_env.h"
#include "corpc_io.h"

#include <signal.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include <google/protobuf/message.h>
#include "foo.pb.h"

// 注意：此客户端实现只是用于测试server端功能，不是游戏客户端参考使用的代码，游戏客户端的网络实现需要另外实现与服务器的连接、数据收发、编解码

static int g_cnt = 0;

static void *log_routine( void *arg )
{
    co_enable_hook_sys();
    
    int total = 0;
    int average = 0;
    
    time_t startAt = time(NULL);
    
    while (true) {
        sleep(1);
        
        total += g_cnt;
        
        if (total == 0) {
            startAt = time(NULL);
            continue;
        }
        
        time_t now = time(NULL);
        
        time_t difTime = now - startAt;
        if (difTime > 0) {
            average = total / difTime;
        } else {
            average = total;
        }
        
        printf("time %ld seconds, cnt: %d, average: %d, total: %d\n", difTime, g_cnt, average, total);
        
        g_cnt = 0;
    }
    
    return NULL;
}


typedef std::function<void(std::shared_ptr<google::protobuf::Message>, std::shared_ptr<CoRpc::Connection>)> MessageHandle;

class TestTcpClient {
    
private:
    struct RegisterMessageInfo {
        int32_t type;
        google::protobuf::Message *proto;
        bool needCoroutine;
        MessageHandle handle;
    };
    
    struct SendMessageInfo {
        int32_t type;
        bool isRaw;
        std::shared_ptr<void> msg;  // 当isRaw为true时，msg中存的是std::string指针，当isRaw为false时，msg中存的是google::protobuf::Message指针。这是为了广播或转发消息给玩家时不需要对数据进行protobuf编解码
    };
    
    struct WorkerTask {
        int32_t type; // 正数类型消息为proto消息，负数类型消息用于系统消息，如：建立连接(-1)、断开连接(-2)
        std::shared_ptr<CoRpc::Connection> connection;  // 消息的连接来源
        std::shared_ptr<google::protobuf::Message> msg; // 注意：当type为-1或-2时，msg中无数据
        // TODO: Gateway服务器有转发功能（需另外实现），且转发消息时，需要的是std::shared_ptr<std::string>类型或带id列表的数据
    };
    
    class Connection: public CoRpc::Connection {
    public:
        Connection(int fd, TestTcpClient* client);
        virtual ~Connection();
        
        virtual void onClose();
        
        TestTcpClient *getClient() { return _client; }
    private:
        TestTcpClient *_client;
    };
    
    class Worker: public CoRpc::CoroutineWorker {
    public:
        Worker(TestTcpClient *client): _client(client) {}
        virtual ~Worker() {}
        
    protected:
        static void *taskCallRoutine( void * arg );
        
        virtual void handleMessage(void *msg); // 注意：处理完消息需要自己删除msg
        
    private:
        TestTcpClient *_client;
    };
    
public:
    static TestTcpClient* create(CoRpc::IO *io, bool needHB, const std::string& ip, uint16_t port);
    
    bool registerMessage(int type,
                         google::protobuf::Message *proto,
                         bool needCoroutine,
                         MessageHandle handle);
    
    static void fooHandle(std::shared_ptr<google::protobuf::Message> msg, std::shared_ptr<CoRpc::Connection> conn);
    
private:
    TestTcpClient(CoRpc::IO *io, bool needHB, const std::string& ip, uint16_t port);
    virtual ~TestTcpClient() {}
    
    static void *connectRoutine( void * arg ); // 建立与服务器的连接
    
    static void *sendFooRequestRoutine( void * arg ); // 测试协程
    
    static void* decode(std::shared_ptr<CoRpc::Connection>& connection, uint8_t *head, uint8_t *body, int size);
    
    static bool encode(std::shared_ptr<CoRpc::Connection>& connection, std::shared_ptr<void>& data, uint8_t *buf, int space, int &size);
    
    void setConnection(std::shared_ptr<CoRpc::Connection>& connection);
protected:
    virtual bool start();
    
    virtual CoRpc::PipelineFactory * getPipelineFactory();
    
    virtual void onConnect(std::shared_ptr<CoRpc::Connection>& connection);
    
    virtual void onClose(std::shared_ptr<CoRpc::Connection>& connection);
    
private:
    CoRpc::IO *_io;
    
    // 服务器的ip和port
    std::string _ip;
    uint16_t _port;
    
    bool _needHB;
    
    Worker *_worker;
    
    CoRpc::TcpPipelineFactory *_pipelineFactory;
    
    std::map<int, RegisterMessageInfo> _registerMessageMap;
    
    std::shared_ptr<CoRpc::Connection> _connection;
};

TestTcpClient::Connection::Connection(int fd, TestTcpClient* client): CoRpc::Connection(fd, client->_io, client->_needHB), _client(client) {
    
}

TestTcpClient::Connection::~Connection() {}

void TestTcpClient::Connection::onClose() {
    std::shared_ptr<CoRpc::Connection> self = CoRpc::Connection::shared_from_this();
    _client->onClose(self);
}

void * TestTcpClient::Worker::taskCallRoutine( void * arg ) {
    WorkerTask *task = (WorkerTask *)arg;
    
    std::shared_ptr<TestTcpClient::Connection> conn = std::static_pointer_cast<TestTcpClient::Connection>(task->connection);
    
    TestTcpClient *client = static_cast<TestTcpClient *>(conn->getClient());
    
    auto iter = client->_registerMessageMap.find(task->type);
    
    iter->second.handle(task->msg, task->connection);
    
    delete task;
    
    return NULL;
}

void TestTcpClient::Worker::handleMessage(void *msg) {
    // 注意：处理完消息需要自己删除msg
    WorkerTask *task = (WorkerTask *)msg;
    
    switch (task->type) {
        case -1: // 新连接建立
            // TODO:
            break;
        case -2: // 连接断开
            // TODO:
            break;
        default: {
            assert(task->type > 0);
            // 其他消息处理
            auto iter = _client->_registerMessageMap.find(task->type);
            if (iter == _client->_registerMessageMap.end()) {
                printf("ERROR: TestTcpClient::Worker::handleMessage -- unknown msg type\n");
                
                return;
            }
            
            if (iter->second.needCoroutine) {
                CoRpc::RoutineEnvironment::startCoroutine(taskCallRoutine, task);
            } else {
                iter->second.handle(task->msg, task->connection);
                
                delete task;
            }
            
            break;
        }
    }
}

TestTcpClient::TestTcpClient(CoRpc::IO *io, bool needHB, const std::string& ip, uint16_t port): _io(io), _needHB(needHB), _ip(ip), _port(port) {
    _worker = new Worker(this);
    
    _pipelineFactory = new CoRpc::TcpPipelineFactory(_worker, decode, encode, CORPC_MESSAGE_HEAD_SIZE, CORPC_MAX_MESSAGE_SIZE, 0, CoRpc::Pipeline::FOUR_BYTES);
}

TestTcpClient* TestTcpClient::create(CoRpc::IO *io, bool needHB, const std::string& ip, uint16_t port) {
    TestTcpClient *client = new TestTcpClient(io, needHB, ip, port);
    
    client->start();
    return client;
}

bool TestTcpClient::registerMessage(int type,
                     google::protobuf::Message *proto,
                     bool needCoroutine,
                                    MessageHandle handle) {
    RegisterMessageInfo info;
    info.type = type;
    info.proto = proto;
    info.needCoroutine = info.needCoroutine;
    info.handle = handle;
    
    _registerMessageMap.insert(std::make_pair(type, info));
    
    return false;
}

void TestTcpClient::fooHandle(std::shared_ptr<google::protobuf::Message> msg, std::shared_ptr<CoRpc::Connection> conn) {
    std::shared_ptr<Connection> connection = std::static_pointer_cast<Connection>(conn);
    FooResponse * resp = static_cast<FooResponse*>(msg.get());
    
    g_cnt++;
    //printf("Response is %s\n", resp->text().c_str());
    
    CoRpc::RoutineEnvironment::startCoroutine(sendFooRequestRoutine, connection->getClient());
}

void *TestTcpClient::connectRoutine( void * arg ) {
    // 建立连接
    TestTcpClient *client = (TestTcpClient *)arg;
    
    int fd = socket(PF_INET, SOCK_STREAM, 0);
    co_set_timeout(fd, -1, 1000);
    printf("co %d socket fd %d\n", co_self(), fd);
    
    struct sockaddr_in addr;
    
    bzero(&addr,sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(client->_port);
    int nIP = 0;
    if (client->_ip.empty() ||
        client->_ip.compare("0") == 0 ||
        client->_ip.compare("0.0.0.0") == 0 ||
        client->_ip.compare("*") == 0) {
        nIP = htonl(INADDR_ANY);
    } else {
        nIP = inet_addr(client->_ip.c_str());
    }
    
    addr.sin_addr.s_addr = nIP;
    
    int ret = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    
    if ( ret >= 0 ) {
        std::shared_ptr<CoRpc::Connection> connection(new Connection(fd, client));
        std::shared_ptr<CoRpc::Pipeline> pipeline = client->getPipelineFactory()->buildPipeline(connection);
        connection->setPipeline(pipeline);
        
        client->setConnection(connection);
    }
    
    return NULL;
}

void *TestTcpClient::sendFooRequestRoutine( void * arg ) {
    TestTcpClient *client = (TestTcpClient*)arg;
    
    std::shared_ptr<FooRequest> request(new FooRequest);
    
    request->set_text("HelloWorld!");
    request->set_times(2);
    
    std::shared_ptr<SendMessageInfo> sendInfo(new SendMessageInfo);
    sendInfo->type = 1;
    sendInfo->isRaw = false;
    sendInfo->msg = request;
    
    client->_connection->send(sendInfo);
    
    return NULL;
}

void* TestTcpClient::decode(std::shared_ptr<CoRpc::Connection> &connection, uint8_t *head, uint8_t *body, int size) {
    std::shared_ptr<Connection> conn = std::static_pointer_cast<Connection>(connection);
    
    TestTcpClient *client = conn->getClient();
    
    uint32_t bodySize = *(uint32_t *)head;
    bodySize = ntohl(bodySize);
    int32_t msgType = *(int32_t *)(head + 4);
    msgType = ntohl(msgType);
    
    // 处理系统类型消息，如：心跳
    // 注意：如果是UDP握手消息怎么办？
    if (msgType < 0) {
        if (msgType == CORPC_MSG_TYPE_UDP_HEARTBEAT) {
            struct timeval now = { 0 };
            gettimeofday( &now,NULL );
            uint64_t nowms = now.tv_sec;
            nowms *= 1000;
            nowms += now.tv_usec / 1000;
            
            connection->setLastRecvHBTime(nowms);
        } else {
            printf("Warning: MessageServer::decode -- recv system message: %d\n", msgType);
        }
        
        return nullptr;
    }
    
    auto iter = client->_registerMessageMap.find(msgType);
    if (iter == client->_registerMessageMap.end()) {
        printf("ERROR: TestTcpClient::decode -- unknown msg type %d for connection %d\n", msgType, connection->getfd());
        
        return nullptr;
    }
    
    google::protobuf::Message *msg = iter->second.proto->New();
    if (!msg->ParseFromArray(body, size)) {
        // 出错处理
        printf("ERROR: TestTcpClient::decode -- parse msg body fail\n");
        
        return nullptr;
    }
    
    WorkerTask *task = new WorkerTask;
    task->type = msgType;
    task->connection = connection;
    task->msg = std::shared_ptr<google::protobuf::Message>(msg);
    
    return task;
}

bool TestTcpClient::encode(std::shared_ptr<CoRpc::Connection> &connection, std::shared_ptr<void>& data, uint8_t *buf, int space, int &size) {
    std::shared_ptr<SendMessageInfo> msgInfo = std::static_pointer_cast<SendMessageInfo>(data);
    uint32_t msgSize;
    if (msgInfo->isRaw) {
        std::shared_ptr<std::string> msg = std::static_pointer_cast<std::string>(msgInfo->msg);
        
        msgSize = msg?msg->size():0;
        if (msgSize + CORPC_MESSAGE_HEAD_SIZE >= space) {
            return true;
        }
        
        if (msgSize) {
            uint8_t * msgBuf = (uint8_t *)msg->data();
            memcpy(buf + CORPC_MESSAGE_HEAD_SIZE, msgBuf, msgSize);
        }
    } else {
        std::shared_ptr<google::protobuf::Message> msg = std::static_pointer_cast<google::protobuf::Message>(msgInfo->msg);
        
        msgSize = msg->GetCachedSize();
        if (msgSize == 0) {
            msgSize = msg->ByteSize();
        }
        
        if (msgSize + CORPC_MESSAGE_HEAD_SIZE >= space) {
            return true;
        }
        
        msg->SerializeWithCachedSizesToArray(buf + CORPC_MESSAGE_HEAD_SIZE);
    }
    
    *(uint32_t *)buf = htonl(msgSize);
    *(uint32_t *)(buf + 4) = htonl(msgInfo->type);
    size = CORPC_MESSAGE_HEAD_SIZE + msgSize;
    
    return true;
}

void TestTcpClient::setConnection(std::shared_ptr<CoRpc::Connection>& connection) {
    _connection = connection;
    
    _io->addConnection(connection);
    
    // 判断是否需要心跳
    if (connection->needHB()) {
        CoRpc::Heartbeater::Instance().addConnection(connection);
    }
    
    // 通知连接建立
    onConnect(connection);
}

bool TestTcpClient::start() {
    _worker->start();
    
    CoRpc::RoutineEnvironment::startCoroutine(connectRoutine, this);
    
    return true;
}

CoRpc::PipelineFactory * TestTcpClient::getPipelineFactory() {
    return _pipelineFactory;
}

void TestTcpClient::onConnect(std::shared_ptr<CoRpc::Connection>& connection) {
    printf("TestTcpClient::onConnect -- fd %d\n", connection->getfd());
    
    CoRpc::RoutineEnvironment::startCoroutine(sendFooRequestRoutine, this);
}

void TestTcpClient::onClose(std::shared_ptr<CoRpc::Connection>& connection) {
    printf("TestTcpClient::onClose -- fd %d\n", connection->getfd());
}

int main(int argc, const char * argv[]) {
    co_start_hook();
    
    if(argc<3){
        printf("Usage:\n"
               "testtcpclt [IP] [PORT]\n");
        return -1;
    }
    
    std::string ip = argv[1];
    unsigned short int port = atoi(argv[2]);
    
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigaction( SIGPIPE, &sa, NULL );
    
    // 注册服务
    CoRpc::IO *io = CoRpc::IO::create(1, 1);
    
    for (int i=0; i<100; i++) {
        TestTcpClient *client = TestTcpClient::create(io, false, ip, port);
    
        client->registerMessage(1, new FooResponse, false, TestTcpClient::fooHandle);
    }
    
    CoRpc::RoutineEnvironment::startCoroutine(log_routine, NULL);
    
    CoRpc::RoutineEnvironment::runEventLoop();
}
