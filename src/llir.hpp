#include "hir.hpp"
#include "types.hpp"
#include <cstdint>

using LlirOperandData = std::variant<std::string, int64_t, double, char, bool>;

struct LlirOperand {
  enum { Local, Register, Literal } kind;
  // If this is a Local variable or Register identifier, 'data' will store the
  // name. Otherwise, it will store the literal value.
  LlirOperandData data;
  Type type;
};

struct LlirAlloca {
  std::string var_name;
  Type type;
};

struct LlirStore {
  std::string var_name;
  LlirOperand op;
};

struct LlirLoad {
  std::string dest_reg;
  std::string src_var;
};

using LlirInstruction = std::variant<LlirAlloca, LlirStore, LlirLoad>;

struct LlirBlock {
  std::string label;
  std::vector<LlirInstruction> instructions;
};

struct LlirFunction {
  std::string name;
  Type return_type;
  std::vector<LlirBlock> blocks;
};

auto lower_hir(const ModulesHir &modules) -> void;

using Llir = std::variant<LlirFunction>;
