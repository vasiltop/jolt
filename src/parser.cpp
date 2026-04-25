#include "parser.hpp"
#include "checker.hpp"
#include "tokens.hpp"
#include <format>
#include <fstream>
#include <iostream>

auto Parser::parse_path(const std::filesystem::path &path) -> ve {
  std::error_code ec0;
  const auto entry = std::filesystem::weakly_canonical(path, ec0);
  if (ec0) {
    return {Error{.msg = std::format("error: could not resolve path: {} ({})",
                                     path.string(), ec0.message())}};
  }
  parse_queue_.push(entry);
  ve errors;

  while (!parse_queue_.empty()) {
    auto p = parse_queue_.front();
    parse_queue_.pop();

    std::error_code wcc;
    const auto abs_file = std::filesystem::weakly_canonical(p, wcc);
    if (wcc) {
      errors.push_back(Error{
          .msg = std::format("error: could not resolve path: {}", p.string())});
      continue;
    }
    if (parsed_paths.contains(abs_file.string())) {
      continue;
    }

    auto data = read_entire_file(abs_file);
    if (!data) {
      errors.push_back(data.error());
      continue;
    }

    Tokenizer tokenizer(abs_file.filename().string(), std::move(*data));

    // TODO: Make this a compiler flag
    tokenizer.print_tokens();

    auto module_name_res = parse_module_name(tokenizer);
    if (!module_name_res) {
      errors.push_back(module_name_res.error());
      continue;
    }

    auto module_name = *module_name_res;

    if (modules_hir_.contains(module_name)) {
      errors.push_back(Error{.msg = "Duplicate module name: " + module_name});
      continue;
    }

    auto hir_nodes = hir(tokenizer, abs_file);
    if (!hir_nodes) {
      errors.push_back(hir_nodes.error());
      continue;
    }

    path_to_mod_[abs_file.string()] = module_name;
    modules_hir_[module_name] = std::move(*hir_nodes);
    module_sources_.insert_or_assign(
        module_name,
        ModuleSource{.path = abs_file.string(), .text = tokenizer.source()});
    parsed_paths.insert(abs_file.string());
  }

  if (errors.size()) {
    return errors;
  }

  link_import_targets(errors);
  if (errors.size()) {
    return errors;
  }

  Checker checker;
  checker.check_modules(modules_hir_, errors, module_sources_);
  return errors;
}

auto Parser::parse_module_name(Tokenizer &tokenizer)
    -> std::expected<std::string, Error> {
  auto mod = tokenizer.expect_token_and_pop(TokenKind::Module);
  PROP_ERR(mod);
  auto path = tokenizer.expect_token_and_pop(TokenKind::Ident);
  PROP_ERR(path);
  auto semi = tokenizer.expect_token_and_pop(TokenKind::Semicolon);
  PROP_ERR(semi);

  return path->text;
}

auto Parser::hir(Tokenizer &tokenizer,
                 const std::filesystem::path &current_file_path)
    -> std::expected<std::vector<Hir>, Error> {
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
    } else if (next.kind == TokenKind::Import) {
      auto import_node = HirImport::try_parse(tokenizer);
      PROP_ERR(import_node);
      resolve_hir_import(*import_node, current_file_path);
      if (!import_node->resolved_path.empty()) {
        std::error_code ec;
        std::filesystem::path rp(import_node->resolved_path);
        auto canon = std::filesystem::weakly_canonical(rp, ec);
        if (!ec) {
          parse_queue_.push(canon);
        }
      }
      nodes.emplace_back(std::move(*import_node));
    } else if (next.kind == TokenKind::Const) {
      auto cnst = HirLet::try_parse(tokenizer, true);
      PROP_ERR(cnst);
      nodes.emplace_back(std::move(*cnst));
    } else if (next.kind == TokenKind::Let) {
      auto let = HirLet::try_parse(tokenizer, false);
      PROP_ERR(let);
      nodes.emplace_back(std::move(*let));
    } else {
      auto t = tokenizer.peek();
      return std::unexpected(tokenizer.make_error(
          t.pos, std::format(
                     "unexpected token at module scope: '{}' ({}). Only `fn`, "
                     "`struct`, `enum`, `import`, `const`, and `let` are valid "
                     "here",
                     t.text, token_kind_string[static_cast<size_t>(t.kind)])));
    }
  }

  return nodes;
}

auto Parser::read_entire_file(const std::filesystem::path &path)
    -> std::expected<std::string, Error> {
  std::ifstream is{path, std::ios::ate | std::ios::binary};
  if (!is)
    return std::unexpected(Error{
        .msg = std::format("error: could not open file: {}", path.string())});

  auto size = is.tellg();
  is.seekg(0);
  std::string out(size, '\0');
  if (!is.read(out.data(), size))
    return std::unexpected(Error{
        .msg = std::format("error: could not read file: {}", path.string())});

  return out;
}

void Parser::resolve_hir_import(HirImport &imp,
                                const std::filesystem::path &importer_file) {
  if (imp.path.empty())
    return;
  std::filesystem::path rel(imp.path);
  auto full = (importer_file.parent_path() / rel).lexically_normal();
  std::error_code ec;
  auto canon = std::filesystem::weakly_canonical(full, ec);
  if (ec)
    return;
  imp.resolved_path = canon.string();
}

void Parser::link_import_targets(std::vector<Error> &errors) {
  for (auto &[name, items] : modules_hir_) {
    for (auto &node : items) {
      if (auto *imp = std::get_if<HirImport>(&node)) {
        if (imp->resolved_path.empty()) {
          errors.push_back(Error{.msg = std::format("bad import: empty path in module `{}`",
                                                    name)});
          continue;
        }
        std::error_code ec;
        const auto p = std::filesystem::weakly_canonical(imp->resolved_path, ec);
        (void)ec;
        const std::string key = p.string();
        auto it = path_to_mod_.find(key);
        if (it == path_to_mod_.end()) {
          errors.push_back(
              Error{.msg = std::format("import did not resolve to a known module: `{}`",
                                       imp->path)});
        } else {
          imp->target_module = it->second;
          imp->import_alias = it->second;
        }
      }
    }
  }
}
