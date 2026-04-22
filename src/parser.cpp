#include "parser.hpp"
#include "checker.hpp"
#include <fstream>
#include <iostream>

auto Parser::parse_path(const std::filesystem::path &path)
    -> std::vector<Error> {
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
    tokenizer.print_tokens();
    auto res = tokenizer.parse_module_name();

    if (!res) {
      errors.push_back(res.error());
      continue;
    }

    auto hir_nodes = hir(tokenizer, p);
    if (!hir_nodes) {
      errors.push_back(hir_nodes.error());
      continue;
    }

    auto module_name = tokenizer.get_module_name();

    if (modules_hir_.contains(module_name)) {
      // TODO: Improve this error message.
      errors.emplace_back("Module name already exists");
      continue;
    }

    modules_hir_[module_name].items = std::move(*hir_nodes);
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

auto Parser::hir(Tokenizer &tokenizer, const std::filesystem::path& current_file_path)
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
      auto import_def = HirImport::try_parse(tokenizer);
      PROP_ERR(import_def);
      
      // Calculate path to imported module.
      // E.g., `import math::geometry;` -> math/geometry.jolt
      std::filesystem::path import_path = current_file_path.parent_path();
      for (const auto& part : import_def->path) {
          import_path /= part.text;
      }
      import_path += ".jolt";
      
      // Add the imported file to the queue to be parsed.
      parse_queue_.push(import_path);
      
      nodes.emplace_back(std::move(*import_def));
    } else {
      // Skip unhandled top-level tokens for now
      tokenizer.consume();
    }
  }

  return nodes;
}

auto Parser::read_entire_file(const std::filesystem::path &path)
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
