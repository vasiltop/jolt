#pragma once

#include "checker.hpp"
#include "errors.hpp"
#include "hir.hpp"
#include "tokenizer.hpp"
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <queue>
#include <string>

class Parser {

public:
  auto parse_path(const std::filesystem::path &path) -> Errors {
    auto canonical = std::filesystem::canonical(path);
    parse_queue_.push(canonical);
    Errors errors;

    while (!parse_queue_.empty()) {
      auto p = parse_queue_.front();
      parse_queue_.pop();

      auto data = read_entire_file(p);
      if (!data) {
        errors.push_back(data.error());
        continue;
      }

      Tokenizer tokenizer(canonical.filename(), std::move(*data));

      auto module_name = parse_module_name(tokenizer);

      if (!module_name) {
				errors.push_back(module_name.error());
        continue;
      }

      auto hir_nodes = hir(tokenizer);
			if (!hir_nodes) {
				errors.push_back(hir_nodes.error());
				continue;
			}

      modules_hir_[*module_name] = *hir_nodes;
    }

		if (errors.size()) {
			return errors;
		}

    Checker checker;
    auto success = checker.check_modules(modules_hir_);
    if (!success.has_value()) {
      // TODO: Propagate these errors.
    }

    return errors;
  }

  const ModulesHir &get_modules() const { return modules_hir_; }

  auto hir(Tokenizer &tokenizer)
      -> std::expected<std::vector<Hir>, Error<TokenizerError>> {
    std::vector<Hir> nodes;

    // TODO: Maybe we should create a peek instead so the first token is not
    // consumed.
    for (auto tok = tokenizer.consume(); tok.kind != TokenKind::Eof;
         tok = tokenizer.consume()) {
      if (tok.kind == TokenKind::Fn) {
        auto fn = HirFnDef::try_parse(tokenizer);
        PROP_ERR(fn);
        nodes.emplace_back(std::move(*fn));
      }
    }

    return nodes;
  }

private:
  auto parse_module_name(Tokenizer &tokenizer)
      -> std::expected<std::string, Error<TokenizerError>> {
    auto mod = tokenizer.expect_token_and_pop(TokenKind::Module);
    PROP_ERR(mod);

    auto module_name_tok = tokenizer.expect_token_and_pop(TokenKind::Ident);
    PROP_ERR(module_name_tok);

    auto semi = tokenizer.expect_token_and_pop(TokenKind::Semicolon);
    PROP_ERR(semi);

    return (*module_name_tok).text;
  }

  static auto read_entire_file(const std::filesystem::path &path)
      -> std::expected<std::string, Error<ParserError>> {
    std::ifstream is{path, std::ios::ate | std::ios::binary};
    if (!is)
      return std::unexpected(
          Error<ParserError>{.kind = ParserError::FileNotFound});

    auto size = is.tellg();
    is.seekg(0);

    std::string out(size, '\0');
    if (!is.read(out.data(), size))
      return std::unexpected(
          Error<ParserError>{.kind = ParserError::ReadError});

    return out;
  }

  std::queue<std::filesystem::path> parse_queue_;
  ModulesHir modules_hir_;
};
