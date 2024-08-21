#include "iec104_exception.h"

TEmptyConfigException::TEmptyConfigException(): std::runtime_error("Config file is empty")
{}

TConfigException::TConfigException(const std::string& message): std::runtime_error("Config error:" + message)
{}
