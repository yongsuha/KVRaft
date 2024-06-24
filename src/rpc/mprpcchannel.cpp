#include "./include/mprpcchannel.h"

MprpcChannel::MprpcChannel(std::string ip, short port, bool connectNow)
    :_ip(ip), _port(port), _clientFd(-1)
{
    // rpc 调用方想要调用 service_name 的 method_name 服务, 需要查询 zk 上该服务所在的 host 信息
    if (!connectNow)
    {
        return;
    }
    std::string errMessage;
    auto it = NewConnect(ip.c_str(), port, &errMessage);
    int tryCount = 3;
    while (!it && tryCount--)
    {
        std::cout << errMessage << std::endl;
        it = NewConnect(ip.c_str(), port, &errMessage);
    }
}
// 对于代理对象调用的 rpc 方法, 进行数据的序列化和网络发送
void MprpcChannel::CallMethod(const google::protobuf::MethodDescriptor* method, google::protobuf::RpcController* controller,
const google::protobuf::Message* request, google::protobuf::Message* response, google::protobuf::Closure* done)
{
    // 1. 检查连接
    if (_clientFd == -1)
    {
        std::string errMessage;
        bool ret = NewConnect(_ip.c_str(), _port, &errMessage);
        if (!ret)
        {
            DPrintf("[func-MprpcChannel::CallMethod]重连接ip: {%s} port: {%d}失败", _ip.c_str(), _port);
            controller->SetFailed(errMessage);
            return;
        }
        else
        {
            DPrintf("[func-MprpcChannel::CallMethod]重连接ip: {%s} port: {%d}成功", _ip.c_str(), _port);
        }
    }
    // 2. 获取服务对象名称和服务方法名称
    const google::protobuf::ServiceDescriptor* serviceDes = method->service();
    std::string service_name = serviceDes->name();
    std::string method_name = method->name();
    // 3. 获取调用方法所需的参数的序列化字符串长度
    uint32_t args_size{};
    std::string args_str;
    if (request->SerializeToString(&args_str))
    {
        args_size = args_str.size();
    }
    else
    {
        controller->SetFailed("serialize request error!!");
        return;
    }
    // 4. 设置数据头信息
    RPC::RpcHeader rpcHeader;
    rpcHeader.set_service_name(service_name);
    rpcHeader.set_method_name(method_name);
    rpcHeader.set_args_size(args_size);
    // 5. 将数据头信息序列化
    std::string rpc_header_str;
    if (!rpcHeader.SerializeToString(&rpc_header_str))
    {
        controller->SetFailed("serialize rpc header error!!");
        return;
    }
    // 6. 构建发送数据流
    std::string send_rpc_str;
    {
        google::protobuf::io::StringOutputStream string_output(&send_rpc_str);
        google::protobuf::io::CodedOutputStream coded_output(&string_output);
        coded_output.WriteVarint32(static_cast<uint32_t>(rpc_header_str.size()));
        coded_output.WriteString(rpc_header_str);
    }
    send_rpc_str += args_str;
    // 7. 发送 rpc 请求, 失败重连, 重连失败直接返回
    while (send(_clientFd, send_rpc_str.c_str(), send_rpc_str.size(), 0) == -1)
    {
        char errTxt[512] = {0};
        sprintf(errTxt, "send error!! errno: %d", errno);
        std::cout << "尝试重连, 对方ip: " << _ip << " 对方port: " <<_port << std::endl;
        close(_clientFd);
        _clientFd = -1;
        std::string errMessage;
        bool ret = NewConnect(_ip.c_str(), _port, &errMessage);
        if (!ret)
        {
            controller->SetFailed(errMessage);
            return;
        }
    }
    // 将请求发给 rpc 服务的提供者后就会开始处理, 如果返回就代表返回了处理结果
    // 8. 接收 rpc 请求的响应结果
    char recv_buf[1024] = {0};
    int recv_size = 0;
    if ((recv(_clientFd, recv_buf, 1024, 0) == recv_size) == -1)
    {
        close(_clientFd);
        _clientFd = -1;
        char errTxt[512] = {0};
        sprintf(errTxt, "recv error!! errno: %d", errno);
        controller->SetFailed(errTxt);
        return;
    }
    // 9. 将相应的处理结果反序列化
    if (!response->ParseFromArray(recv_buf, recv_size))
    {
        char errTxt[1050] = {0};
        sprintf(errTxt, "parse error!! response_str: %s", recv_buf);
        controller->SetFailed(errTxt);
        return;
    }
}

// 获取新连接
bool MprpcChannel::NewConnect(const char* ip, uint16_t port, std::string* errMessage)
{
    int clientFd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientFd == -1)
    {
        char errTxt[512] = {0};
        sprintf(errTxt, "create socket error!! errno: %d", errno);
        clientFd = -1;
        *errMessage = errTxt;
        return false;
    }
    struct sockaddr_in server_addr;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    // 连接 rpc 服务节点
    if (connect(clientFd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1)
    {
        close(clientFd);
        char errTxt[512] = {0};
        sprintf(errTxt, "connect fail!! errno: %d", errno);
        _clientFd = -1;
        *errMessage = errTxt;
        return false;
    }
    _clientFd = clientFd;
    return true;
}