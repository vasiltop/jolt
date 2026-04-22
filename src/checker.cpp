#include "checker.hpp"
#include "types.hpp"
#include <format>
#include <functional>
#include <string>
#include <type_traits>
#include <variant>

static auto type_from_primitive_kind(PrimitiveKind kind) -> Type {
  return Type{.data = PrimitiveType{.kind = kind}};
}

auto Checker::collect_import_edges(const ModulesHir &modules)
    -> std::unordered_map<std::string, std::vector<std::string>> {
  std::unordered_map<std::string, std::vector<std::string>> res;

  for (auto &[name, hir] : modules) {
    for (auto &node : hir) {
      std::visit(
          [&](auto &&arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, HirImport>) {
              res[name].push_back(arg.target_module);
            }
          },
          node);
    }
  }

  return res;
}

auto Checker::find_import_pos(const ModulesHir &modules,
                              const std::string &from_module,
                              const std::string &to_module) -> Pos {
  auto mit = modules.find(from_module);
  if (mit == modules.end())
    return Pos{1, 1, 0};
  for (auto &node : mit->second) {
    if (auto *imp = std::get_if<HirImport>(&node)) {
      if (imp->target_module == to_module && !imp->path.empty())
        return imp->path[0].pos;
    }
  }
  return Pos{1, 1, 0};
}

auto Checker::has_import_cycles(
    const ModulesHir &modules,
    const std::unordered_map<std::string, std::vector<std::string>> &edges,
    std::vector<Error> &errors) -> bool {
  enum class State { NotStarted, InProgress, Done };
  std::unordered_map<std::string, State> state;
  std::vector<std::string> path;
  bool found = false;

  std::function<bool(const std::string &)> dfs =
      [&](const std::string &id) -> bool {
    auto &st = state[id];
    if (st == State::InProgress) {
      if (!found) {
        std::string cycle;
        for (size_t i = 0; i < path.size(); ++i) {
          if (i)
            cycle += " -> ";
          cycle += path[i];
        }
        if (!path.empty())
          cycle += " -> ";
        cycle += id;

        const std::string &from_module = path.empty() ? id : path.back();
        Pos pos = find_import_pos(modules, from_module, id);
        add_error(errors, from_module, pos,
                  std::format("import cycle: {}", cycle));
        found = true;
      }
      return true;
    }
    if (st == State::Done)
      return false;

    st = State::InProgress;
    path.push_back(id);

    auto it = edges.find(id);
    if (it != edges.end()) {
      for (const auto &to : it->second) {
        if (dfs(to))
          return true;
      }
    }

    path.pop_back();
    st = State::Done;
    return false;
  };

  for (const auto &[id, _] : edges) {
    if (state[id] == State::NotStarted && dfs(id))
      return true;
  }

  return found;
}

void Checker::check(HirStmt &stmt, std::vector<Error> &errors, Scope &scope) {
  std::visit(
      [&](auto &&arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::unique_ptr<HirIf>>)
          check(*arg, errors, scope);
        else if constexpr (std::is_same_v<T, std::unique_ptr<HirWhile>>)
          check(*arg, errors, scope);
        else if constexpr (std::is_same_v<T, std::unique_ptr<HirFor>>)
          check(*arg, errors, scope);
        else
          check(arg, errors, scope);
      },
      stmt.item);
}

void Checker::check(HirBlock &block, std::vector<Error> &errors, Scope &scope) {
  Scope block_scope{.parent = &scope};
  for (auto &stmt : block.stmts)
    check(stmt, errors, block_scope);
}

void Checker::check(HirReturn &ret, std::vector<Error> &errors, Scope &scope) {
  if (ret.expression)
    check(*ret.expression, errors, scope);
}

void Checker::check(HirBreak &brk, std::vector<Error> &errors, Scope &scope) {}

void Checker::check(HirContinue &cont, std::vector<Error> &errors,
                    Scope &scope) {}

void Checker::check(HirLet &let, std::vector<Error> &errors, Scope &scope) {
  scope.variables[let.name.text] = &let;
  if (let.explicit_type)
    check(*let.explicit_type, errors, scope);
  if (let.initializer)
    check(*let.initializer, errors, scope);
}

void Checker::check(HirAssign &assign, std::vector<Error> &errors,
                    Scope &scope) {
  check(assign.lvalue, errors, scope);
  check(assign.rvalue, errors, scope);
}

void Checker::check(HirExprStmt &expr_stmt, std::vector<Error> &errors,
                    Scope &scope) {
  check(expr_stmt.expr, errors, scope);
}

void Checker::check(HirIf &if_stmt, std::vector<Error> &errors, Scope &scope) {
  check(if_stmt.condition, errors, scope);
  check(if_stmt.then_block, errors, scope);
  if (if_stmt.else_block)
    check(*if_stmt.else_block, errors, scope);
}

void Checker::check(HirWhile &while_stmt, std::vector<Error> &errors,
                    Scope &scope) {
  check(while_stmt.condition, errors, scope);
  check(while_stmt.block, errors, scope);
}

void Checker::check(HirFor &for_stmt, std::vector<Error> &errors,
                    Scope &scope) {
  if (for_stmt.init)
    check(*for_stmt.init, errors, scope);
  check(for_stmt.condition, errors, scope);
  if (for_stmt.update)
    check(*for_stmt.update, errors, scope);
  check(for_stmt.block, errors, scope);
}

void Checker::check(HirExprLiteral &literal, std::vector<Error> &errors,
                    Scope &scope) {
  switch (literal.tok.kind) {
  case TokenKind::Integer: {
    literal.type = type_from_primitive_kind(PrimitiveKind::I32);
  } break;
  case TokenKind::String: {
    literal.type = type_from_primitive_kind(PrimitiveKind::String);
  } break;
  case TokenKind::Char: {
    literal.type = type_from_primitive_kind(PrimitiveKind::Char);
  } break;
  case TokenKind::Real: {
    literal.type = type_from_primitive_kind(PrimitiveKind::Real);
  } break;
  case TokenKind::Null: {
    literal.type = type_from_primitive_kind(PrimitiveKind::Null);
  } break;
  case TokenKind::True:
  case TokenKind::False: {
    literal.type = type_from_primitive_kind(PrimitiveKind::Bool);
  } break;
  default:
    add_error(errors, literal.tok.pos,
              "Invalid token type for expression literal");
  }
}

void Checker::check(HirExprIdent &ident, std::vector<Error> &errors,
                    Scope &scope) {}

void Checker::check(HirExprPath &path, std::vector<Error> &errors,
                    Scope &scope) {
  if (!path.generic_args)
    return;
  for (auto &g : *path.generic_args) {
    if (g)
      check(*g, errors, scope);
  }
}

void Checker::check(HirExprBinary &binary, std::vector<Error> &errors,
                    Scope &scope) {
  check(*binary.lhs, errors, scope);
  check(*binary.rhs, errors, scope);
}

void Checker::check(HirExprUnary &unary, std::vector<Error> &errors,
                    Scope &scope) {
  check(*unary.expr, errors, scope);
}

void Checker::check(HirExprIndex &index, std::vector<Error> &errors,
                    Scope &scope) {
  check(*index.value, errors, scope);
  check(*index.index, errors, scope);
}

void Checker::check(HirExprMember &member, std::vector<Error> &errors,
                    Scope &scope) {
  check(*member.object, errors, scope);
}

void Checker::check(HirExprCall &call, std::vector<Error> &errors,
                    Scope &scope) {
  check(*call.callee, errors, scope);
  for (auto &a : call.args)
    check(a, errors, scope);
}

void Checker::check(HirExprAs &as_expr, std::vector<Error> &errors,
                    Scope &scope) {
  check(*as_expr.expr, errors, scope);
  check(as_expr.type, errors, scope);
}

void Checker::check(HirExprArray &array, std::vector<Error> &errors,
                    Scope &scope) {
  for (auto &e : array.elements)
    check(e, errors, scope);
}

void Checker::check(HirExprStruct &struct_expr, std::vector<Error> &errors,
                    Scope &scope) {
  check(struct_expr.type, errors, scope);
  for (auto &f : struct_expr.fields)
    check(f, errors, scope);
}

void Checker::check(StructExprField &field, std::vector<Error> &errors,
                    Scope &scope) {
  if (field.value)
    check(*field.value, errors, scope);
}

void Checker::check(HirExpr &expr, std::vector<Error> &errors, Scope &scope) {
  std::visit(
      [&](auto &&arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, HirExprLiteral>) {
          check(arg, errors, scope);
        } else if constexpr (std::is_same_v<T, HirExprPath>) {
          check(arg, errors, scope);
        } else if constexpr (std::is_same_v<T, HirExprIdent>) {
          check(arg, errors, scope);
        } else if constexpr (std::is_same_v<T,
                                            std::unique_ptr<HirExprBinary>>) {
          check(*arg, errors, scope);
        } else if constexpr (std::is_same_v<T, std::unique_ptr<HirExprUnary>>) {
          check(*arg, errors, scope);
        } else if constexpr (std::is_same_v<T, std::unique_ptr<HirExprCall>>) {
          check(*arg, errors, scope);
        } else if constexpr (std::is_same_v<T, std::unique_ptr<HirExprIndex>>) {
          check(*arg, errors, scope);
        } else if constexpr (std::is_same_v<T,
                                            std::unique_ptr<HirExprMember>>) {
          check(*arg, errors, scope);
        } else if constexpr (std::is_same_v<T, std::unique_ptr<HirExprAs>>) {
          check(*arg, errors, scope);
        } else if constexpr (std::is_same_v<T, std::unique_ptr<HirExprArray>>) {
          check(*arg, errors, scope);
        } else if constexpr (std::is_same_v<T,
                                            std::unique_ptr<HirExprStruct>>) {
          check(*arg, errors, scope);
        }
      },
      expr.item);
}

void Checker::check(HirTypePath &path, std::vector<Error> &errors,
                    Scope &scope) {
  if (!path.generic_args)
    return;
  for (auto &g : *path.generic_args) {
    if (g)
      check(*g, errors, scope);
  }
}

void Checker::check(HirTypePtr &ptr, std::vector<Error> &errors, Scope &scope) {
  if (ptr.base)
    check(*ptr.base, errors, scope);
}

void Checker::check(HirType &type, std::vector<Error> &errors, Scope &scope) {
  std::visit(
      [&](auto &&arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, HirTypePath>) {
          check(arg, errors, scope);
        } else if constexpr (std::is_same_v<T, std::unique_ptr<HirTypePtr>>) {
          if (arg)
            check(*arg, errors, scope);
        }
      },
      type.item);
}

void Checker::check(HirTypedIdent &typed, std::vector<Error> &errors,
                    Scope &scope) {
  check(typed.type, errors, scope);
}

void Checker::check(HirFnDef &fn, std::vector<Error> &errors, Scope &scope) {
  scope.functions[fn.name.text] = &fn;
  for (auto &p : fn.params)
    check(p, errors, scope);
  if (fn.return_type)
    check(*fn.return_type, errors, scope);
  check(fn.block, errors, scope);
}

void Checker::check(HirStruct &strct, std::vector<Error> &errors,
                    Scope &scope) {
  scope.structs[strct.name.text] = &strct;
  for (auto &f : strct.fields)
    check(f, errors, scope);
}

void Checker::check(HirEnum &enm, std::vector<Error> &errors, Scope &scope) {
  scope.enums[enm.name.text] = &enm;
}

void Checker::check(HirImport &imp, std::vector<Error> &errors, Scope &scope) {
  scope.imported_modules.insert(&imp);
}

void Checker::add_error(std::vector<Error> &errors, Pos pos,
                        std::string_view message) {
  add_error(errors, current_module_, pos, message);
}

void Checker::add_error(std::vector<Error> &errors,
                        const std::string &module_id, Pos pos,
                        std::string_view message) {
  if (!sources_) {
    errors.push_back(Error{.msg = std::format("{}:{}:{}: error: {}", module_id,
                                              pos.line, pos.col, message)});
    return;
  }
  auto it = sources_->find(module_id);
  if (it == sources_->end()) {
    errors.push_back(Error{.msg = std::format("{}:{}:{}: error: {}", module_id,
                                              pos.line, pos.col, message)});
    return;
  }
  errors.push_back(
      make_source_error(it->second.path, it->second.text, pos, message));
}

void Checker::check_modules(
    ModulesHir &modules, std::vector<Error> &errors,
    const std::unordered_map<std::string, ModuleSource> &sources) {
  sources_ = &sources;
  const auto edges = collect_import_edges(modules);
  if (has_import_cycles(modules, edges, errors))
    return;

  for (auto &[name, hir] : modules) {
    Scope scope{.parent = nullptr};
    current_module_ = name;
    module_global_scope[name] = &scope;

    for (auto &item : hir) {
      std::visit([&](auto &&arg) { check(arg, errors, scope); }, item);
    }
  }
}
