#pragma once
#include <string>
#include <vector>

#define PROP_ERR(val)                                                          \
  if (!val.has_value())                                                        \
    return std::unexpected(val.error());

struct Error {
  std::string msg;
};

using ve = std::vector<Error>;
