#include <filesystem>
#include "parser.cpp"

int main() {
	auto filename = std::filesystem::absolute("./examples/test.jolt");
	Parser parser{};
	parser_init(parser, filename);
}
