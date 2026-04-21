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
  auto parse_path(const std::filesystem::path &path) -> std::vector<Error> {
    auto canonical = std::filesystem::canonical(path);
    parse_queue_.push(canonical);
    std::vector<Error> errors;

    while (!parse_queue_.empty()) {
      auto p = parse_queue_.front();
      parse_queue_.pop();

      auto data = read_entire_file(p);
      if (!data) {
        errors.push_back(data.error());
        continue;
      }

      Tokenizer tokenizer(canonical.filename(), std::move(*data));

      // TODO: Make this a compiler flag
#if false
				tokenizer.print_tokens();
#endif

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

      modules_hir_[*module_name] = std::move(*hir_nodes);
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

  auto hir(Tokenizer &tokenizer) -> std::expected<std::vector<Hir>, Error> {
    std::vector<Hir> nodes;

    while (tokenizer.peek().kind != TokenKind::Eof) {
      auto next = tokenizer.peek();
      if (next.kind == TokenKind::Fn) {
        auto fn = HirFnDef::try_parse(tokenizer);
        PROP_ERR(fn);
        nodes.emplace_back(std::move(*fn));
      } else if (next.kind == TokenKind::Struct) {
        auto struct_def = HirStruct::try_parse(tokenizer);
        PROP_ERR(struct_def);
        nodes.emplace_back(std::move(*struct_def));
      } else if (next.kind == TokenKind::Enum) {
        auto enum_def = HirEnum::try_parse(tokenizer);
        PROP_ERR(enum_def);
        nodes.emplace_back(std::move(*enum_def));
      } else {
        // Skip unhandled top-level tokens for now
        tokenizer.consume();
      }
    }

    return nodes;
  }

private:
  auto parse_module_name(Tokenizer &tokenizer)
      -> std::expected<std::string, Error> {
    auto mod = tokenizer.expect_token_and_pop(TokenKind::Module);
    PROP_ERR(mod);

    auto module_name_tok = tokenizer.expect_token_and_pop(TokenKind::Ident);
    PROP_ERR(module_name_tok);

    auto semi = tokenizer.expect_token_and_pop(TokenKind::Semicolon);
    PROP_ERR(semi);

    return (*module_name_tok).text;
  }

  static auto read_entire_file(const std::filesystem::path &path)
      -> std::expected<std::string, Error> {
    std::ifstream is{path, std::ios::ate | std::ios::binary};
    if (!is)
      return std::unexpected(Error{.msg = "File not found"});

    auto size = is.tellg();
    is.seekg(0);
    std::string out(size, '\0');
    if (!is.read(out.data(), size))
      return std::unexpected(Error{.msg = "Read error"});

    return out;
  }

  std::queue<std::filesystem::path> parse_queue_;
  ModulesHir modules_hir_;
};
