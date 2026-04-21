#pragma once

#include "errors.hpp"
#include "hir.hpp"
#include "types.hpp"
#include <expected>
#include <variant>

// TODO: Handle multiple errors at once
class Checker {
public:
  auto check_modules(ModulesHir &modules) -> std::expected<void, Error>;
};
