#pragma once
#include <ctime>
#include <mutex>
#include <queue>
#include <chrono>
#include <thread>
#include <random>
#include <cstdio>
#include <cstdarg>
#include <sstream>
#include <iomanip>
#include <unistd.h>
#include <iostream>
#include <functional>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <condition_variable>
#include <boost/serialization/access.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include "config.h"

template <class F>
class DeferClass
{
public:
    DeferClass(F&& g) :_func(std::forward<F>(f)) {}
    DeferClass(const F& f) :_func(f) {}
    ~DeferClass() { _func(); }
    DeferClass(const DeferClass& e) = delete;
    DeferClass& operator=(const DeferClass& e) = delete;
private:
    F _func;
};

#define _CONCAT(a, b) a##b
#define _MAKE_DEFER_(line) DeferClass _CONCAT(defer_placeholder, line) = [&]()
#undef DEFER

#define DEFER _MAKE_DEFER_(__LINE__)

void DPrintf(const char* format, ...);
void myAssert(bool condition, std::string message = "Assertion failed!!");

template <typename... Args>
std::string format(const char* format_str, Args... args)
{
    std::stringstream ss;
    int _[] = {((ss << args), 0)...};
    (void)_;
    return ss.str();
}

std::chrono::_V2::system_clock::time_point now();
std::chrono::milliseconds getRandomizedElectionTimeout();
void sleepNMilliseconds(int N);

// 此队列用于以异步的形式写日志
template <typename T>
class LockQueue
{
public:
    // 写日志操作多个 work 线程都会进行
    void Push(const T& data)
    {
        // 用 RAII 的方式保证锁正确释放
        std::lock_guard<std::mutex> lock(_mutex);
        _queue.push(data);
        _condvariable.notify_one();
    }
    // 一个线程的读日志操作
    T pop()
    {
        std::unique_lock<std::mutex> lock(_mutex);
        while (_queue.empty())
        {
            _condvariable.wait(lock);
        }
        T data = _queue.front();
        _queue.pop();
        return data;
    }
    // 在很长时间内队列都为空的情况, 用一个定时器判断是否超时, 超时则返回空对象
    bool timeOutPop(int timeout, T* RetData)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        // 获取当前时间点, 计算出超时时间
        auto now = std::chrono::system_clock::now();
        auto timeout_time = now + std::chrono::milliseconds(timeout);
        // 超时之前轮询队列
        while (_queue.empty())
        {
            if (_condvariable.wait_until(lock, timeout_time) == std::cv_status::timeout)
            {
                return false;
            }
            else
            {
                continue;
            }
        }
        T data = _queue.front();
        _queue.pop();
        *RetData = data;
        return true;
    }
private:
    std::queue<T> _queue;
    std::mutex _mutex;
    std::condition_variable _condvariable;
};

// 此类用于封装 kv 传递给 Raft 的具体 command
class Op
{
public:
    std::string Operation; // "Get" "Put" "Append"
    std::string Key;
    std::string Value;
    std::string ClientId;
    int RequestId; // 客户端 id 请求的 request 序列号, 主要是为了保证线性一致
public:
    // todo: 当前的 command 只是 string, 有个限制是不能包含"|", 有待改进
    std::string asString() const
    {
        std::stringstream ss;
        boost::archive::text_oarchive oa(ss);
        oa << *this;
        return ss.str();
    }
    
    bool parseFromString(std::string str)
    {
        std::stringstream iss(str);
        boost::archive::text_iarchive ia(iss);
        ia >> *this;
        return true;
    }

    friend std::ostream& operator<<(std::ostream& os, const Op& obj)
    {
        os << "[MyClass:Operation{" + obj.Operation + "},Key{" + obj.Key + "},Value{" + obj.Value + "},ClientId{" +
                obj.ClientId + "},RequestId" + std::to_string(obj.RequestId) + "}";
        return os;
    }
private:
    friend class boost::serialization::access;
    template <class Archive>
    void serialize(Archive& ar, const unsigned int version)
    {
        ar& Operation;
        ar& Key;
        ar& Value;
        ar& ClientId;
        ar& RequestId;
    }
};

const std::string OK = "OK";
const std::string ErrNoKey = "ErrNoKey";
const std::string ErrWrongLeader = "ErrWrongLeader";

bool isReleasePort(unsigned short usPort);
bool getReleasePort(short& port);