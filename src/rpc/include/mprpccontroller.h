#pragma once
#include <google/protobuf/service.h>
#include <string>

class MprpcController : public google::protobuf::RpcController
{
public:
    MprpcController();
    void Reset();
    bool Failed() const;
    std::string ErrorText() const;
    void SetFailed(const std::string& reason);
private:
    bool _failed; // RPC 方法执行过程中的状态
    std::string _errText; // RPC 方法执行过程中的错误信息
};