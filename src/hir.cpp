#include "hir.hpp"
#include "tokenizer.hpp"
#include <format>
#include <memory>

auto HirType::to_string() const -> std::string {
  return std::visit(
      [](auto &&arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, HirTypePath>) {
          if (arg.module)
            return std::format("{}:{}", arg.module->text, arg.name.text);
          return arg.name.text;
        } else if constexpr (std::is_same_v<T, std::unique_ptr<HirTypePtr>>) {
          return "*" + arg->base->to_string();
        } else {
          return "<unknown_type>";
        }
      },
      item);
}

auto HirType::try_parse(Tokenizer &tokenizer) -> std::expected<HirType, Error> {
  if (tokenizer.peek().kind == TokenKind::Mul) {
    tokenizer.consume();
    auto base = HirType::try_parse(tokenizer);
    PROP_ERR(base);
    auto ptr = std::make_unique<HirTypePtr>();
    ptr->base = std::make_unique<HirType>(std::move(*base));
    return HirType{.item = std::move(ptr)};
  }

  auto first = tokenizer.expect_token_and_pop(TokenKind::Ident);
  PROP_ERR(first);

  if (tokenizer.peek().kind == TokenKind::Colon) {
    const auto ck = tokenizer.checkpoint();
    tokenizer.consume();
    if (tokenizer.peek().kind == TokenKind::Colon) {
      tokenizer.restore(ck);
    } else {
      auto second = tokenizer.expect_token_and_pop(TokenKind::Ident);
      PROP_ERR(second);
      if (tokenizer.peek().kind == TokenKind::LessThan) {
        return std::unexpected(tokenizer.make_error(
            tokenizer.peek().pos,
            "generics are not supported (unexpected `<` in type)"));
      }
      return HirType{.item = HirTypePath{.module = *first, .name = *second}};
    }
  }

  if (tokenizer.peek().kind == TokenKind::LessThan) {
    return std::unexpected(tokenizer.make_error(
        tokenizer.peek().pos,
        "generics are not supported (unexpected `<` in type)"));
  }

  return HirType{
      .item = HirTypePath{.module = std::nullopt, .name = *first},
  };
}

auto parse_postfix(Tokenizer &tokenizer, bool allow_struct_literal_after_path)
    -> std::expected<HirExpr, Error> {
  auto expr = parse_primary(tokenizer, allow_struct_literal_after_path);
  PROP_ERR(expr);

  for (;;) {
    auto next = tokenizer.peek();
    if (next.kind == TokenKind::Colon) {
      tokenizer.consume();
      if (tokenizer.peek().kind != TokenKind::Colon) {
        return std::unexpected(tokenizer.make_error(
            tokenizer.peek().pos,
            "expected `::` (enum variant uses `::`, module uses a single `:`)"));
      }
      tokenizer.consume();
      auto variant = tokenizer.expect_token_and_pop(TokenKind::Ident);
      PROP_ERR(variant);
      auto *path = std::get_if<HirExprPath>(&(*expr).item);
      if (!path) {
        return std::unexpected(tokenizer.make_error(
            variant->pos, "expected a name (or `mod:Name`) before `::`"));
      }
      HirExprEnumVariant ev;
      ev.module = path->module;
      ev.enum_name = path->name;
      ev.variant = *variant;
      expr = HirExpr{.item = std::move(ev)};
    } else if (next.kind == TokenKind::Dot) {
      tokenizer.consume();
      auto member = tokenizer.expect_token_and_pop(TokenKind::Ident);
      PROP_ERR(member);

      auto access = std::make_unique<HirExprMember>();
      access->object = std::make_unique<HirExpr>(std::move(*expr));
      access->member = *member;
      expr = HirExpr{.item = std::move(access)};
    } else if (next.kind == TokenKind::BracketOpen) {
      tokenizer.consume();
      auto index_expr = HirExpr::try_parse(tokenizer);
      PROP_ERR(index_expr);
      tokenizer.expect_token_and_pop(TokenKind::BracketClose);

      auto access = std::make_unique<HirExprIndex>();
      access->value = std::make_unique<HirExpr>(std::move(*expr));
      access->index = std::make_unique<HirExpr>(std::move(*index_expr));
      expr = HirExpr{.item = std::move(access)};
    } else if (next.kind == TokenKind::ParenOpen) {
      tokenizer.consume();
      auto call = std::make_unique<HirExprCall>();
      call->callee = std::make_unique<HirExpr>(std::move(*expr));

      while (tokenizer.peek().kind != TokenKind::ParenClose &&
             tokenizer.peek().kind != TokenKind::Eof) {
        auto arg = HirExpr::try_parse(tokenizer);
        PROP_ERR(arg);
        call->args.push_back(std::move(*arg));

        if (tokenizer.peek().kind == TokenKind::Comma) {
          tokenizer.consume();
        }
      }
      tokenizer.expect_token_and_pop(TokenKind::ParenClose);

      expr = HirExpr{.item = std::move(call)};
    } else {
      break;
    }
  }
  return expr;
}

auto HirExprUnary::try_parse(Tokenizer &tokenizer,
                             bool allow_struct_literal_after_path)
    -> std::expected<HirExpr, Error> {
  auto tok = tokenizer.peek();

  if (token_is_unary_op(tok)) {
    tokenizer.consume();
    auto sub_expr =
        HirExprUnary::try_parse(tokenizer, allow_struct_literal_after_path);
    PROP_ERR(sub_expr);

    auto unary = std::make_unique<HirExprUnary>();
    unary->op = tok;
    unary->expr = std::make_unique<HirExpr>(std::move(*sub_expr));

    return HirExpr{.item = std::move(unary)};
  }

  return parse_postfix(tokenizer, allow_struct_literal_after_path);
}

auto parse_primary(Tokenizer &tokenizer, bool allow_struct_literal_after_path)
    -> std::expected<HirExpr, Error> {
  auto tok = tokenizer.consume();

  switch (tok.kind) {
  case TokenKind::Integer:
  case TokenKind::String:
  case TokenKind::Char:
  case TokenKind::Real:
  case TokenKind::Null:
  case TokenKind::True:
  case TokenKind::False: {
    HirExprLiteral lit{.tok = tok};
    return HirExpr{.item = std::move(lit)};
  } break;
  case TokenKind::ParenOpen: {
    auto expr = HirExpr::try_parse(tokenizer);
    PROP_ERR(expr);
    auto err = tokenizer.expect_token_and_pop(TokenKind::ParenClose);
    PROP_ERR(err);
    return expr;
  } break;
  case TokenKind::Ident: {
    std::optional<Token> mod;
    Token name = tok;
    if (tokenizer.peek().kind == TokenKind::Colon) {
      const auto ck = tokenizer.checkpoint();
      tokenizer.consume();
      if (tokenizer.peek().kind == TokenKind::Colon) {
        tokenizer.restore(ck);
        // `::` (enum variant) is handled in `parse_postfix`, not as `mod:`.
      } else {
        mod = name;
        auto sym = tokenizer.expect_token_and_pop(TokenKind::Ident);
        PROP_ERR(sym);
        name = *sym;
      }
    }

    // Struct init: `T {` or `mod:Type {` …
    if (allow_struct_literal_after_path &&
        tokenizer.peek().kind == TokenKind::BraceOpen) {
      tokenizer.consume();
      std::vector<StructExprField> fields;

      while (tokenizer.peek().kind != TokenKind::BraceClose &&
             tokenizer.peek().kind != TokenKind::Eof) {

        auto field_name = tokenizer.expect_token_and_pop(TokenKind::Ident);
        PROP_ERR(field_name);

        tokenizer.expect_token_and_pop(TokenKind::Colon);

        auto field_val = HirExpr::try_parse(tokenizer);
        PROP_ERR(field_val);

        fields.emplace_back(*field_name,
                            std::make_unique<HirExpr>(std::move(*field_val)));

        if (tokenizer.peek().kind == TokenKind::Comma) {
          tokenizer.consume();
        } else if (tokenizer.peek().kind == TokenKind::Semicolon) {
          tokenizer.consume();
        } else {
          break;
        }
      }
      tokenizer.expect_token_and_pop(TokenKind::BraceClose);

      auto struct_expr = std::make_unique<HirExprStruct>();
      struct_expr->hir_type =
          HirType{.item = HirTypePath{.module = std::move(mod), .name = name}};
      struct_expr->fields = std::move(fields);
      return HirExpr{.item = std::move(struct_expr)};
    }

    return HirExpr{
        .item = HirExprPath{.module = std::move(mod), .name = name},
    };
  } break;
  case TokenKind::BracketOpen: {
    std::vector<HirExpr> elements;
    while (tokenizer.peek().kind != TokenKind::BracketClose &&
           tokenizer.peek().kind != TokenKind::Eof) {
      auto element = HirExpr::try_parse(tokenizer);
      PROP_ERR(element);
      elements.push_back(std::move(*element));

      if (tokenizer.peek().kind == TokenKind::Comma) {
        tokenizer.consume();
      } else {
        break;
      }
    }
    tokenizer.expect_token_and_pop(TokenKind::BracketClose);

    auto array_expr = std::make_unique<HirExprArray>();
    array_expr->elements = std::move(elements);
    return HirExpr{.item = std::move(array_expr)};
  } break;
  case TokenKind::BraceOpen:
    break;
  default:
    std::string type(token_kind_string[static_cast<size_t>(tok.kind)]);
    return std::unexpected(tokenizer.make_error(
        tok.pos, std::format("expression syntax not implemented for `{}` ({})",
                             tok.text, type)));
  }

  return std::unexpected(
      tokenizer.make_error(tok.pos, "invalid primary expression"));
}

auto HirExpr::try_parse(Tokenizer &tokenizer, int precedence,
                        bool allow_struct_literal_after_path)
    -> std::expected<HirExpr, Error> {
  auto unary =
      HirExprUnary::try_parse(tokenizer, allow_struct_literal_after_path);
  PROP_ERR(unary);
  HirExprItem left = std::move(unary->item);

  for (;;) {
    auto tok = tokenizer.peek();
    auto cur_precedence = token_precedence(tok);

    if (cur_precedence < precedence) {
      break;
    }

    tokenizer.consume();

    if (tok.kind == TokenKind::As) {
      auto type_tok = HirType::try_parse(tokenizer);
      PROP_ERR(type_tok);

      auto as_expr = std::make_unique<HirExprAs>();
      as_expr->expr = std::make_unique<HirExpr>(HirExpr{{}, std::move(left)});
      as_expr->hir_type = std::move(*type_tok);
      left = std::move(as_expr);
    } else {
      auto right = HirExpr::try_parse(tokenizer, cur_precedence + 1);
      PROP_ERR(right);

      auto binary = std::make_unique<HirExprBinary>();
      binary->op = tok;
      binary->lhs = std::make_unique<HirExpr>(HirExpr{{}, std::move(left)});
      binary->rhs = std::make_unique<HirExpr>(std::move(*right));
      left = std::move(binary);
    }
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

auto HirBreak::try_parse(Tokenizer &tokenizer)
    -> std::expected<HirBreak, Error> {
  auto kw = tokenizer.expect_token_and_pop(TokenKind::Break);
  PROP_ERR(kw);
  auto semi = tokenizer.expect_token_and_pop(TokenKind::Semicolon);
  PROP_ERR(semi);
  return HirBreak{};
}

auto HirContinue::try_parse(Tokenizer &tokenizer)
    -> std::expected<HirContinue, Error> {
  auto kw = tokenizer.expect_token_and_pop(TokenKind::Continue);
  PROP_ERR(kw);
  auto semi = tokenizer.expect_token_and_pop(TokenKind::Semicolon);
  PROP_ERR(semi);
  return HirContinue{};
}

auto HirLet::try_parse(Tokenizer &tokenizer, bool is_const)
    -> std::expected<HirLet, Error> {
  tokenizer.expect_token_and_pop(is_const ? TokenKind::Const : TokenKind::Let);
  auto name = tokenizer.expect_token_and_pop(TokenKind::Ident);
  PROP_ERR(name);

  std::optional<HirType> explicit_type;
  if (tokenizer.peek().kind == TokenKind::Colon) {
    tokenizer.expect_token_and_pop(TokenKind::Colon);
    auto type_tok = HirType::try_parse(tokenizer);
    PROP_ERR(type_tok);
    explicit_type = std::move(*type_tok);
  }

  std::optional<HirExpr> initializer;
  if (is_const) {
    tokenizer.expect_token_and_pop(TokenKind::Assign);
    auto expr = HirExpr::try_parse(tokenizer);
    PROP_ERR(expr);
    initializer = std::move(*expr);
  } else if (tokenizer.peek().kind == TokenKind::Assign) {
    tokenizer.consume();
    auto expr = HirExpr::try_parse(tokenizer);
    PROP_ERR(expr);
    initializer = std::move(*expr);
  }

  auto semicolon = tokenizer.expect_token_and_pop(TokenKind::Semicolon);
  PROP_ERR(semicolon);

  return HirLet{.is_const = is_const,
                .name = *name,
                .explicit_type = std::move(explicit_type),
                .initializer = std::move(initializer)};
}

auto HirStmt::try_parse(Tokenizer &tokenizer) -> std::expected<HirStmt, Error> {
  auto next = tokenizer.peek();
  if (next.kind == TokenKind::Ret) {
    auto ret = HirReturn::try_parse(tokenizer);
    PROP_ERR(ret);
    return HirStmt{.item = std::move(*ret)};
  } else if (next.kind == TokenKind::Break) {
    auto brk = HirBreak::try_parse(tokenizer);
    PROP_ERR(brk);
    return HirStmt{.item = std::move(*brk)};
  } else if (next.kind == TokenKind::Continue) {
    auto cont = HirContinue::try_parse(tokenizer);
    PROP_ERR(cont);
    return HirStmt{.item = std::move(*cont)};
  } else if (next.kind == TokenKind::Let) {
    auto let = HirLet::try_parse(tokenizer, false);
    PROP_ERR(let);
    return HirStmt{.item = std::move(*let)};
  } else if (next.kind == TokenKind::Const) {
    auto cnst = HirLet::try_parse(tokenizer, true);
    PROP_ERR(cnst);
    return HirStmt{.item = std::move(*cnst)};
  } else if (next.kind == TokenKind::If) {
    auto if_stmt = HirIf::try_parse(tokenizer);
    PROP_ERR(if_stmt);
    return HirStmt{.item = std::move(*if_stmt)};
  } else if (next.kind == TokenKind::While) {
    auto while_stmt = HirWhile::try_parse(tokenizer);
    PROP_ERR(while_stmt);
    return HirStmt{.item = std::move(*while_stmt)};
  } else if (next.kind == TokenKind::For) {
    auto for_stmt = HirFor::try_parse(tokenizer);
    PROP_ERR(for_stmt);
    return HirStmt{.item = std::move(*for_stmt)};
  }

  auto expr = HirExpr::try_parse(tokenizer);
  PROP_ERR(expr);

  if (tokenizer.peek().kind == TokenKind::Assign) {
    tokenizer.consume();
    auto rhs = HirExpr::try_parse(tokenizer);
    PROP_ERR(rhs);
    tokenizer.expect_token_and_pop(TokenKind::Semicolon);
    return HirStmt{.item = HirAssign{.lvalue = std::move(*expr),
                                     .rvalue = std::move(*rhs)}};
  }

  tokenizer.expect_token_and_pop(TokenKind::Semicolon);
  return HirStmt{.item = HirExprStmt{.expr = std::move(*expr)}};
}

auto HirIf::try_parse(Tokenizer &tokenizer)
    -> std::expected<std::unique_ptr<HirIf>, Error> {
  tokenizer.expect_token_and_pop(TokenKind::If);

  auto cond = HirExpr::try_parse(tokenizer, 0, false);
  PROP_ERR(cond);

  auto then_block = HirBlock::try_parse(tokenizer);
  PROP_ERR(then_block);

  std::optional<HirBlock> else_block;
  if (tokenizer.peek().kind == TokenKind::Else) {
    tokenizer.consume();
    if (tokenizer.peek().kind == TokenKind::If) {
      // Handle `else if` by wrapping it in a block manually
      auto elif_stmt = HirIf::try_parse(tokenizer);
      PROP_ERR(elif_stmt);

      HirBlock blk;
      blk.stmts.push_back(HirStmt{.item = std::move(*elif_stmt)});
      else_block = std::move(blk);
    } else {
      auto eb = HirBlock::try_parse(tokenizer);
      PROP_ERR(eb);
      else_block = std::move(*eb);
    }
  }

  auto stmt = std::make_unique<HirIf>();
  stmt->condition = std::move(*cond);
  stmt->then_block = std::move(*then_block);
  stmt->else_block = std::move(else_block);

  return stmt;
}

auto HirWhile::try_parse(Tokenizer &tokenizer)
    -> std::expected<std::unique_ptr<HirWhile>, Error> {
  tokenizer.expect_token_and_pop(TokenKind::While);

  auto cond = HirExpr::try_parse(tokenizer, 0, false);
  PROP_ERR(cond);

  auto block = HirBlock::try_parse(tokenizer);
  PROP_ERR(block);

  auto stmt = std::make_unique<HirWhile>();
  stmt->condition = std::move(*cond);
  stmt->block = std::move(*block);

  return stmt;
}

auto HirFor::try_parse(Tokenizer &tokenizer)
    -> std::expected<std::unique_ptr<HirFor>, Error> {
  tokenizer.expect_token_and_pop(TokenKind::For);

  auto init = HirStmt::try_parse(tokenizer);
  PROP_ERR(init);

  auto cond = HirExpr::try_parse(tokenizer, 0, false);
  PROP_ERR(cond);

  tokenizer.expect_token_and_pop(TokenKind::Semicolon);

  auto update_lhs = HirExpr::try_parse(tokenizer);
  PROP_ERR(update_lhs);

  HirStmt update_stmt;
  if (tokenizer.peek().kind == TokenKind::Assign) {
    tokenizer.consume();
    auto rhs = HirExpr::try_parse(tokenizer);
    PROP_ERR(rhs);
    update_stmt = HirStmt{.item = HirAssign{.lvalue = std::move(*update_lhs),
                                            .rvalue = std::move(*rhs)}};
  } else {
    update_stmt = HirStmt{.item = HirExprStmt{.expr = std::move(*update_lhs)}};
  }

  auto block = HirBlock::try_parse(tokenizer);
  PROP_ERR(block);

  auto stmt = std::make_unique<HirFor>();
  stmt->init = std::make_unique<HirStmt>(std::move(*init));
  stmt->condition = std::move(*cond);
  stmt->update = std::make_unique<HirStmt>(std::move(update_stmt));
  stmt->block = std::move(*block);

  return stmt;
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

auto HirFnDef::try_parse(Tokenizer &tokenizer, bool is_extern)
    -> std::expected<HirFnDef, Error> {
  tokenizer.expect_token_and_pop(TokenKind::Fn);
  auto name_tok = tokenizer.expect_token_and_pop(TokenKind::Ident);
  PROP_ERR(name_tok);

  tokenizer.expect_token_and_pop(TokenKind::ParenOpen);

  std::vector<HirTypedIdent> params;
  bool is_variadic = false;
  while (tokenizer.peek().kind != TokenKind::ParenClose) {
    if (tokenizer.peek().kind == TokenKind::DotDotDot) {
      tokenizer.consume();
      is_variadic = true;
      break;
    }
    
    auto arg_name_tok = tokenizer.expect_token_and_pop(TokenKind::Ident);
    PROP_ERR(arg_name_tok);

    tokenizer.expect_token_and_pop(TokenKind::Colon);
    auto arg_type_tok = HirType::try_parse(tokenizer);
    PROP_ERR(arg_type_tok);

    params.emplace_back(*arg_name_tok, std::move(*arg_type_tok));

    if (tokenizer.peek().kind == TokenKind::Comma) {
      tokenizer.consume();
    }
  }

  tokenizer.expect_token_and_pop(TokenKind::ParenClose);

  std::optional<HirType> return_type_tok;
  if (tokenizer.peek().kind != TokenKind::BraceOpen && tokenizer.peek().kind != TokenKind::Semicolon) {
    auto rt = HirType::try_parse(tokenizer);
    PROP_ERR(rt);
    return_type_tok = std::move(*rt);
  }

  std::optional<HirBlock> body;
  if (is_extern) {
    tokenizer.expect_token_and_pop(TokenKind::Semicolon);
  } else {
    auto parsed_body = HirBlock::try_parse(tokenizer);
    PROP_ERR(parsed_body);
    body = std::move(*parsed_body);
  }

  return HirFnDef{.name = *name_tok,
                  .is_extern = is_extern,
                  .is_variadic = is_variadic,
                  .params = std::move(params),
                  .return_type = std::move(return_type_tok),
                  .block = std::move(body)};
}

auto HirStruct::try_parse(Tokenizer &tokenizer)
    -> std::expected<HirStruct, Error> {
  tokenizer.expect_token_and_pop(TokenKind::Struct);
  auto name_tok = tokenizer.expect_token_and_pop(TokenKind::Ident);
  PROP_ERR(name_tok);

  tokenizer.expect_token_and_pop(TokenKind::BraceOpen);
  std::vector<HirTypedIdent> fields;
  while (tokenizer.peek().kind != TokenKind::BraceClose) {
    auto field_name = tokenizer.expect_token_and_pop(TokenKind::Ident);
    PROP_ERR(field_name);
    tokenizer.expect_token_and_pop(TokenKind::Colon);
    auto field_type = HirType::try_parse(tokenizer);
    PROP_ERR(field_type);
    fields.emplace_back(*field_name, std::move(*field_type));
    tokenizer.expect_token_and_pop(TokenKind::Semicolon);
  }
  tokenizer.expect_token_and_pop(TokenKind::BraceClose);
  if (tokenizer.peek().kind == TokenKind::Semicolon)
    tokenizer.consume(); // Optional trailing ;

  return HirStruct{.name = *name_tok, .fields = std::move(fields)};
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

static auto strip_string_quotes(std::string s) -> std::string {
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
    s = s.substr(1, s.size() - 2);
  }
  return s;
}

auto HirImport::try_parse(Tokenizer &tokenizer)
    -> std::expected<HirImport, Error> {
  auto imp = tokenizer.expect_token_and_pop(TokenKind::Import);
  PROP_ERR(imp);

  auto path_tok = tokenizer.expect_token_and_pop(TokenKind::String);
  PROP_ERR(path_tok);

  auto semi = tokenizer.expect_token_and_pop(TokenKind::Semicolon);
  PROP_ERR(semi);

  return HirImport{.path = strip_string_quotes(std::string(path_tok->text)),
                   .path_pos = path_tok->pos};
}
