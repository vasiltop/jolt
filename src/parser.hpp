#pragma once

#include "diagnostics.hpp"
#include "errors.hpp"
#include "hir.hpp"
#include "tokenizer.hpp"
#include <expected>
#include <filesystem>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class Parser {
public:
  auto parse_path(const std::filesystem::path &path) -> ve;
  const ModulesHir &get_modules() const { return modules_hir_; }
  auto hir(Tokenizer &tokenizer, const std::filesystem::path &current_file_path)
      -> std::expected<std::vector<Hir>, Error>;

private:
  static auto read_entire_file(const std::filesystem::path &path)
      -> std::expected<std::string, Error>;
  static auto path_to_module_id(std::filesystem::path rel) -> std::string;
  auto derive_module_id(const std::filesystem::path &absolute_file) const
      -> std::string;
  auto parse_module_name(Tokenizer &tokenizer)
      -> std::expected<std::string, Error>;
  static void resolve_hir_import(HirImport &imp,
                                 const std::filesystem::path &importer_file);
  void link_import_targets(std::vector<Error> &errors);

  std::queue<std::filesystem::path> parse_queue_;
  ModulesHir modules_hir_;
  std::unordered_map<std::string, ModuleSource> module_sources_;
  /// Canonical absolute file path -> module id (from `module` line)
  std::unordered_map<std::string, std::string> path_to_mod_;
  // Parsed paths in canonical form
  std::set<std::string> parsed_paths;
};
