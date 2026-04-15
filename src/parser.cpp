#include "tokenizer.cpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>

struct Parser {
  Tokenizer tokenizer;
};

std::optional<std::string> read_entire_file(const std::filesystem::path &path) {
	std::ifstream is{path, std::ios::ate | std::ios::binary};
	if (!is) return std::nullopt;

	auto size = is.tellg();
	is.seekg(0);

	std::string out(size, '\0');
	if(!is.read(out.data(), size)) return std::nullopt;

	return out;
}

bool parser_init(Parser &parser, const std::filesystem::path &path) {
	auto data = read_entire_file(path);
	if (!data) return false;
	
	Tokenizer tokenizer{std::move(*data)};

	return true;
}
