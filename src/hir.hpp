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
  /// if set, the type is `Path<T, U, ...>`
  std::optional<std::vector<std::shared_ptr<HirType>>> generic_args;
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

// Stores any name lookup
struct HirExprPath : HirBase {
  std::vector<Token> segments;
  /// Set for `f<i32>(...)` and similar; struct literals use `HirType` on
  /// `HirExprStruct`.
  std::optional<std::vector<std::shared_ptr<HirType>>> generic_args;
  bool is_local() const { return segments.size() == 1; }
  std::string primary_name() const { return segments[0].text; }
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

struct HirExprArray : HirBase {
  std::vector<HirExpr> elements;
};

struct StructExprField {
  Token name;
  std::unique_ptr<HirExpr> value;
};

struct HirExprStruct : HirBase {
  HirType type;
  std::vector<StructExprField> fields;
};

using HirExprItem =
    std::variant<HirExprLiteral, HirExprPath, HirExprIdent,
                 std::unique_ptr<HirExprBinary>, std::unique_ptr<HirExprUnary>,
                 std::unique_ptr<HirExprCall>, std::unique_ptr<HirExprIndex>,
                 std::unique_ptr<HirExprMember>, std::unique_ptr<HirExprAs>,
                 std::unique_ptr<HirExprArray>, std::unique_ptr<HirExprStruct>>;

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

struct HirBreak : HirBase {
  static auto try_parse(Tokenizer &tokenizer) -> std::expected<HirBreak, Error>;
};

struct HirContinue : HirBase {
  static auto try_parse(Tokenizer &tokenizer)
      -> std::expected<HirContinue, Error>;
};

struct HirLet : HirBase {
  /// `const` at module scope or in a block vs `let`.
  bool is_const = false;
  Token name;
  std::optional<HirType> explicit_type;
  std::optional<HirExpr> initializer;

  static auto try_parse(Tokenizer &tokenizer, bool is_const)
      -> std::expected<HirLet, Error>;
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
    std::variant<HirReturn, HirBreak, HirContinue, HirLet, HirAssign,
                 HirExprStmt, std::unique_ptr<HirIf>, std::unique_ptr<HirWhile>,
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

  static auto try_parse(Tokenizer &tokenizer)
      -> std::expected<std::unique_ptr<HirIf>, Error>;
};

struct HirWhile : HirBase {
  HirExpr condition;
  HirBlock block;

  static auto try_parse(Tokenizer &tokenizer)
      -> std::expected<std::unique_ptr<HirWhile>, Error>;
};

struct HirFor : HirBase {
  std::unique_ptr<HirStmt> init;
  HirExpr condition;
  std::unique_ptr<HirStmt> update;
  HirBlock block;

  static auto try_parse(Tokenizer &tokenizer)
      -> std::expected<std::unique_ptr<HirFor>, Error>;
};

// Types & Signatures
struct HirTypedIdent : HirBase {
  Token name;
  HirType type;

  HirTypedIdent(Token name_, HirType type_)
      : name(name_), type(std::move(type_)) {};
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

  static auto try_parse(Tokenizer &tokenizer)
      -> std::expected<HirImport, Error>;
};

using Hir = std::variant<HirFnDef, HirStruct, HirEnum, HirImport, HirLet>;

using ModulesHir = std::unordered_map<std::string, std::vector<Hir>>;
