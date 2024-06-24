#pragma once

const bool Debug = true;
const int debugMul = 1; // 时间单位: time.Millsecond
const int HeartBeatTimeout = 25 * debugMul; // 心跳时间, 比选举超时时间小一个数量级
const int ApplyInterval = 10 * debugMul;
const int minRandomizedElectionTime = 300 * debugMul;
const int maxRandomizedElectionTime = 500 * debugMul;
const int CONSENSUS_TIMEOUT = 500 * debugMul;