#pragma once
#include "wire_codec.h"
#include <optional>

namespace Wire {
struct Task {
    quint32 requestId=0;
    qint16 amplitude=0,limit=100;
    quint32 duration=100,feedbackTimeout=100;
    quint8 node=1,action=1,timeoutPolicy=1,permission=1;
};
enum class Operation : quint8 {Open=1,Heartbeat=2,Grant=3,Stop=4,Clear=5,Close=6};
struct OperatorRequest {
    quint32 requestId=0,nonce=0,bootLow=0,bootHigh=0,session=0,sequence=0;
    Operation operation=Operation::Open;
    std::optional<Task> task;
};
QByteArray encodeTask(const Task &task,QString &error);
QByteArray encodeOperator(const OperatorRequest &request,QString &error);
bool withinCurrentGrantLimits(const Task &task);
}
