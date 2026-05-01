#include "dump.hpp"
#include "llir.hpp"
#include "llvm_backend.hpp"
#include "parser.hpp"
#include <filesystem>
#include <iostream>
#include <string_view>
#include <string>

auto main(int argc, char **argv) -> int {
  std::string filename = "./examples/values.jolt";
  bool emit_llvm_ir = false;

  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--emit-llvm") {
      emit_llvm_ir = true;
      continue;
    }
    filename = argv[i];
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

  LlirModule llir = lower_hir(parser.get_modules());
  if (emit_llvm_ir)
    print_llir(llir);

  jolt_emit_llvm_ir(llir, parser.get_modules());

  return 0;
}
