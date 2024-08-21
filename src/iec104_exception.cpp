#include "iec104_exception.h"

TEmptyConfigException::TEmptyConfigException(): std::runtime_error("Config file is empty")
{}
