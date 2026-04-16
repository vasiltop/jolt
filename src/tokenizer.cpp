#include <iostream>
#include <optional>
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
  Ret,
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
                                                         "=",
                                                         "<>",
                                                         "<",
                                                         ">",
                                                         "<=",
                                                         ">=",
                                                         ":",
                                                         "identifier",
                                                         "integer literal",
                                                         "real literal",
                                                         "string literal",
                                                         "and",
                                                         "begin",
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
                                                         "for"};

struct Token {
  Pos pos;
  TokenKind kind;
  std::string text;
};

struct Tokenizer {
  std::string data;
  Pos pos;
};

auto eof(const Tokenizer &tokenizer) -> bool {
  return tokenizer.pos.offset >= tokenizer.data.size();
}

auto next_char(Tokenizer &tokenizer) -> std::optional<char> {
  if (eof(tokenizer)) {
    return std::nullopt;
  }

  tokenizer.pos.col += 1;
  return tokenizer.data[tokenizer.pos.offset++];
}

auto peek_char(const Tokenizer &tokenizer) -> std::optional<char> {
  if (eof(tokenizer)) {
    return std::nullopt;
  }

  return tokenizer.data[tokenizer.pos.offset];
}

auto skip_whitespace(Tokenizer &tokenizer) -> void {
  while (auto c = peek_char(tokenizer)) {
    if (std::isspace(*c)) {
      c = next_char(tokenizer);

      if (c == '\n') {
        tokenizer.pos.line += 1;
        tokenizer.pos.col = 0;
      }
    } else {
      break;
    }
  }
}

auto consume(Tokenizer &tokenizer) -> Token {
  skip_whitespace(tokenizer);

  auto pos = tokenizer.pos;
  auto o = next_char(tokenizer);
  if (!o)
    return Token{.pos = pos, .kind = TokenKind::Eof, .text = ""};
  auto cur = *o;

  Token t{.pos = tokenizer.pos, .kind = TokenKind::Invalid};

  // These three are used for parsing multi character tokens
  // such as Identifiers and Integers
  const char *first = &tokenizer.data[pos.offset];
  size_t count{1};

  if (std::isalpha(cur)) {
    t.kind = TokenKind::Ident;

		for (auto o = peek_char(tokenizer); o && (std::isalnum(*o) || *o == '_'); o = peek_char(tokenizer)) {
			next_char(tokenizer);
			count++;
		}

    std::string_view ident_view(first, count);
    std::unordered_map<std::string_view, TokenKind> keywords{
        {"fn", TokenKind::Fn},
        {"ret", TokenKind::Ret},
    };

    if (auto it = keywords.find(ident_view); it != keywords.end()) {
      t.kind = it->second;
    }

    t.text = ident_view;

  } else if (std::isdigit(cur)) { // TODO: decimal numbers
    t.kind = TokenKind::Integer;


		for (auto o = peek_char(tokenizer); o && std::isdigit(*o); o = peek_char(tokenizer)) {
			next_char(tokenizer);
			count++;
		}

    t.text = std::string_view(first, count);

    // auto d = digit_view.data();
    // std::from_chars(d, d + digit_view.size(), result);
    //  auto [ptr, ec]
    //  TODO handle errors
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
