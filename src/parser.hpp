#pragma once

#include "checker.hpp"
#include "errors.hpp"
#include "hir.hpp"
#include "tokenizer.hpp"
#include <expected>
#include <filesystem>
#include <queue>
#include <string>
#include <vector>

class Parser {
public:
  auto parse_path(const std::filesystem::path &path) -> std::vector<Error>;
  const ModulesHir &get_modules() const { return modules_hir_; }
  auto hir(Tokenizer &tokenizer, const std::filesystem::path& current_file_path) -> std::expected<std::vector<Hir>, Error>;

private:
  static auto read_entire_file(const std::filesystem::path &path)
      -> std::expected<std::string, Error>;
  static auto path_to_module_id(std::filesystem::path rel) -> std::string;
  auto derive_module_id(const std::filesystem::path &absolute_file) const
      -> std::string;

  std::queue<std::filesystem::path> parse_queue_;
  ModulesHir modules_hir_;
  /// Directory used to turn paths into `a::b::c` (parent of the entry file);
  /// set on the first parsed file and reused for imports.
  std::optional<std::filesystem::path> source_root_;
};
