#include "./include/mprpccontroller.h"

MprpcController::MprpcController()
    :_failed(false), _errText("")
{}

void MprpcController::Reset()
{
    _failed = false;
    _errText = "";
}

bool MprpcController::Failed() const
{
    return _failed;
}

std::string MprpcController::ErrorText() const
{
    return _errText;
}

void MprpcController::SetFailed(const std::string& reason)
{
    _failed = true;
    _errText = reason;
}