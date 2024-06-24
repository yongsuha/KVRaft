#pragma once
#include <string>
#include <cstring>
#include <fstream>
#include <netdb.h>
#include <unistd.h>
#include <functional>
#include <arpa/inet.h>
#include <unordered_map>
#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpServer.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/TcpConnection.h>
#include <google/protobuf/descriptor.h>
#include "util.h"
#include "rpcheader.pb.h"
#include "google/protobuf/service.h"

// 该类用于提供一个可以发布 rpc 方法的框架, 搭配 muduo 网络库使用
// todo: rpc 客户端变成了长连接, 所以服务器最好能提供一个定时器
class RpcProvider
{
    // service 服务类型信息
    struct ServiceInfo
    {
        google::protobuf::Service* _service;
        std::unordered_map<std::string, const google::protobuf::MethodDescriptor*> _methodMap;
    };
public:
    // 框架提供给外部的发布 rpc 方法的函数接口
    void NotifyService(google::protobuf::Service* service);
    // 启动 rpc 服务节点, 开始提供 rpc 远程网络调用服务
    void Run(int nodeIndex, short port);
    ~RpcProvider();
private:
    // 存储注册成功的服务对象和其服务方法信息
    std::unordered_map<std::string, ServiceInfo> _serviceMap;
    // 新连接建立成功时的回调函数
    void OnConnection(const muduo::net::TcpConnectionPtr& connectionPtr);
    // 连接建立成功后, 读写事件的回调函数
    void OnMessage(const muduo::net::TcpConnectionPtr& connectionPtr, muduo::net::Buffer* buffer, muduo::Timestamp time);
    // Closure 的回调操作, 用于序列化 rpc 的响应信息并通过网络发送
    void SendRpcResponse(const muduo::net::TcpConnectionPtr& connectionPtr, google::protobuf::Message* response);
private:
    muduo::net::EventLoop _eventLoop;
    std::shared_ptr<muduo::net::TcpServer> _muduo_server;
};