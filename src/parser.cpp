#include "tokenizer.cpp"
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>

struct Parser {
  Tokenizer tokenizer;
};

enum class ParserErrorKind {
  FileNotFound,
  ReadError,
};

struct ParserError {
  ParserErrorKind kind;
  std::string msg;
};

auto read_entire_file(const std::filesystem::path &path) -> std::expected<std::string, ParserError> {
  std::ifstream is{path, std::ios::ate | std::ios::binary};
  if (!is)
    return std::unexpected(ParserError{ParserErrorKind::FileNotFound});

  auto size = is.tellg();
  is.seekg(0);

  std::string out(size, '\0');
  if (!is.read(out.data(), size))
    return std::unexpected(ParserError{ParserErrorKind::ReadError});

  return out;
}

auto parser_init(Parser &parser, const std::filesystem::path &path) -> bool {
  auto data = read_entire_file(path);
  if (!data.has_value())
    return false;

  Tokenizer tokenizer{std::move(*data)};

	for (auto tok = consume(tokenizer); tok.kind != TokenKind::Eof; tok = consume(tokenizer)) {
		std::cout << tok.text << " - " << token_kind_string[static_cast<size_t>(tok.kind)] << std::endl;
	}

  return true;
}
