#pragma once

#include "errors.hpp"
#include "hir.hpp"
#include "types.hpp"
#include <expected>
#include <variant>

// TODO: Handle multiple errors at once
class Checker {
private:
  struct Context {
    HirFnDef *current_fn = nullptr;
  } ctx;

public:
  auto check_modules(ModulesHir &modules)
      -> std::expected<void, Error<CheckerError>> {
    // TODO: parse the function / struct / etc .. definitions in the modules
    // and verify that there are no errors.

    for (auto &[module_name, hir_vector] : modules) {
      for (auto &node : hir_vector) {
        auto err = visit(node);
        PROP_ERR(err);
      }
    }

    return {};
  }

  auto check(HirReturn &ret) -> std::expected<void, Error<CheckerError>> {
    // TODO: Check the contents
    ret.type = Type{.data = PrimitiveType{.kind = PrimitiveKind::Int}};

    return {};
  }

  auto check(HirFnDef &fn) -> std::expected<void, Error<CheckerError>> {
    auto *old_fn = ctx.current_fn;
    ctx.current_fn = &fn;

		// TODO: Parse return type.
    fn.type = Type{.data = PrimitiveType{.kind = PrimitiveKind::Int}};

    for (auto &node : fn.block.stmts) {
      auto err = visit(node);
      PROP_ERR(err);
    }

    ctx.current_fn = old_fn;
    return {};
  }

  auto visit(Hir &hir) -> std::expected<void, Error<CheckerError>> {
    return std::visit([&](auto &&arg) { return check(arg); }, hir);
  }

  auto visit(HirStmt &stmt) -> std::expected<void, Error<CheckerError>> {
    return std::visit([&](auto &&arg) { return check(arg); }, stmt.item);
  }
};
