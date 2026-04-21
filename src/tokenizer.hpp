#pragma once

#include "errors.hpp"
#include "tokens.hpp"
#include <expected>
#include <format>
#include <iostream>
#include <optional>
#include <string>

class Tokenizer {
public:
  Tokenizer(std::string filename, std::string data)
      : filename_(filename), data_(std::move(data)), pos_({1, 0, 0}) {}

  auto eof() const -> bool { return pos_.offset >= data_.size(); }

  auto next_char() -> std::optional<char> {
    if (eof()) {
      return std::nullopt;
    }

    pos_.col += 1;
    return data_[pos_.offset++];
  }

  auto peek_char() const -> std::optional<char> {
    if (eof()) {
      return std::nullopt;
    }

    return data_[pos_.offset];
  }

  auto skip_whitespace() -> void {
    while (auto c = peek_char()) {
      if (std::isspace(*c)) {
        c = next_char();

        if (c == '\n') {
          pos_.line += 1;
          pos_.col = 0;
        }
      } else if (*c == '/' && pos_.offset + 1 < data_.size() &&
                 data_[pos_.offset + 1] == '/') {
        while (auto cc = peek_char()) {
          if (*cc == '\n') {
            break;
          }
          next_char();
        }
      } else {
        break;
      }
    }
  }

  auto expect_token_and_pop(TokenKind kind) -> std::expected<Token, Error> {
    auto tok = consume();

    if (tok.kind != kind) {
      auto found = token_kind_string[static_cast<size_t>(tok.kind)];
      auto expected = token_kind_string[static_cast<size_t>(kind)];

      return std::unexpected(Error{
          .msg = std::format("{}:{}:{}: error: expected token {}, found {}.",
                             get_filename(), tok.pos.line, tok.pos.col,
                             expected, found)});
    }

    return tok;
  }

  auto peek() -> Token {
    if (!peeked_token_) {
      peeked_token_ = next_token_impl();
    }
    return *peeked_token_;
  }

  auto consume() -> Token {
    if (peeked_token_) {
      Token t = *peeked_token_;
      peeked_token_ = std::nullopt;
      return t;
    }
    return next_token_impl();
  }

private:
  auto next_token_impl() -> Token {
    skip_whitespace();

    auto start_pos = pos_;
    auto o = next_char();
    if (!o)
      return Token{.pos = start_pos, .kind = TokenKind::Eof, .text = ""};
    auto cur = *o;

    Token t{.pos = start_pos, .kind = TokenKind::Invalid};

    // These two are used for parsing multi character tokens
    // such as Identifiers and Integers
    const auto first = &data_[start_pos.offset];
    size_t count{1};

    if (std::isalpha(cur)) {
      t.kind = TokenKind::Ident;

      for (auto o = peek_char(); o && (std::isalnum(*o) || *o == '_');
           o = peek_char()) {
        next_char();
        count++;
      }

      std::string_view ident_view(first, count);

      if (auto it = keywords.find(ident_view); it != keywords.end()) {
        t.kind = it->second;
      }

      t.text = ident_view;

    } else if (std::isdigit(cur)) { // TODO: decimal numbers
      t.kind = TokenKind::Integer;

      for (auto o = peek_char(); o && std::isdigit(*o); o = peek_char()) {
        next_char();
        count++;
      }

      t.text = std::string_view(first, count);

    } else {
      t.text = cur;
      switch (cur) {
      case '(':
        t.kind = TokenKind::ParenOpen;
        break;
      case ')':
        t.kind = TokenKind::ParenClose;
        break;
      case '{':
        t.kind = TokenKind::BraceOpen;
        break;
      case '}':
        t.kind = TokenKind::BraceClose;
        break;
      case ';':
        t.kind = TokenKind::Semicolon;
        break;
      }
    }

    return t;
  }

  std::string get_filename() const { return filename_; }

public:
  auto reset() -> void {
    pos_ = {1, 0, 0};
    peeked_token_ = std::nullopt;
  }

  auto print_tokens() -> void {
    auto saved_pos = pos_;
    auto saved_peeked = peeked_token_;

    reset();

    while (true) {
      auto t = consume();
      auto kind_str = token_kind_string[static_cast<size_t>(t.kind)];

      std::cout << std::format("[{}:{}:{}] {} '{}'\n", filename_, t.pos.line,
                               t.pos.col, kind_str, t.text);

      if (t.kind == TokenKind::Eof || t.kind == TokenKind::Invalid) {
        break;
      }
    }

    pos_ = saved_pos;
    peeked_token_ = saved_peeked;
  }

private:
  std::optional<Token> peeked_token_;
  std::string filename_;
  std::string data_;
  Pos pos_;
};
