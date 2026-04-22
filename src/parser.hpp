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

  std::queue<std::filesystem::path> parse_queue_;
  ModulesHir modules_hir_;
};
