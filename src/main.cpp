#include "dump.hpp"
#include "parser.hpp"
#include <filesystem>
#include <iostream>
#include <string>
#include <variant>

auto main(int argc, char **argv) -> int {
  std::string filename = "./examples/test.jolt";

  Parser parser{};
  auto errors = parser.parse_path(filename);

  if (errors.size()) {
    for (auto &err : errors) {
      std::visit([](auto &&arg) { std::cout << arg.msg << std::endl; }, err);
    }

    return 1;
  }

  print_modules(parser.get_modules());

  return 0;
}
