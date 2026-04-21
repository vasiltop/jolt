#pragma once

#include "errors.hpp"
#include "tokenizer.hpp"
#include "tokens.hpp"
#include "types.hpp"
#include <expected>
#include <optional>
#include <variant>
#include <vector>

// TODO: Include a position for errors.
struct HirBase {
  std::optional<Type> type;
};

struct HirReturn : HirBase {
  Token expression; // TODO: Make this an actual expression, not just an int.

  static auto try_parse(Tokenizer &tokenizer)
      -> std::expected<HirReturn, Error> {

    // TODO: Handle any expression type
    auto ret_err = tokenizer.expect_token_and_pop(TokenKind::Ret);
    PROP_ERR(ret_err);

    auto return_value = tokenizer.expect_token_and_pop(TokenKind::Integer);
    PROP_ERR(return_value);

    auto sem_err = tokenizer.expect_token_and_pop(TokenKind::Semicolon);
    PROP_ERR(ret_err);

    return HirReturn{.expression = *return_value};
  }
};

using HirStmtItem = std::variant<HirReturn>;

struct HirStmt : HirBase {
  HirStmtItem item;
};

struct HirBlock : HirBase {
  std::vector<HirStmt> stmts;
};

struct HirFnDef : HirBase {
  Token name;
  Token return_type;
  HirBlock block;

  HirFnDef(Token name_, Token return_type_, HirBlock block_)
      : name(name_), return_type(return_type_), block(std::move(block_)) {}

  static auto try_parse(Tokenizer &tokenizer)
      -> std::expected<HirFnDef, Error> {
    // We are ignoring the Fn since the parser already consumed it, this might
    // change.

    // TODO: For now this is hardcoded to the example
    auto name_tok = tokenizer.expect_token_and_pop(TokenKind::Ident);
    PROP_ERR(name_tok);
    auto oparen_err = tokenizer.expect_token_and_pop(TokenKind::ParenOpen);
    PROP_ERR(oparen_err);

    auto cparen_err = tokenizer.expect_token_and_pop(TokenKind::ParenClose);
    PROP_ERR(cparen_err);

    auto return_type_tok = tokenizer.expect_token_and_pop(TokenKind::Ident);
    PROP_ERR(return_type_tok);

    auto obrace_err = tokenizer.expect_token_and_pop(TokenKind::BraceOpen);
    PROP_ERR(obrace_err);

    HirBlock body;
    auto ret = HirReturn::try_parse(tokenizer);
    PROP_ERR(ret);
    body.stmts.push_back(HirStmt{*ret});

    auto cbrace_err = tokenizer.expect_token_and_pop(TokenKind::BraceClose);
    PROP_ERR(cbrace_err);

    return HirFnDef(*name_tok, *return_type_tok, body);
  }
};

using Hir = std::variant<HirFnDef>;
using ModulesHir = std::unordered_map<std::string, std::vector<Hir>>;
