#pragma once

#include "errors.hpp"
#include "tokens.hpp"
#include <format>
#include <string>
#include <string_view>

struct ModuleSource {
  std::string path;
  std::string text;
};

inline auto line_at_offset(std::string_view data, size_t offset) -> std::string {
  if (offset > data.size())
    offset = data.size();

  size_t line_start = data.rfind('\n', offset);
  if (line_start == std::string_view::npos) {
    line_start = 0;
  } else {
    line_start++;
  }

  size_t line_end = data.find('\n', offset);
  if (line_end == std::string_view::npos) {
    line_end = data.size();
  }

  return std::string(data.substr(line_start, line_end - line_start));
}

inline auto make_source_error(std::string_view filename,
                              std::string_view source, Pos pos,
                              std::string_view message) -> Error {
  std::string line_content = line_at_offset(source, pos.offset);
  std::string pointer(pos.col > 0 ? static_cast<size_t>(pos.col) - 1 : 0, ' ');
  pointer += "^";
  return Error{.msg = std::format("{}:{}:{}: error: {}\n"
                                 "    |\n"
                                 "{:4}| {}\n"
                                 "    | {}",
                                 filename, pos.line, pos.col, message, pos.line,
                                 line_content, pointer)};
}
