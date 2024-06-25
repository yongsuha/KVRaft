#pragma once
#include "./include/raftRPC.pb.h"

// 该类主要是对某一个节点与其他节点之间 rpc 通信的接口进行封装, 需要维护与其他节点的连接
class RaftRpcUtil
{
public:
    RaftRpcUtil(std::string ip, short port);
    ~RaftRpcUtil();
    // 这几个接口用于本节点主动调用其他节点的方法, 暂时不支持其他节点调用自己
    // todo: 要支持其他节点调用自己, 需要继承 protoc 的 service 类
    bool AppendEntries(raftRpcProtoc::AppendEntriesArgs* args, raftRpcProtoc::AppendEntriesReply* response);
    bool InstallSnapshot(raftRpcProtoc::InstallSnapshotRequest* args, raftRpcProtoc::InstallSnapshotResponse* response);
    bool RequestVote(raftRpcProtoc::RequestVoteArgs* args, raftRpcProtoc::RequestVoteReply* response);
private:
    raftRpcProtoc::raftRpc_Stub* _stub;
};