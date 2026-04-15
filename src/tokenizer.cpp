#include <string>

struct Pos {
  int line;
  int col;
};

enum TokenKind {
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

  Ident,
  Integer,
  Real,
  String,

  And,
  Begin,
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
  Rat,
  Type,
  Let,
  While,
  For,
};

// Must match the order of TokenKind
static constexpr std::string_view token_kind_string[] = {
    "invalid", "eof",
    "+", "-", "*", "/", "%", ".", ",", ";",
    "|", "(", ")", "[", "]",
    "{", "}", ":=", "=", "<>",
    "<", ">", "<=", ">=", ":",
    "identifier", "integer literal", "real literal", "string literal",
    "and", "begin", "const", "else", "false", "true", "if", "import", "null", "not", "or",
    "fn", "rat", "type", "let", "while", "for"
};

struct Token {
  Pos pos;
  TokenKind kind;
  std::string text;
};

struct Tokenizer {
  std::string data;
};
