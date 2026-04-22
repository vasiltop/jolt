#pragma once

#include <string>
#include <unordered_map>

struct Pos {
  int line;
  int col;
  size_t offset;
};

enum class TokenKind {
  Invalid,
  Eof,

  Add,
  Sub,
  Mul,
  Div,
  Mod,
  Dot,
  Comma,
  Semicolon,
  VerticalBar,
  ParenOpen,
  ParenClose,
  BracketOpen,
  BracketClose,
  BraceOpen,
  BraceClose,
  Assign,
  Equal,
  NotEqual,
  LessThan,
  GreaterThan,
  LessThanEqual,
  GreaterThanEqual,
  Colon,
  Ampersand,

  Ident,
  Integer,
  Real,
  String,
  Char,

  And,
  As,
  Const,
  Else,
  False,
  True,
  If,
  Import,
  Null,
  Not,
  Or,
  Fn,
  Ret,
  Let,
  While,
  For,
  Struct,
  Enum,
  Break,
  Continue,
};

// Must match the order of TokenKind
static constexpr std::string_view token_kind_string[] = {"invalid",
                                                         "eof",
                                                         "+",
                                                         "-",
                                                         "*",
                                                         "/",
                                                         "%",
                                                         ".",
                                                         ",",
                                                         ";",
                                                         "|",
                                                         "(",
                                                         ")",
                                                         "[",
                                                         "]",
                                                         "{",
                                                         "}",
                                                         "=",
                                                         "==",
                                                         "!=",
                                                         "<",
                                                         ">",
                                                         "<=",
                                                         ">=",
                                                         ":",
                                                         "&",
                                                         "identifier",
                                                         "integer literal",
                                                         "real literal",
                                                         "string literal",
                                                         "char literal",
                                                         "and",
                                                         "as",
                                                         "const",
                                                         "else",
                                                         "false",
                                                         "true",
                                                         "if",
                                                         "import",
                                                         "null",
                                                         "not",
                                                         "or",
                                                         "fn",
                                                         "ret",
                                                         "let",
                                                         "while",
                                                         "for",
                                                         "struct",
                                                         "enum",
                                                         "break",
                                                         "continue"};

inline std::unordered_map<std::string_view, TokenKind> keywords{
    {"fn", TokenKind::Fn},         {"ret", TokenKind::Ret},
    {"let", TokenKind::Let},
    {"while", TokenKind::While},   {"for", TokenKind::For},
    {"if", TokenKind::If},         {"else", TokenKind::Else},
    {"import", TokenKind::Import}, {"const", TokenKind::Const},
    {"null", TokenKind::Null},     {"true", TokenKind::True},
    {"false", TokenKind::False},   {"and", TokenKind::And},
    {"or", TokenKind::Or},         {"not", TokenKind::Not},
    {"struct", TokenKind::Struct}, {"enum", TokenKind::Enum},
    {"as", TokenKind::As},
    {"break", TokenKind::Break},   {"continue", TokenKind::Continue}};

struct Token {
  Pos pos;
  TokenKind kind;
  std::string text;
};

inline auto token_is_unary_op(const Token &tok) -> bool {
  if (tok.kind == TokenKind::Mul || tok.kind == TokenKind::Ampersand ||
      tok.kind == TokenKind::Not || tok.kind == TokenKind::Sub) {
    return true;
  }
  return false;
}

inline auto token_precedence(const Token &tok) -> int {
  switch (tok.kind) {
  case TokenKind::As:
    return 6;
  case TokenKind::Mul:
  case TokenKind::Div:
  case TokenKind::Mod:
    return 5;
  case TokenKind::Add:
  case TokenKind::Sub:
    return 4;
  case TokenKind::LessThan:
  case TokenKind::GreaterThan:
  case TokenKind::LessThanEqual:
  case TokenKind::GreaterThanEqual:
    return 3;
  case TokenKind::NotEqual:
  case TokenKind::Equal:
    return 2;
  case TokenKind::And:
    return 1;
  case TokenKind::Or:
    return 0;
  default:
    return -1;
  }
}
