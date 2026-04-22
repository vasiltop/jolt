#pragma once

#include "errors.hpp"
#include "tokenizer.hpp"
#include "tokens.hpp"
#include "types.hpp"
#include <expected>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

struct HirBase {
  std::optional<Type> type;
};

struct HirType;

struct HirTypePath {
  std::vector<Token> path;
};

struct HirTypePtr {
  std::unique_ptr<HirType> base;
};

using HirTypeItem = std::variant<HirTypePath, std::unique_ptr<HirTypePtr>>;

struct HirType {
  HirTypeItem item;

  static auto try_parse(Tokenizer &tokenizer) -> std::expected<HirType, Error>;
  std::string to_string() const;
};

struct HirExpr;
struct HirExprUnary;

struct HirExprLiteral : HirBase {
  Token tok;
};

struct HirExprIdent : HirBase {
  Token tok;
};

struct HirExprBinary : HirBase {
  Token op;
  std::unique_ptr<HirExpr> lhs;
  std::unique_ptr<HirExpr> rhs;
};

struct HirExprIndex : HirBase {
  std::unique_ptr<HirExpr> value;
  std::unique_ptr<HirExpr> index;
};

// For structs or enums
struct HirExprMember : HirBase {
  std::unique_ptr<HirExpr> object;
  Token member;
};

struct HirExprCall : HirBase {
  std::unique_ptr<HirExpr> callee;
  std::vector<HirExpr> args;
};

struct HirExprAs : HirBase {
  std::unique_ptr<HirExpr> expr;
  HirType type;
};

auto parse_primary(Tokenizer &tokenizer) -> std::expected<HirExpr, Error>;
auto parse_postfix(Tokenizer &tokenizer) -> std::expected<HirExpr, Error>;

struct HirExprUnary : HirBase {
  Token op;
  std::unique_ptr<HirExpr> expr;

  static auto try_parse(Tokenizer &tokenizer) -> std::expected<HirExpr, Error>;
};

using HirExprItem =
    std::variant<HirExprLiteral, HirExprIdent, std::unique_ptr<HirExprBinary>,
                 std::unique_ptr<HirExprUnary>, std::unique_ptr<HirExprCall>,
                 std::unique_ptr<HirExprIndex>, std::unique_ptr<HirExprMember>,
                 std::unique_ptr<HirExprAs>>;

struct HirExpr : HirBase {
  HirExprItem item;

  static auto try_parse(Tokenizer &tokenizer, int precedence = 0)
      -> std::expected<HirExpr, Error>;
};

// Statements
struct HirReturn : HirBase {
  std::optional<HirExpr> expression;

  static auto try_parse(Tokenizer &tokenizer)
      -> std::expected<HirReturn, Error>;
};

struct HirLet : HirBase {
  Token name;
  std::optional<HirType> explicit_type;
  std::optional<HirExpr> initializer;

  static auto try_parse(Tokenizer &tokenizer) -> std::expected<HirLet, Error>;
};

struct HirAssign : HirBase {
  HirExpr lvalue;
  HirExpr rvalue;
};

struct HirExprStmt : HirBase {
  HirExpr expr;
};

// Control Flow
struct HirBlock;
struct HirIf;
struct HirWhile;
struct HirFor;

using HirStmtItem =
    std::variant<HirReturn, HirLet, HirAssign, HirExprStmt,
                 std::unique_ptr<HirIf>, std::unique_ptr<HirWhile>,
                 std::unique_ptr<HirFor>>;

struct HirStmt : HirBase {
  HirStmtItem item;

  static auto try_parse(Tokenizer &tokenizer) -> std::expected<HirStmt, Error>;
};

struct HirBlock : HirBase {
  std::vector<HirStmt> stmts;

  static auto try_parse(Tokenizer &tokenizer) -> std::expected<HirBlock, Error>;
};

struct HirIf : HirBase {
  HirExpr condition;
  HirBlock then_block;
  std::optional<HirBlock> else_block;
};

struct HirWhile : HirBase {
  HirExpr condition;
  HirBlock block;
};

struct HirFor : HirBase {
  std::unique_ptr<HirStmt> init;
  HirExpr condition;
  std::unique_ptr<HirStmt> update;
  HirBlock block;
};

// Types & Signatures
struct HirTypedIdent : HirBase {
  Token name;
  HirType type;

  HirTypedIdent(Token name_, HirType type_) : name(name_), type(std::move(type_)) {};
};

// Top-Level Items
struct HirFnDef : HirBase {
  Token name;
  std::vector<Token> generics;
  std::vector<HirTypedIdent> params;
  std::optional<HirType> return_type;
  HirBlock block;

  static auto try_parse(Tokenizer &tokenizer) -> std::expected<HirFnDef, Error>;
};

struct HirStruct : HirBase {
  Token name;
  std::vector<Token> generics;
  std::vector<HirTypedIdent> fields;

  static auto try_parse(Tokenizer &tokenizer)
      -> std::expected<HirStruct, Error>;
};

struct HirEnum : HirBase {
  Token name;
  std::vector<Token> variants;

  static auto try_parse(Tokenizer &tokenizer) -> std::expected<HirEnum, Error>;
};

struct HirImport : HirBase {
  std::vector<Token> path;

  static auto try_parse(Tokenizer &tokenizer) -> std::expected<HirImport, Error>;
};

struct HirConst : HirBase {
  Token name;
  HirExpr initializer;
};

using Hir = std::variant<HirFnDef, HirStruct, HirEnum, HirImport, HirConst>;

struct ModuleScope {
    std::vector<Hir> items;
    std::unordered_map<std::string, std::unique_ptr<ModuleScope>> submodules;
};

using ModulesHir = std::unordered_map<std::string, ModuleScope>;
