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
        DPrintf("[func-AppendEntries-raft{%d}] refused because the term{%d} of leader{%d} less than the term{%d} of rf{%d}\n", _self, args->term(), args->leaderid(), _currentTerm, _self);
        // 收到过期的 leader 的消息不需要重设定时器
        return;
    }
    // 由于该局部变量在创建锁之后, 所以执行 persist 的时候也是拿到锁的
    DEFER { persist(); };
    // 2. leader 的任期号大于当前节点的任期号, 则更新当前节点任期号和 leader 相同
    if (args->term() > _currentTerm)
    {
        DPrintf("[func-AppendEntries-raft{%d}] turn into follower and renew it's term because the term of leader{%v} is bigger\n", _self, args->leaderid());
        _status = Follower;
        _currentTerm = args->term();
        // 将投票状态改为 -1, 如果当前节点突然宕机再上线之后是可以参与投票的
        _votedFor = -1;
        // 这里不用返回, 当 leader 的身份没问题的情况下, 其他节点应该等待接收来自 leader 的日志
    }
    // 如果 leader 的任期号过期会在前面返回, 走到这里代表当前结点任期号已经更新为 leader 的任期号
    myAssert(args->term() == _currentTerm, format ("assert {args.term == raft.currentTerm} failed"));
    // 如果是 candidate 收到来自同一个任期号的 leader 的消息, 需要变成 follower
    _status = Follower;
    _lastResetElectionTime = now(); // 收到 leader 的消息需要重设定时器
    DPrintf("[func-AppendEntries-raft{%d}] reset election outtime timer", _self);
    // 因为各种可能导致延迟的问题, 不能直接从 leader 日志的前一个开始
    // 所以接下来需要比较 leader 和当前结点的日志信息差多少
    // 1. leader 的日志条目多于当前节点, 表明两者日志信息不匹配, 返回 false
    if (args->prevlogindex() > getLastLogIndex())
    {
        reply->set_success(false);
        reply->set_term(_currentTerm);
        reply->set_updatenextindex(getLastLogIndex() + 1);
        DPrintf("[func-AppendEntries-raft{%d}] refused leader{%v} because the log is too new", _self);
        return;
    }
    // 接下来检查日志一致性, 找到两个节点一致的条目中最新的那个
    else if (args->prevlogindex() < _lastSnapshotIncludeIndex)
    {
        // leader 上前一个日志条目索引小于当前节点快照上的最后一个, 表明 leader 上的索引太老了
        reply->set_success(false);
        reply->set_term(_currentTerm);
        reply->set_updatenextindex(_lastSnapshotIncludeIndex + 1);
        DPrintf("[func-AppendEntries-raft{%d}] refused leader because the log of leader is too old", _self);
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
                    myAssert(false, format("[func-AppendEntries-raft{%d}] index and term are same, but the command{%d:%d} and {%d:%d} are different",
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
                 format("[func-AppendEntries-raft{%d}] LastLogIndex is not same as PrevLogIndex + Entries.size", _self));
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
            if (getLogTermFromLogIndex(index) != getLogTermFromLogIndex(args->prevlogindex()))
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
    while (true)
    {
        // todo: 这里的锁是否应该从匿名函数改为可以在管道中传递的锁
        _mutex.lock();
        if (_status == Leader)
        {
            DPrintf("[Raft::applierTicker() - raft{%d}] _lastApplied{%d} _commitIndex{%d}", _self, _lastApplied, _commitIndex);
        }
        auto applyMsgs = getApplyLogs();
        _mutex.unlock();
        if (!applyMsgs.empty())
        {
            DPrintf("[func-Raft::applierTicker()-raft{%d}] the length from kvserver to appliMsgs: {%d}", _self, applyMsgs.size());
        }
        for (auto& message : applyMsgs)
        {
            _applyQueue->Push(message);
        }
        sleepNMilliseconds(ApplyInterval);
    }
}

bool Raft::CondInstallSnapshot(int lastIncludeTerm, int lastIncludeIndex, std::string snapshot)
{
    // todo
    return true;
}
// 当前节点不是 leader 且选举定时器超时时开始选举
void Raft::doElection()
{
    if (_status != Leader)
    {
        DPrintf("[ticker-func-raft(%d)] election timer is over and it is not leader, start election\n", _self);
        // 身份由 follower 变为 candidate, 任期号 + 1, 并把票投给自己
        _status = Candidate;
        _currentTerm += 1;
        _votedFor = _self;
        persist();
        std::shared_ptr<int> voteNum = std::make_shared<int>(1);
        // 重新设置定时器
        _lastResetElectionTime = now();
        // 向其他节点发送 RequestVote PRC
        for (int i = 0; i < _peers.size(); i++)
        {
            if (i == _self)
            {
                continue;
            }
            // 先拿到当前节点自己的最后一个日志的索引和任期号
            int lastLogIndex = -1, lastLogTerm = -1;
            getLastLogIndexAndTerm(&lastLogIndex, &lastLogTerm);
            // 设置 RPC 参数
            std::shared_ptr<raftRpcProtoc::RequestVoteArgs> requestVoteArgs = std::make_shared<raftRpcProtoc::RequestVoteArgs>();
            requestVoteArgs->set_candidateid(_self);
            requestVoteArgs->set_lastlogindex(lastLogIndex);
            requestVoteArgs->set_lastlogterm(lastLogTerm);
            requestVoteArgs->set_term(_currentTerm);
            auto requestVoteReply = std::make_shared<raftRpcProtoc::RequestVoteReply>();
            // 使用匿名函数执行发送和接收 RPC 数据, 避免其拿到锁
            std::thread t(&Raft::sendRequestVote, this, i, requestVoteArgs, requestVoteReply, voteNum);
            t.detach();
        }
    }
}
// 发起心跳(仅限 leader)
void Raft::doHeartBeat()
{
    // todo: 因为后续涉及到 log 优化, 所以后面应该把管理 log 的工作单独封装
    std::lock_guard<std::mutex> lock(_mutex);
    if (_status == Leader)
    {
        DPrintf("[func-Raft::doHeartBeat()-leader: {%d}] start sending AE\n", _self);
        // 正确返回的节点的数目
        auto appendNums = std::make_shared<int>(1);
        // 发送 AE 给其他节点
        for (int i = 0; i < _peers.size(); i++)
        {
            if (i == _self)
            {
                continue;
            }
            DPrintf("[func-Raft::doHeartBeat()-leader: {%d}] heart timer is over, index: {%d}", _self, i);
            myAssert(_nextIndex[i] >= 1, format("raft.nextIndex[%d] = {%d}", i, _nextIndex[i]));
            // todo: 后续加入日志压缩后要判断是发送 AE 还是快照
            if (_nextIndex[i] <= _lastSnapshotIncludeIndex)
            {
                std::thread t(&Raft::leaderSendSnapshot, this, i);
                t.detach();
                continue;
            }
            // 设置 AE RPC 的参数
            int prevLogIndex = -1, prevLogTerm = -1;
            getPrevLogInfo(i, &prevLogIndex, &prevLogTerm);
            std::shared_ptr<raftRpcProtoc::AppendEntriesArgs>appendEntriesArgs = std::make_shared<raftRpcProtoc::AppendEntriesArgs>();
            appendEntriesArgs->set_leadercommit(_commitIndex);
            appendEntriesArgs->set_leaderid(_self);
            appendEntriesArgs->set_prevlogindex(prevLogIndex);
            appendEntriesArgs->set_prevlogterm(prevLogTerm);
            appendEntriesArgs->set_term(_currentTerm);
            appendEntriesArgs->clear_entries();
            if (prevLogIndex != _lastSnapshotIncludeIndex)
            {
                for (int j = getSliceIndexFromLogIndex(prevLogIndex) + 1; j < _logs.size(); ++j)
                {
                    raftRpcProtoc::LogEntry* sendEntryPtr = appendEntriesArgs->add_entries();
                    *sendEntryPtr = _logs[j];
                }
            }
            else
            {
                for (const auto& it : _logs)
                {
                    raftRpcProtoc::LogEntry* sendEntryPtr = appendEntriesArgs->add_entries();
                    *sendEntryPtr = it;
                }
            }
            // 虽然 leader 对每个节点发送的日志数量可能不同, 但都保证了从 prevIndex 直到最后
            int lastLogIndex = getLastLogIndex();
            myAssert(appendEntriesArgs->prevlogindex() + appendEntriesArgs->entries_size() == lastLogIndex, format(
                     "appendEntriesArgs.prevLogIndex + len(appednEntriesArgs.Entries) != lastLogIndex"));
            // 构造返回值并创建线程发送 AE
            const std::shared_ptr<raftRpcProtoc::AppendEntriesReply>appendEntriesReply = std::make_shared<raftRpcProtoc::AppendEntriesReply>();
            appendEntriesReply->set_appstate(DisConnected);
            std::thread t(&Raft::sendAppendEntries, this, i, appendEntriesArgs, appendEntriesReply, appendNums);
            t.detach();
        }
        _lastResetHearBeatTime = now();
    }
}
// 轮询检查睡眠时间内有没有重置定时器, 没有则超时, 有则应该开始选举, 设置合适的睡眠时间: 重置时间 + 超时时间
void Raft::electionTimeOutTicker()
{
    while (true)
    {
        while (_status == Leader)
        {
            usleep(HeartBeatTimeout); // todo: 时间可能需要更严谨一些
        }
        std::chrono::duration<signed long int, std::ratio<1, 1000000000>>suitableSleepTime{};
        std::chrono::system_clock::time_point wakeTime{};
        {
            _mutex.lock();
            wakeTime = now();
            suitableSleepTime = getRandomizedElectionTimeout() + _lastResetElectionTime - wakeTime;
            _mutex.unlock();
        }
        if (std::chrono::duration<double, std::milli>(suitableSleepTime).count() > 1)
        {
            // 获取当前时间点
            auto start = std::chrono::steady_clock::now();
            usleep(std::chrono::duration_cast<std::chrono::microseconds>(suitableSleepTime).count());
            // 获取函数运行结束后的时间点
            auto end = std::chrono::steady_clock::now();
            // 计算时间差并输出结果
            std::chrono::duration<double, std::milli> duration = end - start;
            // 改颜色比较好看到
            std::cout << "\033[1;35m electionTimeOutTicker();函数设置睡眠时间为: "
                << std::chrono::duration_cast<std::chrono::milliseconds>(suitableSleepTime).count() << " 毫秒\033[0m"
                << std::endl;
            std::cout << "\033[1;35m electionTimeOutTicker();函数实际睡眠时间为: " << duration.count() << " 毫秒\033[0m"
                << std::endl;
        }
        if (std::chrono::duration<double, std::milli>(_lastResetElectionTime - wakeTime).count() > 0)
        {
            // 说明睡眠的时间里有重置定时器, 也就没有超时, 再次睡眠
            continue;
        }
        doElection();
    }
}

std::vector<ApplyMsg> Raft::getApplyLogs()
{
    std::vector<ApplyMsg> applyMsgs;
    myAssert(_commitIndex <= getLastLogIndex(), format("[func-getApplyLogs-raft] commitIndex > getLastLogIndex"));
    while (_lastApplied < _commitIndex)
    {
        _lastApplied++;
        myAssert(_logs[getSliceIndexFromLogIndex(_lastApplied)].logindex() == _lastApplied, format(
                 "raft.logs[raft.getSlicesIndexFromLogIndex(raft.lastApplied)].LogIndex != raft.lastApplied"));
        ApplyMsg applyMsg;
        applyMsg._commandValid = true;
        applyMsg._snapshotValid = false;
        applyMsg._command = _logs[getSliceIndexFromLogIndex(_lastApplied)].command();
        applyMsg._commandIndex = _lastApplied;
        applyMsgs.emplace_back(applyMsg);
    }
    return applyMsgs;
}
// 获取新指令应该分配的 index
int Raft::getNewCommandIndex()
{
    auto lastLongIndex = getLastLogIndex();
    return lastLongIndex + 1;
}
// 该接口由 leader 调用, 根据传入的节点 index 传出发送 AE 的 prevLogIndex 和 prevLogTerm
void Raft::getPrevLogInfo(int server, int* preIndex, int* preTerm)
{
    if (_nextIndex[server] == _lastSnapshotIncludeIndex + 1)
    {
        // 要发送的日志是第一个日志, 直接返回 _lastSnapshotIncludeIndex 和 _lastSnapshotIncludeTerm
        *preIndex = _lastSnapshotIncludeIndex;
        *preTerm = _lastSnapshotIncludeTerm;
        return;
    }
    auto nextIndex = _nextIndex[server];
    *preIndex = nextIndex + 1;
    *preTerm = _logs[getSliceIndexFromLogIndex(*preIndex)].logterm();
}

void Raft::GetState(int* term, bool* isLeader)
{
    _mutex.lock();
    DEFER { _mutex.unlock(); }; // todo: 会不会死锁
    *term = _currentTerm;
    *isLeader = (_status == Leader);
}

void Raft::InstallSnapshot(const raftRpcProtoc::InstallSnapshotRequest* args, raftRpcProtoc::InstallSnapshotResponse* reply)
{
    _mutex.lock();
    DEFER { _mutex.unlock(); };
    if (args->term() < _currentTerm) // leader 过期
    {
        reply->set_term(_currentTerm);
        return;
    }
    if (args->term() > _currentTerm)
    {
        _currentTerm = args->term();
        _votedFor = -1;
        _status = Follower;
        persist();
    }
    _status = Follower;
    _lastResetElectionTime = now(); // 更新计时器
    if (args->lastsnapshotincludeindex() <= _lastSnapshotIncludeIndex)
    {
        // leader 的日志过时
        return;
    }
    // 截断日志, 修改 commitIndex 和 lastApplied
    // todo: 由于 getSlicesIndexFromLogIndex 的实现, 目前不能传入不存在的 logIndex
    auto lastLogIndex = getLastLogIndex();
    if (lastLogIndex > args->lastsnapshotincludeindex())
    {
        // todo: 这是对的吗? 好像应该删除后半部分
        _logs.erase(_logs.begin(), _logs.begin() + getSliceIndexFromLogIndex(args->lastsnapshotincludeindex()) + 1);
    }
    else
    {
        _logs.clear();
    }
    _commitIndex = std::max(_commitIndex, args->lastsnapshotincludeindex());
    _lastApplied = std::max(_lastApplied, args->lastsnapshotincludeindex());
    _lastSnapshotIncludeIndex = args->lastsnapshotincludeindex();
    _lastSnapshotIncludeTerm = args->lastsnapshotincludeterm();

    reply->set_term(_currentTerm);
    ApplyMsg msg;
    msg._snapshotValid = true;
    msg._snapshot = args->data();
    msg._snapshotTerm = args->lastsnapshotincludeterm();
    msg._snapshotIndex = args->lastsnapshotincludeindex();

    _applyQueue->Push(msg);
    std::thread t(&Raft::pushMsgToKvServer, this, msg);
    t.detach();
    // todo: 是否还能优化
    _persister->Save(persistData(), args->data());
}

void Raft::leaderHearBeatTicker()
{
    while (true)
    {
        // todo: 目前对于非 leader 节点的操作是睡眠, 后续可进行优化
        while (_status != Leader)
        {
            usleep(1000 * HeartBeatTimeout);
        }
        static std::atomic<int32_t> atomicCount = 0;
        std::chrono::duration<signed long int, std::ratio<1, 1000000000>> suitableSleepTime{};
        std::chrono::system_clock::time_point wakeTime{};
        {
            std::lock_guard<std::mutex> lock(_mutex);
            wakeTime = now();
            suitableSleepTime = std::chrono::milliseconds(HeartBeatTimeout) + _lastResetHearBeatTime - wakeTime;
        }

        if (std::chrono::duration<double, std::milli>(suitableSleepTime).count() > 1) 
        {
            std::cout << atomicCount << "\033[1;35m leaderHearBeatTicker();函数设置睡眠时间为: "
                      << std::chrono::duration_cast<std::chrono::milliseconds>(suitableSleepTime).count() << " 毫秒\033[0m" << std::endl;
            // 获取当前时间点
            auto start = std::chrono::steady_clock::now();
            usleep(std::chrono::duration_cast<std::chrono::microseconds>(suitableSleepTime).count());
            // 获取函数运行结束后的时间点
            auto end = std::chrono::steady_clock::now();
            // 计算时间差并输出结果（单位为毫秒）
            std::chrono::duration<double, std::milli> duration = end - start;
            std::cout << atomicCount << "\033[1;35m leaderHearBeatTicker();函数实际睡眠时间为: " << duration.count() << " 毫秒\033[0m" << std::endl;
            ++atomicCount;
        }
        if (std::chrono::duration<double, std::milli>(_lastResetHearBeatTime - wakeTime).count() > 0)
        {
            // 睡眠时间内有重置定时器, 证明没有超时, 再次睡眠
            continue;
        }
        doHeartBeat();
    }
}

void Raft::leaderSendSnapshot(int server)
{
    // 1. 设置快照参数
    _mutex.lock();
    raftRpcProtoc::InstallSnapshotRequest args;
    args.set_leaderid(_self);
    args.set_term(_currentTerm);
    args.set_lastsnapshotincludeindex(_lastSnapshotIncludeIndex);
    args.set_lastsnapshotincludeterm(_lastSnapshotIncludeTerm);
    // 2. 设置返回信息并在对应节点更新快照
    raftRpcProtoc::InstallSnapshotResponse reply;
    _mutex.unlock();
    bool ok = _peers[server]->InstallSnapshot(&args, &reply);
    _mutex.lock();
    DEFER { _mutex.unlock(); };
    if (!ok)
    {
        return;
    }
    // 由于中间释放过锁, 所以可能导致节点身份的变化
    if (_status != Leader || _currentTerm != args.term())
    {
        return;
    }
    // 根据返回的响应信息, 判断对方任期号是否比自己新
    if (reply.term() > _currentTerm)
    {
        _currentTerm = reply.term();
        _votedFor = -1;
        _status = Follower;
        persist();
        // 自己已经不是 leader 了, 所以从现在开始重新选举计时
        _lastResetElectionTime = now();
        return;
    }
    // 走到这里证明一切顺利, 可以修改对应节点的日志索引和任期号了
    _matchIndex[server] = args.lastsnapshotincludeindex();
    _nextIndex[server] = _matchIndex[server] + 1;
}

void Raft::leaderUpdateCommitIndex()
{
    // 遍历, 找到过半已经复制的日志, 才能提交
    _commitIndex = _lastSnapshotIncludeIndex;
    for (int index = getLastLogIndex(); index >= _lastSnapshotIncludeIndex + 1; index--)
    {
        int sum = 0;
        for (int i = 0; i < _peers.size(); i++)
        {
            if (i == _self)
            {
                sum += 1;
                continue;
            }
            if (_matchIndex[i] >= index)
            {
                sum += 1;
            }
        }
        if (sum >= _peers.size() / 2 + 1 && getLogTermFromLogIndex(index) == _currentTerm)
        {
            _commitIndex = index;
            break;
        }
    }
}

bool Raft::matchLog(int logIndex, int logTerm)
{
    // 要保证 logIndex 是存在的, >= lastSnapshotIncludeIndex && <= getLastLogIndex
    myAssert(logIndex >= _lastSnapshotIncludeIndex && logIndex <= getLastLogIndex(), format(
             "the logIndex is not between lastSnapshotIncludeIndex and getLastLogIndex()"));
    return logTerm == getLogTermFromLogIndex(logIndex);
}

void Raft::persist()
{
    auto data = persistData();
    _persister->SaveRaftState(data);
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

int Raft::getLogTermFromLogIndex(int logIndex)
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
    _applyQueue->Push(msg);
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