#include "./include/raftRpcUtil.h"
#include "../rpc/include/mprpcchannel.h"
#include "../rpc/include/mprpccontroller.h"

RaftRpcUtil::RaftRpcUtil(std::string ip, short port)
{
    _stub = new raftRpcProtoc::raftRpc_Stub(new MprpcChannel(ip, port, true));
}

RaftRpcUtil::~RaftRpcUtil()
{
    delete _stub;
}
// 这几个接口用于本节点主动调用其他节点的方法, 暂时不支持其他节点调用自己
// todo: 要支持其他节点调用自己, 需要继承 protoc 的 service 类
bool RaftRpcUtil::AppendEntries(raftRpcProtoc::AppendEntriesArgs* args, raftRpcProtoc::AppendEntriesReply* response)
{
    MprpcController controller;
    _stub->AppendEntries(&controller, args, response, nullptr);
    return !controller.Failed();
}

bool RaftRpcUtil::InstallSnapshot(raftRpcProtoc::InstallSnapshotRequest* args, raftRpcProtoc::InstallSnapshotResponse* response)
{
    MprpcController controller;
    _stub->InstallSnapshot(&controller, args, response, nullptr);
    return !controller.Failed();
}

bool RaftRpcUtil::RequestVote(raftRpcProtoc::RequestVoteArgs* args, raftRpcProtoc::RequestVoteReply* response)
{
    MprpcController controller;
    _stub->RequestVote(&controller, args, response, nullptr);
    return !controller.Failed();
}