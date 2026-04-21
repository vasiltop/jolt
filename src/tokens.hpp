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

  And,
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
  Module,
  Let,
  While,
  For,
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
                                                         ":=",
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
                                                         "and",
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
                                                         "module",
                                                         "let",
                                                         "while",
                                                         "for"};

std::unordered_map<std::string_view, TokenKind> keywords{
    {"fn", TokenKind::Fn},
    {"ret", TokenKind::Ret},
    {"module", TokenKind::Module},
    {"let", TokenKind::Let},
    {"while", TokenKind::While},
    {"for", TokenKind::For},
    {"if", TokenKind::If},
    {"else", TokenKind::Else},
    {"import", TokenKind::Import},
    {"const", TokenKind::Const},
    {"null", TokenKind::Null},
    {"true", TokenKind::True},
    {"false", TokenKind::False},
    {"and", TokenKind::And},
    {"or", TokenKind::Or},
    {"not", TokenKind::Not},
};

struct Token {
  Pos pos;
  TokenKind kind;
  std::string text;
};
