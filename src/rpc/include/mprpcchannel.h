#pragma once

#include <map>
#include <cerrno>
#include <random>
#include <string>
#include <vector>
#include <unistd.h>
#include <iostream>
#include <algorithm>
#include <algorithm>
#include <functional>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unordered_map>
#include <google/protobuf/message.h>
#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>
#include "util.h"
#include "rpcheader.pb.h"
#include "mprpccontroller.h"

// 负责消息发送和接收前后的处理工作
class MprpcChannel : public google::protobuf::RpcChannel
{
public:
    MprpcChannel(std::string ip, short port, bool connectNow);
    // 对于代理对象调用的 rpc 方法, 进行数据的序列化和网络发送
    void CallMethod(const google::protobuf::MethodDescriptor* method, google::protobuf::RpcController* controller,
                    const google::protobuf::Message* request, google::protobuf::Message* response, google::protobuf::Closure* done) override;
private:
    bool NewConnect(const char* ip, uint16_t port, std::string* errMessage);
private:
    int _clientFd;
    const std::string _ip;
    const uint16_t _port;
};