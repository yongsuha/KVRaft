#include "./include/raft.h"

void Raft::AppendEntries1(const raftRpcProtoc::AppendEntriesArgs* args, raftRpcProtoc::AppendEntriesReply* reply)
{
    std::lock_guard<std::mutex> lock(_mutex);
    reply->set_appstate(AppNormal); // 能接收到消息证明网络是正常的
    // 分情况讨论当前节点任期号和 leader 的任期号的大小
    // 1. leader 的任期号小于当前节点的任期号, 则返回 false 和当前节点任期号, 以让 leader 更新自己的任期号
    if (args->term() < _currentTerm)
    {
        reply->set_success(false);
        reply->set_term(_currentTerm);
        reply->set_updatenextindex(-100); // 设置得足够小以便 leader 有足够的时间更新自己
        DPrintf("[func-AppendEntries-rf{%d}] refused because the term{%d} of leader{%d} less than the term{%d} of rf{%d}\n", _self, args->term(), args->leaderid(), _currentTerm, _self);
        // 收到过期的 leader 的消息不需要重设定时器
        return;
    }
    // 由于该局部变量在创建锁之后, 所以执行 persist 的时候也是拿到锁的
    DEFER { persist(); };
    // 2. leader 的任期号大于当前节点的任期号, 则更新当前节点任期号和 leader 相同
    if (args->term() > _currentTerm)
    {
        DPrintf("[func-AppendEntries-rf{%d}] turn into follower and renew it's term because the term of leader{%v} is bigger\n", _self, args->leaderid());
        _status = Follower;
        _currentTerm = args->term();
        // 将投票状态改为 -1, 如果当前节点突然宕机再上线之后是可以参与投票的
        _votedFor = -1;
        // 这里不用返回, 当 leader 的身份没问题的情况下, 其他节点应该等待接收来自 leader 的日志
    }
    // 如果 leader 的任期号过期会在前面返回, 走到这里代表当前结点任期号已经更新为 leader 的任期号
    myAssert(args->term() == _currentTerm, format ("assert {args.term == rf.currentTerm} failed"));
    // 如果是 candidate 收到来自同一个任期号的 leader 的消息, 需要变成 follower
    _status = Follower;
    _lastResetElectionTime = now(); // 收到 leader 的消息需要重设定时器
    DPrintf("[func-AppendEntries-rf{%d}] reset election outtime timer", _self);
    // 因为各种可能导致延迟的问题, 不能直接从 leader 日志的前一个开始
    // 所以接下来需要比较 leader 和当前结点的日志信息差多少
    // 1. leader 的日志条目多于当前节点, 表明两者日志信息不匹配, 返回 false
    if (args->prevlogindex() > getLastLogIndex())
    {
        reply->set_success(false);
        reply->set_term(_currentTerm);
        reply->set_updatenextindex(getLastLogIndex() + 1);
        DPrintf("[func-AppendEntries-rf{%d}] refused leader{%v} because the log is too new", _self);
        return;
    }
    // 接下来检查日志一致性, 找到两个节点一致的条目中最新的那个
    else if (args->prevlogindex() < _lastSnapshotIncludeIndex)
    {
        // leader 上前一个日志条目索引小于当前节点快照上的最后一个, 表明 leader 上的索引太老了
        reply->set_success(false);
        reply->set_term(_currentTerm);
        reply->set_updatenextindex(_lastSnapshotIncludeIndex + 1);
        DPrintf("[func-AppendEntries-rf{%d}] refused leader because the log of leader is too old", _self);
    }
    // 从一致的日志中最新的一条后面开始截断, 删除 follower 中所有冲突的条目
    if (matchLog(args->prevlogindex(), args->prevlogterm()))
    {
        // todo: 整理 logs
        // todo: 这里一个个检查, 可是论文中是截断
        for (int i = 0; i < args->entries_size(); i++)
        {
            auto log = args->entries(i);
            // 如果该日志的索引大于当前 follower 的索引直接则直接加在后面
            if (log.logindex() > getLastLogIndex())
            {
                _logs.push_back(log);
            }
            // 没超过就看是否匹配, 不匹配就更新, 还是没有截断
            else
            {
                // todo: 这里改为判断对应 logIndex 位置的 term 是否相等会更好
                // 但是当前的代码却保证不了 index 和 term 相等的情况下, 日志也相同, 需要排查
                if (_logs[getSliceIndexFromLogIndex(log.logindex())].logterm() == log.logterm() &&
                    _logs[getSliceIndexFromLogIndex(log.logindex())].command() != log.command())
                {
                    // 相同 index 和 term 的日志不同, 不符合 raft 的向前匹配, 有问题
                    myAssert(false, format("[func-AppendEntries-rf{%d}] index and term are same, but the command{%d:%d} and {%d:%d} are different",
                             _self, _self, _logs[getSliceIndexFromLogIndex(log.logindex())].command(), args->leaderid(), log.command()));   
                }
                if (_logs[getSliceIndexFromLogIndex(log.logindex())].logterm() != log.logterm())
                {
                    // 更新
                    _logs[getSliceIndexFromLogIndex(log.logindex())] = log;
                }
            }
        }
        // 按理来说, 走到这里就证明日志已经保证一致性了
        myAssert(getLastLogIndex() >= args->prevlogindex() + args->entries_size(),
                 format("[func-AppendEntries-rf{%d}] LastLogIndex is not same as PrevLogIndex + Entries.size", _self));
        // 判断 commitIndex
        if (args->leadercommit() > _commitIndex)
        {
            _commitIndex = std::min(args->leadercommit(), getLastLogIndex());
        }
        myAssert(getLastLogIndex() >= _commitIndex, format("[func-AppendEntries-rf{%d}] LastLogIndex < commitIndex", _self));
        reply->set_success(true);
        reply->set_term(_currentTerm);
        return;
    }
    else
    {
        reply->set_updatenextindex(args->prevlogindex());
        for (int index = args->prevlogindex(); index >= _lastSnapshotIncludeIndex; --index)
        {
            if (getLastTermFromLogIndex(index) != getLastTermFromLogIndex(args->prevlogindex()))
            {
                reply->set_updatenextindex(index + 1);
                break;
            }
        }
        reply->set_success(false);
        reply->set_term(_currentTerm);
        return;
    }
}

void Raft::applierTicker()
{

}

bool Raft::CondInstallSnapshot(int lastIncludeTerm, int lastIncludeIndex, std::string snapshot)
{

}

void Raft::doElection()
{

}
// 发起心跳(仅限 leader)
void Raft::doHeartBeat()
{

}
// 轮询检查睡眠时间内有没有重置定时器, 没有则超时, 有则设置合适的睡眠时间: 重置时间 + 超时时间
void Raft::electionTimeOutTicker()
{

}

std::vector<ApplyMsg> Raft::getApplyLogs()
{

}

int Raft::getNewCommandIndex()
{

}

void Raft::getPrevLogInfo(int server, int* preIndex, int* preTerm)
{

}

void Raft::GetState(int* term, bool* isLeader)
{

}

void Raft::InstallSnapshot(const raftRpcProtoc::InstallSnapshotRequest* args, raftRpcProtoc::InstallSnapshotResponse* reply)
{

}

void Raft::leaderHearBeatTicker()
{

}

void Raft::leaderSendSnapshot(int server)
{

}

void Raft::leaderUpdateCommitIndex()
{

}

bool Raft::matchLog(int logIndex, int logTerm)
{

}

void Raft::persist()
{

}

void Raft::RequestVote(const raftRpcProtoc::RequestVoteArgs* args, raftRpcProtoc::RequestVoteReply* reply)
{

}

bool Raft::UpToDate(int index, int term)
{

}

int Raft::getLastLogIndex()
{

}

int Raft::getLastLogTerm()
{

}

void Raft::getLastLogIndexAndTerm(int* lastLogIndex, int* lastLogTerm)
{

}

int Raft::getLastTermFromLogIndex(int logIndex)
{

}

int Raft::GetRaftStateSize()
{

}

int Raft::getSliceIndexFromLogIndex(int logIndex)
{

}

bool Raft::sendRequestVote(int server, std::shared_ptr<raftRpcProtoc::RequestVoteArgs> args,
                        std::shared_ptr<raftRpcProtoc::RequestVoteReply> reply, std::shared_ptr<int> votedNum)
{
    
}

bool Raft::sendAppendEntries(int server, std::shared_ptr<raftRpcProtoc::AppendEntriesArgs> args,
                        std::shared_ptr<raftRpcProtoc::AppendEntriesReply> reply, std::shared_ptr<int> appendNums)
{
    
}

void Raft::pushMsgToKvServer(ApplyMsg msg)
{

}

void Raft::readPersist(std::string data)
{

}

std::string Raft::persistData()
{

}

void Raft::Start(Op command, int* newLogIndex, int* newLogTerm, bool* isLeader)
{

}
// 这个接口为了抛弃已经安装到快照里的日志, 并重新安装上层 service 传来的快照字节流
// 属于 peers 自身主动更新, 与 leader 发送快照并不冲突
// 即服务层主动发起请求保存快照数据, index 用于表示 snapshot 快照执行到了哪条命令
void Raft::Snapshot(int index, std::string snapshot)
{

}

void Raft::init(std::vector<std::shared_ptr<RaftRpcUtil>> peers, int self, 
            std::shared_ptr<Persister> persister, std::shared_ptr<LockQueue<ApplyMsg>> applyQueue)
{
    
}
// 下面三个接口是重写基类的方法, 也是 rpc 远程真正调用的方法, 主要是获取值以及执行本地方法
void Raft::AppendEntries(google::protobuf::RpcController* controller, const ::raftRpcProtoc::AppendEntriesArgs* request,
                    ::raftRpcProtoc::AppendEntriesReply* response, ::google::protobuf::Closure* done)
{
    
}
void Raft::InstallSnapshot(google::protobuf::RpcController* controller, const ::raftRpcProtoc::InstallSnapshotRequest* request,
                        ::raftRpcProtoc::InstallSnapshotResponse* response, ::google::protobuf::Closure* done)
{
    
}
void Raft::RequestVote(google::protobuf::RpcController* controller, const ::raftRpcProtoc::RequestVoteArgs* request,
                    ::raftRpcProtoc::RequestVoteReply* response, ::google::protobuf::Closure* done)
{
    
}