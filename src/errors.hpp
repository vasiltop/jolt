#pragma once
#include <string>

#define PROP_ERR(val)                                                          \
  if (!val.has_value())                                                        \
    return std::unexpected(val.error());

struct Error {
  std::string msg;
};
