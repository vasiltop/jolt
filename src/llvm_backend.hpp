#pragma once

#include "hir.hpp"
#include "llir.hpp"

void jolt_emit_llvm_ir(const LlirModule &llir, const ModulesHir &modules,
                       std::vector<std::string> clang_args);
