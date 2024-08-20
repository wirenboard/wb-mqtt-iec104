#pragma once

#include <stdexcept>

class TEmptyConfigException: public std::runtime_error
{
public:
    explicit TEmptyConfigException();
};
