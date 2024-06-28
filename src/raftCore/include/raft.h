#pragma once

#include <cmath>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <memory>
#include <chrono>
#include <iostream>
#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include "ApplyMsg.h"
#include "Persister.h"
#include "raftRpcUtil.h"
#include "boost/any.hpp"
#include "../../common/include/util.h"
#include "../../common/include/config.h"
#include "boost/serialization/serialization.hpp"

// 网络状态标识
constexpr int DisConnected = 0;
constexpr int AppNormal = 1;
// 投票状态
constexpr int Killed = 0;
constexpr int Voted = 1; // 已经投过票了
constexpr int Expire = 2; // 投票过期
constexpr int Normal = 3;

enum Status { Follower, Candidate, Leader};

class Raft : public raftRpcProtoc::raftRpc
{
public:
    void AppendEntries1(const raftRpcProtoc::AppendEntriesArgs* args, raftRpcProtoc::AppendEntriesReply* reply);
    void applierTicker();
    bool CondInstallSnapshot(int lastIncludeTerm, int lastIncludeIndex, std::string snapshot);
    void doElection();
    // 发起心跳(仅限 leader)
    void doHeartBeat();
    // 轮询检查睡眠时间内有没有重置定时器, 没有则超时, 有则设置合适的睡眠时间: 重置时间 + 超时时间
    void electionTimeOutTicker();
    std::vector<ApplyMsg> getApplyLogs();
    int getNewCommandIndex();
    void getPrevLogInfo(int server, int* preIndex, int* preTerm);
    void GetState(int* term, bool* isLeader);
    void InstallSnapshot(const raftRpcProtoc::InstallSnapshotRequest* args, raftRpcProtoc::InstallSnapshotResponse* reply);
    void leaderHearBeatTicker();
    void leaderSendSnapshot(int server);
    void leaderUpdateCommitIndex();
    bool matchLog(int logIndex, int logTerm);
    void persist();
    void RequestVote(const raftRpcProtoc::RequestVoteArgs* args, raftRpcProtoc::RequestVoteReply* reply);
    bool UpToDate(int index, int term);
    int getLastLogIndex();
    int getLastLogTerm();
    void getLastLogIndexAndTerm(int* lastLogIndex, int* lastLogTerm);
    int getLogTermFromLogIndex(int logIndex);
    int GetRaftStateSize();
    int getSliceIndexFromLogIndex(int logIndex);
    bool sendRequestVote(int server, std::shared_ptr<raftRpcProtoc::RequestVoteArgs> args,
                         std::shared_ptr<raftRpcProtoc::RequestVoteReply> reply, std::shared_ptr<int> votedNum);
    bool sendAppendEntries(int server, std::shared_ptr<raftRpcProtoc::AppendEntriesArgs> args,
                           std::shared_ptr<raftRpcProtoc::AppendEntriesReply> reply, std::shared_ptr<int> appendNums);
    void pushMsgToKvServer(ApplyMsg msg);
    void readPersist(std::string data);
    std::string persistData();
    void Start(Op command, int* newLogIndex, int* newLogTerm, bool* isLeader);
    // 这个接口为了抛弃已经安装到快照里的日志, 并重新安装上层 service 传来的快照字节流
    // 属于 peers 自身主动更新, 与 leader 发送快照并不冲突
    // 即服务层主动发起请求保存快照数据, index 用于表示 snapshot 快照执行到了哪条命令
    void Snapshot(int index, std::string snapshot);
    void init(std::vector<std::shared_ptr<RaftRpcUtil>> peers, int self, 
              std::shared_ptr<Persister> persister, std::shared_ptr<LockQueue<ApplyMsg>> applyQueue);
    // 下面三个接口是重写基类的方法, 也是 rpc 远程真正调用的方法, 主要是获取值以及执行本地方法
    void AppendEntries(google::protobuf::RpcController* controller, const ::raftRpcProtoc::AppendEntriesArgs* request,
                       ::raftRpcProtoc::AppendEntriesReply* response, ::google::protobuf::Closure* done) override;
    void InstallSnapshot(google::protobuf::RpcController* controller, const ::raftRpcProtoc::InstallSnapshotRequest* request,
                         ::raftRpcProtoc::InstallSnapshotResponse* response, ::google::protobuf::Closure* done) override;
    void RequestVote(google::protobuf::RpcController* controller, const ::raftRpcProtoc::RequestVoteArgs* request,
                     ::raftRpcProtoc::RequestVoteReply* response, ::google::protobuf::Closure* done) override;
private:
    std::mutex _mutex;
    std::vector<std::shared_ptr<RaftRpcUtil>> _peers;
    std::shared_ptr<Persister> _persister;
    int _self;
    int _currentTerm;
    int _votedFor;
    // 日志条目数组, 包含了状态机要执行的指令集, 以及收到 leader 时的任期号
    std::vector<raftRpcProtoc::LogEntry> _logs;
    int _commitIndex;
    int _lastApplied; // 这两个状态易失
    // 这两个状态易失, 由服务器维护, 下标从 1 开始
    std::vector<int> _nextIndex;
    std::vector<int> _matchIndex;
    Status _status;
    // 日志存放的队列, 从这里取出日志用于 client 和 raft 的通信
    std::shared_ptr<LockQueue<ApplyMsg>> _applyQueue;
    // 选举超时时间和心跳时间
    std::chrono::_V2::system_clock::time_point _lastResetElectionTime;
    std::chrono::_V2::system_clock::time_point _lastResetHearBeatTime;
    // 快照中最后一个日志的索引和任期号
    int _lastSnapshotIncludeIndex;
    int _lastSnapshotIncludeTerm;
private:
    class BoostPersistRaftNode
    {
    public:
        friend class boost::serialization::access;
        template <class Archive>
        void serialize(Archive& ar, const unsigned int version)
        {
            ar& _currentTerm;
            ar& _votedFor;
            ar& _lastSnapshotIncludeIndex;
            ar& _lastSnapshotIncludeTerm;
            ar& _logs;
        }
    public:
        int _currentTerm;
        int _votedFor;
        int _lastSnapshotIncludeIndex;
        int _lastSnapshotIncludeTerm;
        std::vector<std::string> _logs;
        std::unordered_map<std::string, int> _map;
    };
};