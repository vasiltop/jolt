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

struct HirExpr;
struct HirExprUnary;

struct HirExprLiteral {
  Token tok;
};

struct HirExprIdent {
  Token tok;
};

struct HirExprBinary {
  Token op;
  std::unique_ptr<HirExpr> lhs;
  std::unique_ptr<HirExpr> rhs;
};

struct HirExprIndex {
  std::unique_ptr<HirExpr> value;
  std::unique_ptr<HirExpr> index;
};

// For structs or enums
struct HirExprMember {
  std::unique_ptr<HirExpr> object;
  Token member;
};

struct HirExprCall {
  std::unique_ptr<HirExpr> callee;
  std::vector<HirExpr> args;
};

using HirExprItem =
    std::variant<HirExprLiteral, HirExprIdent, std::unique_ptr<HirExprBinary>,
                 std::unique_ptr<HirExprUnary>, std::unique_ptr<HirExprCall>,
                 std::unique_ptr<HirExprIndex>>;

struct HirExprUnary {
  Token op;
  std::unique_ptr<HirExpr> expr;

  static auto try_parse(Tokenizer &tokenizer)
      -> std::expected<std::unique_ptr<HirExprUnary>, Error> {
    auto tok = tokenizer.peek();

    if (token_is_unary_op(tok)) {
      tokenizer.consume();
      auto sub_expr = HirExprUnary::try_parse(tokenizer);
      PROP_ERR(sub_expr);

      auto expr = std::make_unique<HirExprUnary>();
      expr->op = tok;
      expr->expr = std::make_unique<HirExpr>(std::move(*sub_expr));
      return expr;
    }
  }
};

struct HirExpr : HirBase {
  HirExprItem item;

  static auto try_parse(Tokenizer &tokenizer) -> std::expected<HirExpr, Error> {
    // TODO: Implement expression parsing.
    // For now, we'll just consume tokens blindly until a statement terminator
    // to keep the AST structure intact while you build the parser.
    auto tok = tokenizer.consume();
    if (tok.kind == TokenKind::Integer || tok.kind == TokenKind::Real ||
        tok.kind == TokenKind::String || tok.kind == TokenKind::True ||
        tok.kind == TokenKind::False || tok.kind == TokenKind::Null) {
      return HirExpr{.item = HirExprLiteral{tok}};
    }
    return HirExpr{.item = HirExprIdent{tok}};
  }
};

// Statements
struct HirReturn : HirBase {
  std::optional<HirExpr> expression;

  static auto try_parse(Tokenizer &tokenizer)
      -> std::expected<HirReturn, Error> {
    auto ret_err = tokenizer.expect_token_and_pop(TokenKind::Ret);
    PROP_ERR(ret_err);

    std::optional<HirExpr> expr;
    if (tokenizer.peek().kind != TokenKind::Semicolon) {
      auto parsed_expr = HirExpr::try_parse(tokenizer);
      PROP_ERR(parsed_expr);
      expr = std::move(*parsed_expr);
    }

    auto sem_err = tokenizer.expect_token_and_pop(TokenKind::Semicolon);
    PROP_ERR(sem_err);

    return HirReturn{.expression = std::move(expr)};
  }
};

struct HirLet : HirBase {
  Token name;
  std::optional<Token> explicit_type;
  std::optional<HirExpr> initializer;

  static auto try_parse(Tokenizer &tokenizer) -> std::expected<HirLet, Error> {
    tokenizer.expect_token_and_pop(TokenKind::Let);
    auto name = tokenizer.expect_token_and_pop(TokenKind::Ident);
    PROP_ERR(name);

    std::optional<Token> explicit_type;
    if (tokenizer.peek().kind == TokenKind::Colon) {
      tokenizer.expect_token_and_pop(TokenKind::Colon);
      auto type_tok = tokenizer.expect_token_and_pop(TokenKind::Ident);
      PROP_ERR(type_tok);
      explicit_type = *type_tok;
    }

    std::optional<HirExpr> initializer;
    auto assign_tok = tokenizer.peek();
    if (assign_tok.kind == TokenKind::Assign ||
        assign_tok.kind == TokenKind::Equal) {
      tokenizer.consume();
      auto expr = HirExpr::try_parse(tokenizer);
      PROP_ERR(expr);
      initializer = std::move(*expr);
    }

    tokenizer.expect_token_and_pop(TokenKind::Semicolon);

    return HirLet{.name = *name,
                  .explicit_type = explicit_type,
                  .initializer = std::move(initializer)};
  }
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

  static auto try_parse(Tokenizer &tokenizer) -> std::expected<HirStmt, Error> {
    auto next = tokenizer.peek();
    if (next.kind == TokenKind::Ret) {
      auto ret = HirReturn::try_parse(tokenizer);
      PROP_ERR(ret);
      return HirStmt{.item = std::move(*ret)};
    } else if (next.kind == TokenKind::Let) {
      auto let = HirLet::try_parse(tokenizer);
      PROP_ERR(let);
      return HirStmt{.item = std::move(*let)};
    }
    // TODO: Handle If, While, For, Assign, ExprStmt
    // Fallback: parse an expression and consume a semicolon
    auto expr = HirExpr::try_parse(tokenizer);
    PROP_ERR(expr);
    tokenizer.expect_token_and_pop(TokenKind::Semicolon);
    return HirStmt{.item = HirExprStmt{.expr = std::move(*expr)}};
  }
};

struct HirBlock : HirBase {
  std::vector<HirStmt> stmts;

  static auto try_parse(Tokenizer &tokenizer)
      -> std::expected<HirBlock, Error> {
    tokenizer.expect_token_and_pop(TokenKind::BraceOpen);
    HirBlock block;
    while (tokenizer.peek().kind != TokenKind::BraceClose &&
           tokenizer.peek().kind != TokenKind::Eof) {
      auto stmt = HirStmt::try_parse(tokenizer);
      PROP_ERR(stmt);
      block.stmts.push_back(std::move(*stmt));
    }
    tokenizer.expect_token_and_pop(TokenKind::BraceClose);
    return block;
  }
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
  Token type;

  HirTypedIdent(Token name_, Token type_) : name(name_), type(type_) {};
};

// Top-Level Items
struct HirFnDef : HirBase {
  Token name;
  std::vector<Token> generics;
  std::vector<HirTypedIdent> params;
  std::optional<Token> return_type;
  HirBlock block;

  static auto try_parse(Tokenizer &tokenizer)
      -> std::expected<HirFnDef, Error> {
    tokenizer.expect_token_and_pop(TokenKind::Fn);
    auto name_tok = tokenizer.expect_token_and_pop(TokenKind::Ident);
    PROP_ERR(name_tok);

    std::vector<Token> generics;
    if (tokenizer.peek().kind == TokenKind::LessThan) {
      tokenizer.expect_token_and_pop(TokenKind::LessThan);
      auto first_generic = tokenizer.expect_token_and_pop(TokenKind::Ident);
      PROP_ERR(first_generic);
      generics.push_back(*first_generic);

      while (tokenizer.peek().kind != TokenKind::GreaterThan) {
        tokenizer.expect_token_and_pop(TokenKind::Comma);
        auto ident = tokenizer.expect_token_and_pop(TokenKind::Ident);
        PROP_ERR(ident);
        generics.push_back(*ident);
      }
      tokenizer.expect_token_and_pop(TokenKind::GreaterThan);
    }

    tokenizer.expect_token_and_pop(TokenKind::ParenOpen);

    std::vector<HirTypedIdent> params;
    while (tokenizer.peek().kind != TokenKind::ParenClose) {
      auto arg_name_tok = tokenizer.expect_token_and_pop(TokenKind::Ident);
      PROP_ERR(arg_name_tok);

      tokenizer.expect_token_and_pop(TokenKind::Colon);
      auto arg_type_tok = tokenizer.expect_token_and_pop(TokenKind::Ident);
      PROP_ERR(arg_type_tok);

      params.emplace_back(*arg_name_tok, *arg_type_tok);

      if (tokenizer.peek().kind == TokenKind::Comma) {
        tokenizer.consume();
      }
    }

    tokenizer.expect_token_and_pop(TokenKind::ParenClose);

    std::optional<Token> return_type_tok;
    if (tokenizer.peek().kind == TokenKind::Ident) {
      return_type_tok = tokenizer.consume();
    }

    auto body = HirBlock::try_parse(tokenizer);
    PROP_ERR(body);

    return HirFnDef{.name = *name_tok,
                    .generics = generics,
                    .params = params,
                    .return_type = return_type_tok,
                    .block = std::move(*body)};
  }
};

struct HirStruct : HirBase {
  Token name;
  std::vector<Token> generics;
  std::vector<HirTypedIdent> fields;

  static auto try_parse(Tokenizer &tokenizer)
      -> std::expected<HirStruct, Error> {
    tokenizer.expect_token_and_pop(TokenKind::Struct);
    auto name_tok = tokenizer.expect_token_and_pop(TokenKind::Ident);
    PROP_ERR(name_tok);

    std::vector<Token> generics;
    if (tokenizer.peek().kind == TokenKind::LessThan) {
      tokenizer.consume();
      while (tokenizer.peek().kind != TokenKind::GreaterThan) {
        auto t = tokenizer.consume();
        if (t.kind == TokenKind::Ident)
          generics.push_back(t);
      }
      tokenizer.expect_token_and_pop(TokenKind::GreaterThan);
    }

    tokenizer.expect_token_and_pop(TokenKind::BraceOpen);
    std::vector<HirTypedIdent> fields;
    while (tokenizer.peek().kind != TokenKind::BraceClose) {
      auto field_name = tokenizer.expect_token_and_pop(TokenKind::Ident);
      PROP_ERR(field_name);
      tokenizer.expect_token_and_pop(TokenKind::Colon);
      auto field_type = tokenizer.expect_token_and_pop(TokenKind::Ident);
      PROP_ERR(field_type);
      fields.emplace_back(*field_name, *field_type);
      tokenizer.expect_token_and_pop(TokenKind::Semicolon);
    }
    tokenizer.expect_token_and_pop(TokenKind::BraceClose);
    if (tokenizer.peek().kind == TokenKind::Semicolon)
      tokenizer.consume(); // Optional trailing ;

    return HirStruct{.name = *name_tok, .generics = generics, .fields = fields};
  }
};

struct HirEnum : HirBase {
  Token name;
  std::vector<Token> variants;

  static auto try_parse(Tokenizer &tokenizer) -> std::expected<HirEnum, Error> {
    tokenizer.expect_token_and_pop(TokenKind::Enum);
    auto name_tok = tokenizer.expect_token_and_pop(TokenKind::Ident);
    PROP_ERR(name_tok);

    tokenizer.expect_token_and_pop(TokenKind::BraceOpen);
    std::vector<Token> variants;
    while (tokenizer.peek().kind != TokenKind::BraceClose) {
      auto var_name = tokenizer.expect_token_and_pop(TokenKind::Ident);
      PROP_ERR(var_name);
      variants.push_back(*var_name);
      if (tokenizer.peek().kind == TokenKind::Comma)
        tokenizer.consume();
    }
    tokenizer.expect_token_and_pop(TokenKind::BraceClose);
    if (tokenizer.peek().kind == TokenKind::Semicolon)
      tokenizer.consume(); // Optional trailing ;

    return HirEnum{.name = *name_tok, .variants = variants};
  }
};

struct HirImport : HirBase {
  std::vector<Token> path;
};

struct HirConst : HirBase {
  Token name;
  HirExpr initializer;
};

using Hir = std::variant<HirFnDef, HirStruct, HirEnum, HirImport, HirConst>;
using ModulesHir = std::unordered_map<std::string, std::vector<Hir>>;
