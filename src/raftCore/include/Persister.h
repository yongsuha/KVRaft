#pragma once
#include <mutex>
#include <fstream>
#include "util.h"

class Persister
{
public:
    explicit Persister(int self);
    ~Persister();
    void Save(std::string raftState, std::string snapshot);
    std::string ReadSnapshot();
    void SaveRaftState(const std::string& data);
    long long RaftStateSize();
    std::string ReadRaftState();
private:
    void clearRaftState();
    void clearSnapshot();
    void clearRaftStateAndSnapshot();
private:
    std::mutex _mutex;
    std::string _raftState;
    std::string _snapshot;
    const std::string _raftStateFileName; // raftState 文件名
    const std::string _snapshotFileName; // snapshot 文件名
    std::ofstream _raftStateOutStream; // 保存 raftState 的输出流
    std::ofstream _snapshotOutStream; // 保存 snapshot 的输出流
    long long _raftStateSize; // raftState 的大小, 避免每次读取文件来获取具体大小
};