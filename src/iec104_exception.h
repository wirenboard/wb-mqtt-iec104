#pragma once

#include <stdexcept>
#include <string>

class TConfigException: public std::runtime_error
{
public:
    explicit TConfigException(const std::string& message);
};

class TEmptyConfigException: public std::runtime_error
{
public:
    explicit TEmptyConfigException();
};
