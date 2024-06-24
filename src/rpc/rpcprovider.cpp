#include "rpcprovider.h"

// 框架提供给外部的发布 rpc 方法的函数接口
void RpcProvider::NotifyService(google::protobuf::Service* service)
{
    ServiceInfo service_info;
    // 1. 从服务信息里拿到服务对象的描述信息
    const google::protobuf::ServiceDescriptor* serviceDesc = service->GetDescriptor();
    // 2. 从服务对象的描述信息里拿到服务的名字
    std::string service_name = serviceDesc->name();
    // 3. 从服务对象的描述信息里拿到服务对象的方法的数量
    int methodCount = serviceDesc->method_count();
    std::cout << "service_name: " << service_name << std::endl;
    // 4. 获取服务对象指定下标的服务方法的描述
    for (int i = 0; i < methodCount; i++)
    {
        const google::protobuf::MethodDescriptor* methodDesc = serviceDesc->method(i);
        std::string method_name = methodDesc->name();
        service_info._methodMap.insert({method_name, methodDesc});
    }
    service_info._service = service;
    _serviceMap.insert({service_name, service_info});
}
// 启动 rpc 服务节点, 开始提供 rpc 远程网络调用服务
void RpcProvider::Run(int nodeIndex, short port)
{
    // 1. 获取服务节点信息
    char* ipC;
    char hname[128];
    struct hostent* hent;
    gethostname(hname, sizeof(hname));
    hent = gethostbyname(hname);
    for (int i = 0; hent->h_addr_list[i]; i++)
    {
        ipC = inet_ntoa(*(struct in_addr*)(hent->h_addr_list[i]));
    }
    std::string ip = std::string(ipC);
    // 2. 将节点信息写入配置文件
    std::string node = "node" + std::to_string(nodeIndex);
    std::ofstream outfile;
    outfile.open("test.conf", std::ios::app);
    if (!outfile.is_open())
    {
        std::cout << "打开文件失败!!" << std::endl;
        exit(EXIT_FAILURE);
    }
    outfile << node + "ip=" + ip << std::endl;
    outfile << node + "port=" + std::to_string(port) << std::endl;
    outfile.close();
    // 3. 创建服务器
    muduo::net::InetAddress address(ip, port);
    // 4. 给服务器设置连接回调函数
    _muduo_server->setConnectionCallback(std::bind(&RpcProvider::OnConnection, this, std::placeholders::_1));
    _muduo_server->setMessageCallback(std::bind(&RpcProvider::OnMessage, this, std::placeholders::_1,
                                        std::placeholders::_2, std::placeholders::_3));
    // 5. 设置服务器的线程数量
    _muduo_server->setThreadNum(4);
    std::cout << "RpcProvider start service at ip:" << ip << " port:" << port << std::endl;
    // 6. 启动网络服务
    _muduo_server->start();
    _eventLoop.loop();
}

RpcProvider::~RpcProvider()
{
    std::cout << "[func - RpcProvider::~RpcProvider()]: ip & port: " << _muduo_server->ipPort() << std::endl;
    _eventLoop.quit();
}
// 新连接建立成功时的回调函数
void RpcProvider::OnConnection(const muduo::net::TcpConnectionPtr& connectionPtr)
{
    // 这里的新连接什么都不干, 就等待有新的信息到来
    if (!connectionPtr->connected())
    {
        connectionPtr->shutdown();
    }
}
// 连接建立成功后, 读写事件的回调函数
// 这个服务器充当远程服务的发布方, 所以如果有信息到来, 一定是调用远程服务的请求
// 所以这个接口的工作是: 解析信息, 得到服务名和方法名以及参数, 然后调用方法进行业务处理
void RpcProvider::OnMessage(const muduo::net::TcpConnectionPtr& connectionPtr, muduo::net::Buffer* buffer, muduo::Timestamp time)
{
    // 实际的业务中, 调用者和被调用者双方要协商好进行信息交流时的数据的基本格式, 方便双方解析数据
    // 这里采用数据包头大小 + 数据报头内容(服务名称 + 方法名称) + 参数
    std::string recv_buf = buffer->retrieveAllAsString();
    // 1. 使用 protobuf 的 CodedInputStream 解析数据流
    google::protobuf::io::ArrayInputStream array_input(recv_buf.data(), recv_buf.size());
    google::protobuf::io::CodedInputStream coded_input(&array_input);
    uint32_t header_size{};
    // 2. 解析 header_size, 读取数据头的原始字符流
    coded_input.ReadVarint32(&header_size);
    std::string rpc_header_str;
    RPC::RpcHeader rpcHeader;
    std::string service_name;
    std::string method_name;
    // 读取数据时设置读取限制, 防止数据读多
    google::protobuf::io::CodedInputStream::Limit msg_limit = coded_input.PushLimit(header_size);
    coded_input.ReadString(&rpc_header_str, header_size);
    // 恢复之前的限制, 以便继续读取其他数据
    coded_input.PopLimit(msg_limit);
    uint32_t args_size{};
    // 3. 反序列化数据头
    if (rpcHeader.ParseFromString(rpc_header_str))
    {
        // 数据头反序列成功
        service_name = rpcHeader.service_name();
        method_name = rpcHeader.method_name();
        args_size = rpcHeader.args_size();
    }
    else
    {
        // 失败
        std::cout << "rpc_header_str: " << rpc_header_str << " paser error!!" << std::endl;
        return;
    }
    // 4. 获取 rpc 方法参数的字符流数据
    std::string args_str;
    bool read_args_success = coded_input.ReadString(&args_str, args_size);
    if (!read_args_success)
    {
        return;
    }
    // 5. 获取服务对象和方法对象
    auto it = _serviceMap.find(service_name);
    if (it == _serviceMap.end())
    {
        std::cout << "服务: " << service_name << " is not exist!!" << std::endl;
        std::cout << "当前已有的服务列表为: ";
        for (auto item : _serviceMap)
        {
            std::cout << item.first << " ";
        }
        std::cout << std::endl;
        return;
    }
    auto mit = it->second._methodMap.find(method_name);
    if (mit == it->second._methodMap.end())
    {
        std::cout << service_name << ": " << method_name << " is not exist!!" << std::endl;
        return;
    }
    google::protobuf::Service* service = it->second._service;
    const google::protobuf::MethodDescriptor* method = mit->second;
    // 6. 生成调用 rpc 方法的 request 以及响应 response 的参数
    google::protobuf::Message* request = service->GetRequestPrototype(method).New();
    if (!request->ParseFromString(args_str))
    {
        std::cout << "request parse error, content: " << args_str << std::endl;
        return;
    }
    google::protobuf::Message* response = service->GetResponsePrototype(method).New();
    // 7. 设置本地方法执行完成之后的回调, 主要负责数据的序列化和反向发送请求
    google::protobuf::Closure* done = google::protobuf::NewCallback<RpcProvider, const muduo::net::TcpConnectionPtr&, 
    google::protobuf::Message*>(this, &RpcProvider::SendRpcResponse, connectionPtr, response);
    // 8. 真正调用方法
    // 用户注册的 service 类继承自 .protoc 生成的 serviceRpc 类, 又继承自google::protobuf::Service
    // serviceRpc 中重写了 Service 类中的纯虚函数 CallMethod, 会根据参数调用具体注册的方法
    service->CallMethod(method, nullptr, request, response, done);
}
// Closure 的回调操作, 用于序列化 rpc 的响应信息并通过网络发送
void RpcProvider::SendRpcResponse(const muduo::net::TcpConnectionPtr& connectionPtr, google::protobuf::Message* response)
{
    std::string response_str;
    if (response->SerializeToString(&response_str))
    {
        connectionPtr->send(response_str);
    }
    else
    {
        std::cout << "serialize response_str error!!" << std::endl;
    }
}