#pragma once
#include <string>

class ApplyMsg
{
public:
    ApplyMsg()
        :_commandValid(false), _command(), _commandIndex(-1),
        _snapshotValid(false), _snapshotTerm(-1), _snapshotIndex(-1)
    {}
public:
    bool _commandValid;
    std::string _command;
    int _commandIndex;
    bool _snapshotValid;
    std::string _snapshot;
    int _snapshotTerm;
    int _snapshotIndex;
};