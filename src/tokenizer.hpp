#pragma once

#include "errors.hpp"
#include "tokens.hpp"
#include <expected>
#include <optional>
#include <string>
#include <string_view>

class Tokenizer {
public:
  struct Checkpoint {
    Pos pos{};
    std::optional<Token> peeked_token;
  };

  Tokenizer(std::string filename, std::string data)
      : filename_(filename), data_(std::move(data)), pos_({1, 0, 0}) {}

  auto eof() const -> bool { return pos_.offset >= data_.size(); }
  auto next_char() -> std::optional<char>;
  auto peek_char() const -> std::optional<char>;
  auto skip_whitespace() -> void;
  auto get_line_contents(size_t offset) const -> std::string;
  /// Same layout as a failed `expect_token_and_pop` (file, line, caret line).
  auto make_error(Pos pos, std::string_view message) const -> Error;
  auto make_error(const Token &tok, std::string_view message) const -> Error;
  auto expect_token_and_pop(TokenKind kind) -> std::expected<Token, Error>;
  auto parse_module_name() -> std::expected<void, Error>;
  auto peek() -> Token;
  auto consume() -> Token;
  const std::string &get_filename() const { return filename_; }
  const std::string &get_module_name() const { return module_name_; }

  auto checkpoint() const -> Checkpoint;
  auto restore(Checkpoint c) -> void;

public:
  auto reset() -> void;
  auto print_tokens() -> void;

private:
  auto next_token_impl() -> Token;

private:
  std::optional<Token> peeked_token_;
  std::string filename_;
  std::string module_name_;
  std::string data_;
  Pos pos_;
};
