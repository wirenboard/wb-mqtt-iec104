#include "iec104_exception.h"

TConfigException::TConfigException(const std::string& message): std::runtime_error("Configuration error: " + message)
{}