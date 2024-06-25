#include "./include/Persister.h"

explicit Persister::Persister(int self)
    :_raftStateFileName("raftstatePersist" + std::to_string(self) + ".txt"),
     _snapshotFileName("snapshotPersist" + std::to_string(self) + ".txt"), _raftStateSize(0)
{
    // 1. 检查文件状态并清空文件内容
    bool fileOpenFlag = true;
    std::fstream file(_raftStateFileName, std::ios::out | std::ios::trunc);
    if (file.is_open())
    {
        file.close();
    }
    else
    {
        fileOpenFlag = false;
    }
    file = std::fstream(_snapshotFileName, std::ios::out | std::ios::trunc);
    if (file.is_open())
    {
        file.close();
    }
    else
    {
        fileOpenFlag = false;
    }
    if (!fileOpenFlag)
    {
        DPrintf("[func-Persister::Persister] file open error!!");
    }
    // 2. 绑定流
    _raftStateOutStream.open(_raftStateFileName);
    _snapshotOutStream.open(_snapshotFileName);
}

Persister::~Persister()
{
    _raftStateSize = 0;
    if (_raftStateOutStream.is_open())
    {
        _raftStateOutStream.close();
    }
    // 重新打开文件流并清空文件内容
    _raftStateOutStream.open(_raftStateFileName, std::ios::out | std::ios::trunc);
}
// todo: 涉及到反复打开文件的操作, 暂时没有考虑文件出问题的情况
void Persister::Save(std::string raftState, std::string snapshot)
{
    std::lock_guard<std::mutex> lock(_mutex);
    clearRaftStateAndSnapshot();
    // 将 raftstate 和 snapshot 写入本地文件
    _raftStateOutStream << raftState;
    _snapshotOutStream << snapshot;
}

std::string Persister::ReadSnapshot()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_snapshotOutStream.is_open())
    {
        _snapshotOutStream.close();
    }
    DEFER
    {
        _snapshotOutStream.open(_snapshotFileName);
    };
    std::fstream ifs(_snapshotFileName, std::ios_base::in);
    if (!ifs.good())
    {
        return "";
    }
    std::string snapshot;
    ifs >> snapshot;
    ifs.close();
    return snapshot;
}

void Persister::SaveRaftState(const std::string& data)
{
    std::lock_guard<std::mutex> lock(_mutex);
    clearRaftState();
    _raftStateOutStream << data;
    _raftStateSize += data.size();
}

long long Persister::RaftStateSize()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _raftStateSize;
}

std::string Persister::ReadRaftState()
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::fstream ifs(_raftStateFileName, std::ios_base::in);
    if (!ifs.good())
    {
        return "";
    }
    std::string snapshot;
    ifs >> snapshot;
    ifs.close();
    return snapshot;
}

void Persister::clearRaftState()
{
    _raftStateSize = 0;
    if (_raftStateOutStream.is_open())
    {
        _raftStateOutStream.close();
    }
    _raftStateOutStream.open(_raftStateFileName, std::ios::out | std::ios::trunc);
}

void Persister::clearSnapshot()
{
    if (_snapshotOutStream.is_open())
    {
        _snapshotOutStream.close();
    }
    _snapshotOutStream.open(_snapshotFileName, std::ios::out | std::ios::trunc);
}

void Persister::clearRaftStateAndSnapshot()
{
    clearRaftState();
    clearSnapshot();
}