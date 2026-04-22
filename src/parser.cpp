#include "parser.hpp"
#include "checker.hpp"
#include "tokens.hpp"
#include <format>
#include <fstream>
#include <iostream>

auto Parser::path_to_module_id(std::filesystem::path rel) -> std::string {
  rel = rel.lexically_normal();
  std::vector<std::string> segs;
  for (const auto &part : rel) {
    if (part == "." || part == ".." || part.empty())
      continue;
    if (part == "/")
      continue;
    segs.push_back(part.string());
  }
  if (segs.empty())
    return {};
  {
    std::string &last = segs.back();
    const auto n = last.size();
    if (n >= 5 && last.compare(n - 5, 5, ".jolt") == 0)
      last = last.substr(0, n - 5);
    else
      last = std::filesystem::path(last).stem().string();
  }
  std::string out;
  for (size_t i = 0; i < segs.size(); ++i) {
    if (i)
      out += "::";
    out += segs[i];
  }
  return out;
}

auto Parser::derive_module_id(const std::filesystem::path &abs) const
    -> std::string {
  if (!source_root_.has_value())
    return abs.stem().string();
  std::error_code ec;
  auto rel = std::filesystem::relative(abs, *source_root_, ec);
  if (ec || rel.empty())
    return abs.stem().string();
  if (rel.generic_string().find("..") != std::string::npos)
    return abs.stem().string();
  auto s = path_to_module_id(std::move(rel));
  if (s.empty())
    return abs.stem().string();
  return s;
}

auto Parser::parse_path(const std::filesystem::path &path)
    -> std::vector<Error> {
  std::error_code ec0;
  const auto entry = std::filesystem::weakly_canonical(path, ec0);
  if (ec0) {
    return {Error{
        .msg = std::format("error: could not resolve path: {} ({})",
                            path.string(), ec0.message())}};
  }
  parse_queue_.push(entry);
  std::vector<Error> errors;

  while (!parse_queue_.empty()) {
    auto p = parse_queue_.front();
    parse_queue_.pop();

    std::error_code wcc;
    const auto abs_file = std::filesystem::weakly_canonical(p, wcc);
    if (wcc) {
      errors.push_back(
          Error{.msg = std::format("error: could not resolve path: {}",
                                    p.string())});
      continue;
    }

    auto data = read_entire_file(abs_file);
    if (!data) {
      errors.push_back(data.error());
      continue;
    }

    if (!source_root_.has_value())
      source_root_ = abs_file.parent_path();

    Tokenizer tokenizer(abs_file.filename().string(), std::move(*data));

    // TODO: Make this a compiler flag
    tokenizer.print_tokens();

    const std::string module_name = derive_module_id(abs_file);

    if (module_name.empty()) {
      errors.push_back(
          Error{.msg = "internal error: could not derive module name from path; "
                       "check the file location relative to the entry file"});
      continue;
    }

    auto hir_nodes = hir(tokenizer, abs_file);
    if (!hir_nodes) {
      errors.push_back(hir_nodes.error());
      continue;
    }

    if (modules_hir_.contains(module_name)) {
      errors.push_back(
          Error{.msg = std::format(
                    "error: duplicate module name `{}` (each .jolt file must map "
                    "to a unique path under the entry directory)",
                    module_name)});
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
      for (const auto &part : import_def->path) {
        import_path /= part.text;
      }
      import_path += ".jolt";

      // Add the imported file to the queue to be parsed.
      parse_queue_.push(import_path);

      nodes.emplace_back(std::move(*import_def));
    } else if (next.kind == TokenKind::Const) {
      auto cnst = HirConst::try_parse(tokenizer);
      PROP_ERR(cnst);
      nodes.emplace_back(std::move(*cnst));
    } else if (next.kind == TokenKind::Let) {
      auto module_let = HirModuleLet::try_parse(tokenizer);
      PROP_ERR(module_let);
      nodes.emplace_back(std::move(*module_let));
    } else {
      auto t = tokenizer.peek();
      return std::unexpected(tokenizer.make_error(
          t.pos,
          std::format("unexpected token at module scope: '{}' ({}). Only `fn`, "
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
