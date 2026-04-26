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
  LlirOperand dest;
  LlirOperand value;
};

struct LlirLoad {
  std::string dest_reg;
  LlirOperand src;
};

struct LlirBinaryOp {
  enum {
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    And,
    Or,
    Xor,
    Shl,
    Shr,
    Eq,
    Ne,
    Lt,
    Gt,
    Le,
    Ge
  } op;
  std::string dest_reg;
  LlirOperand lhs;
  LlirOperand rhs;
};

struct LlirUnaryOp {
  enum { Neg, Not } op;
  std::string dest_reg;
  LlirOperand src;
};

struct LlirBranch {
  std::string target_branch;
};

struct LlirCondBranch {
  LlirOperand condition; // register that contains bool
  std::string true_label;
  std::string false_label;
};

struct LlirReturn {
  std::optional<LlirOperand> value;
};

struct LlirGetElement {
  std::string dest_reg;
  LlirOperand base;
  LlirOperand index;
};

struct LlirCall {
  std::string func_name;
  std::vector<LlirOperand> args;
};

struct LlirCast {
  std::string dest_reg;
  LlirOperand source;
  Type target_type;
};

using LlirInstruction =
    std::variant<LlirAlloca, LlirStore, LlirLoad, LlirBinaryOp, LlirUnaryOp,
                 LlirBranch, LlirCondBranch, LlirReturn, LlirGetElement,
                 LlirCall, LlirCast>;

struct LlirBlock {
  std::string label;
  std::vector<LlirInstruction> instructions;
};

struct LlirFunction {
  std::string name;
  Type return_type;
  std::vector<LlirBlock> blocks;
};

struct LlirGlobal {
  std::string name;
  Type type;
  LlirOperandData initial_value;
  bool is_constant;
};

auto lower_hir(const ModulesHir &modules) -> void;

struct LlirModule {
  std::string name;
  std::vector<LlirGlobal> globals;
  std::vector<LlirFunction> functions;
};
