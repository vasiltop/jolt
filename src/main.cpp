#include "parser.cpp"
#include <filesystem>

auto main() -> int {
  auto filename = std::filesystem::canonical("./examples/test.jolt");
  Parser parser{};
  parser_init(parser, filename);
}
