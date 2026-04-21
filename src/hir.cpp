#include "hir.hpp"
#include "tokenizer.hpp"
#include <memory>

auto HirExprUnary::try_parse(Tokenizer &tokenizer)
    -> std::expected<HirExpr, Error> {
  auto tok = tokenizer.peek();

  if (token_is_unary_op(tok)) {
    tokenizer.consume();
    auto sub_expr = HirExprUnary::try_parse(tokenizer);
    PROP_ERR(sub_expr);

    auto unary = std::make_unique<HirExprUnary>();
    unary->op = tok;
    unary->expr = std::make_unique<HirExpr>(std::move(*sub_expr));

    return HirExpr{.item = std::move(unary)};
  }

  return parse_primary(tokenizer);
}

auto parse_primary(Tokenizer &tokenizer) -> std::expected<HirExpr, Error> {
  auto tok = tokenizer.consume();

  switch (tok.kind) {
  case TokenKind::Integer:
  case TokenKind::String:
  case TokenKind::True:
  case TokenKind::False: {
    HirExprLiteral lit{.tok = tok};
    return HirExpr{.item = lit};
  } break;
  case TokenKind::ParenOpen: {
    auto expr = HirExpr::try_parse(tokenizer);
    PROP_ERR(expr);
    auto err = tokenizer.expect_token_and_pop(TokenKind::ParenClose);
    PROP_ERR(err);
    return expr;
  } break;
  case TokenKind::Ident: {
    // This can be one of two cases
    // module_name::{struct, function, global_var, enum}
    // or we can access something in the current module implicitly if we do not
    // include the module name
    // tok will either be the module name or the name of thing we are accessing
    // in the current module

    auto module_name = tokenizer.get_module_name();
    auto access_name = tok.text;

    auto next = tokenizer.peek();
    if (next.kind == TokenKind::Colon) {
      tokenizer.consume();
      auto second_colon = tokenizer.expect_token_and_pop(TokenKind::Colon);
      PROP_ERR(second_colon);
      module_name = std::move(tok.text);
      auto name = tokenizer.expect_token_and_pop(TokenKind::Ident);
      PROP_ERR(name);
      access_name = std::move(name->text);
    }

  } break;
  case TokenKind::BraceOpen:
    break;
  default:
    return std::unexpected(Error{.msg = "TODO: unimplemented"});
  }

  return std::unexpected(Error{.msg = "TODO: unimplemented"});
}

auto HirExpr::try_parse(Tokenizer &tokenizer, int precedence)
    -> std::expected<HirExpr, Error> {
  auto unary = HirExprUnary::try_parse(tokenizer);
  PROP_ERR(unary);
  HirExprItem left = std::move(unary->item);

  for (;;) {
    auto tok = tokenizer.peek();
    auto cur_precedence = token_precedence(tok);

    if (cur_precedence < precedence) {
      break;
    }

    tokenizer.consume();

    auto right = HirExpr::try_parse(tokenizer, cur_precedence + 1);
    PROP_ERR(right);

    auto binary = std::make_unique<HirExprBinary>();
    binary->op = tok;
    binary->lhs = std::make_unique<HirExpr>(HirExpr{{}, std::move(left)});
    binary->rhs = std::make_unique<HirExpr>(std::move(*right));
    left = std::move(binary);
  }

  return HirExpr{.item = std::move(left)};
}

auto HirReturn::try_parse(Tokenizer &tokenizer)
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

auto HirLet::try_parse(Tokenizer &tokenizer) -> std::expected<HirLet, Error> {
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

auto HirStmt::try_parse(Tokenizer &tokenizer) -> std::expected<HirStmt, Error> {
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
  auto expr = HirExpr::try_parse(tokenizer);
  PROP_ERR(expr);
  tokenizer.expect_token_and_pop(TokenKind::Semicolon);
  return HirStmt{.item = HirExprStmt{.expr = std::move(*expr)}};
}

auto HirBlock::try_parse(Tokenizer &tokenizer)
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

auto HirFnDef::try_parse(Tokenizer &tokenizer)
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
    // TODO: Parse type instead of ident
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

auto HirStruct::try_parse(Tokenizer &tokenizer)
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

auto HirEnum::try_parse(Tokenizer &tokenizer) -> std::expected<HirEnum, Error> {
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
