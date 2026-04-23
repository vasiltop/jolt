#include "dump.hpp"
#include "parser.hpp"
#include <filesystem>
#include <iostream>
#include <string>

auto main(int argc, char **argv) -> int {
  std::string filename = "./examples/values.jolt";
  
  if (argc > 1) {
    filename = argv[1];
  }

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
