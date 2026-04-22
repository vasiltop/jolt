#pragma once

#include "diagnostics.hpp"
#include "errors.hpp"
#include "hir.hpp"
#include <set>
#include <string_view>
#include <unordered_map>
#include <vector>

struct Scope {
  // Parent always outlives the child
  Scope *parent;
  std::set<HirImport *> imported_modules;

  // We know these will all be alive during the lifetime of the checker
  std::unordered_map<std::string, HirFnDef *> functions;
  std::unordered_map<std::string, HirStruct *> structs;
  std::unordered_map<std::string, HirEnum *> enums;
  std::unordered_map<std::string, HirLet *> variables;
};

class Checker {
public:
  void
  check_modules(ModulesHir &modules, std::vector<Error> &errors,
                const std::unordered_map<std::string, ModuleSource> &sources);

private:
  void add_error(std::vector<Error> &errors, Pos pos, std::string_view message);
  void add_error(std::vector<Error> &errors, const std::string &module_id,
                 Pos pos, std::string_view message);

  static auto collect_import_edges(const ModulesHir &modules)
      -> std::unordered_map<std::string, std::vector<std::string>>;
  static auto find_import_pos(const ModulesHir &modules,
                              const std::string &from_module,
                              const std::string &to_module) -> Pos;
  auto has_import_cycles(
      const ModulesHir &modules,
      const std::unordered_map<std::string, std::vector<std::string>> &edges,
      std::vector<Error> &errors) -> bool;

  const std::unordered_map<std::string, ModuleSource> *sources_{nullptr};
  std::unordered_map<std::string, Scope *> module_global_scope;

  void check(HirStmt &stmt, std::vector<Error> &errors, Scope &scope);
  void check(HirBlock &block, std::vector<Error> &errors, Scope &scope);
  void check(HirReturn &ret, std::vector<Error> &errors, Scope &scope);
  void check(HirBreak &brk, std::vector<Error> &errors, Scope &scope);
  void check(HirContinue &cont, std::vector<Error> &errors, Scope &scope);
  void check(HirLet &let, std::vector<Error> &errors, Scope &scope);
  void check(HirAssign &assign, std::vector<Error> &errors, Scope &scope);
  void check(HirExprStmt &expr_stmt, std::vector<Error> &errors, Scope &scope);
  void check(HirIf &if_stmt, std::vector<Error> &errors, Scope &scope);
  void check(HirWhile &while_stmt, std::vector<Error> &errors, Scope &scope);
  void check(HirFor &for_stmt, std::vector<Error> &errors, Scope &scope);
  void check(HirExpr &expr, std::vector<Error> &errors, Scope &scope);
  void check(HirExprLiteral &literal, std::vector<Error> &errors, Scope &scope);
  void check(HirExprIdent &ident, std::vector<Error> &errors, Scope &scope);
  void check(HirExprPath &path, std::vector<Error> &errors, Scope &scope);
  void check(HirExprBinary &binary, std::vector<Error> &errors, Scope &scope);
  void check(HirExprUnary &unary, std::vector<Error> &errors, Scope &scope);
  void check(HirExprIndex &index, std::vector<Error> &errors, Scope &scope);
  void check(HirExprMember &member, std::vector<Error> &errors, Scope &scope);
  void check(HirExprCall &call, std::vector<Error> &errors, Scope &scope);
  void check(HirExprAs &as_expr, std::vector<Error> &errors, Scope &scope);
  void check(HirExprArray &array, std::vector<Error> &errors, Scope &scope);
  void check(HirExprStruct &struct_expr, std::vector<Error> &errors,
             Scope &scope);
  void check(StructExprField &field, std::vector<Error> &errors, Scope &scope);

  void check(HirType &type, std::vector<Error> &errors, Scope &scope);
  void check(HirTypePath &path, std::vector<Error> &errors, Scope &scope);
  void check(HirTypePtr &ptr, std::vector<Error> &errors, Scope &scope);
  void check(HirTypedIdent &typed, std::vector<Error> &errors, Scope &scope);

  void check(HirFnDef &fn, std::vector<Error> &errors, Scope &scope);
  void check(HirStruct &strct, std::vector<Error> &errors, Scope &scope);
  void check(HirEnum &enm, std::vector<Error> &errors, Scope &scope);
  void check(HirImport &imp, std::vector<Error> &errors, Scope &scope);

  std::string current_module_;
};
