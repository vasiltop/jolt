#pragma once
#include <string>
#include <variant>
#include <vector>

#define PROP_ERR(val)                                                          \
  if (!val.has_value())                                                        \
    return std::unexpected(val.error());

enum class ParserError {
  FileNotFound,
  ReadError,
  TokenizerError,
};

enum class CheckerError {};

enum class TokenizerError {
  UnexpectedToken,
};

template <typename T> struct Error {
  T kind;
  std::string msg;
};

using Errors = std::vector<std::variant<Error<ParserError>, Error<CheckerError>,
                                        Error<TokenizerError>>>;
