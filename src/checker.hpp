#pragma once

#include "diagnostics.hpp"
#include "errors.hpp"
#include "hir.hpp"
#include <memory>
#include <set>
#include <string>
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
  std::unordered_map<std::string, HirTypedIdent *> fn_params;
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

  void scan_module_exports(const std::string &module_name,
                           std::vector<Hir> &hir, Scope &scope,
                           std::vector<Error> &errors);

  const std::unordered_map<std::string, ModuleSource> *sources_{nullptr};
  std::unordered_map<std::string, std::unique_ptr<Scope>> module_scope_storage_;
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
  void check(HirExprPath &path, std::vector<Error> &errors, Scope &scope);
  void check(HirExprEnumVariant &ev, std::vector<Error> &errors, Scope &scope);
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
  void check(HirTypePtr &ptr, std::vector<Error> &errors, Scope &scope);
  void check(HirTypedIdent &typed, std::vector<Error> &errors, Scope &scope);

  void check(HirFnDef &fn, std::vector<Error> &errors, Scope &scope);
  void check(HirStruct &strct, std::vector<Error> &errors, Scope &scope);
  void check(HirEnum &enm, std::vector<Error> &errors, Scope &scope);
  void check(HirImport &imp, std::vector<Error> &errors, Scope &scope);

  const ModulesHir *all_modules_{nullptr};

  static auto clone_type(const Type &t) -> Type;
  auto hir_to_type(const HirType &ht, std::vector<Error> &errors,
                   const Pos &pos, Scope &scope) -> std::optional<Type>;
  auto function_type_for(const HirFnDef &fn, std::vector<Error> &errors,
                         Scope &scope) -> std::optional<Type>;
  static auto types_compatible(const Type &a, const Type &b) -> bool;

  static auto lookup_let(Scope &scope, std::string_view name) -> HirLet *;
  static auto lookup_param(Scope &scope, std::string_view name)
      -> HirTypedIdent *;
  static auto lookup_fn(Scope &scope, std::string_view name) -> HirFnDef *;
  static auto lookup_struct(Scope &scope, std::string_view name) -> HirStruct *;
  static auto lookup_enum(Scope &scope, std::string_view name) -> HirEnum *;

  void resolve_path(HirExprPath &path, std::vector<Error> &errors,
                    Scope &scope);
  void resolve_expr_enum_variant(HirExprEnumVariant &ev, std::vector<Error> &errors,
                                 Scope &scope);
  auto resolve_struct_named(const NamedType &named, std::vector<Error> &errors,
                            const Pos &pos, Scope &scope) -> HirStruct *;
  auto resolve_enum_named(const NamedType &named, std::vector<Error> &errors,
                          const Pos &pos, Scope &scope) -> HirEnum *;
  auto scope_for_imported_module(Scope &scope, std::string_view alias)
      -> Scope *;
  auto type_of_field(HirStruct *st, std::string_view field,
                     std::vector<Error> &errors, const Pos &pos, Scope &scope)
      -> std::optional<Type>;

  std::string current_module_;
};
