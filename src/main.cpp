#include "dump.hpp"
#include "parser.hpp"
#include <filesystem>
#include <iostream>
#include <string>

auto main(int argc, char **argv) -> int {
  std::string filename = "./examples/full_syntax.jolt";
  //   std::string filename = "./examples/full_syntax.jolt";

  Parser parser{};
  auto errors = parser.parse_path(filename);

  if (errors.size()) {
    for (auto &err : errors) {
      std::cout << err.msg << std::endl;
    }

    return 1;
  }

  print_modules(parser.get_modules());

  return 0;
}
