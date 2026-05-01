#pragma once

#include "hir.hpp"
#include "types.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

using LlirOperandData = std::variant<std::string, int64_t, double, char, bool>;

struct LlirOperand {
  enum { Local, Register, Global, Literal } kind;
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

struct LlirAddrOf {
  std::string dest_reg;
  std::string local_slot;
};

struct LlirIndexedStore {
  LlirOperand array_slot;
  LlirOperand index;
  LlirOperand value;
};

struct LlirFieldLoad {
  std::string dest_reg;
  LlirOperand object;
  std::string field;
};

struct LlirFieldStore {
  LlirOperand object;
  std::string field;
  LlirOperand value;
};

/// Address of a struct field (`&agg.field`). `object` refers to aggregate storage (Local slot).
struct LlirFieldAddr {
  std::string dest_reg;
  LlirOperand object;
  std::string field;
};

struct LlirBinaryOp {
  enum Kind {
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
  };
  Kind op;
  std::string dest_reg;
  LlirOperand lhs;
  LlirOperand rhs;
};

struct LlirUnaryOp {
  enum Kind { Neg, Not };
  Kind op;
  std::string dest_reg;
  LlirOperand src;
};

struct LlirBranch {
  std::string target_branch;
};

struct LlirCondBranch {
  LlirOperand condition;
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
  std::optional<std::string> dest_reg;
};

struct LlirCast {
  std::string dest_reg;
  LlirOperand source;
  Type target_type;
};

using LlirInstruction =
    std::variant<LlirAlloca, LlirStore, LlirLoad, LlirAddrOf, LlirIndexedStore,
                 LlirFieldLoad, LlirFieldStore, LlirFieldAddr, LlirBinaryOp,
                 LlirUnaryOp,
                 LlirBranch, LlirCondBranch, LlirReturn, LlirGetElement,
                 LlirCall, LlirCast>;

struct LlirBlock {
  std::string label;
  std::vector<LlirInstruction> instructions;
};

struct LlirParam {
  std::string name;
  /// Human-readable signature fragment (HIR type text or semantic print).
  std::string type_display;
};

struct LlirFunction {
  std::string name;
  Type return_type;
  std::vector<LlirParam> params;
  std::vector<LlirBlock> blocks;
};

struct LlirGlobal {
  std::string name;
  Type type;
  LlirOperandData initial_value;
  bool is_constant;
};

struct LlirModule {
  std::vector<LlirGlobal> globals;
  std::vector<LlirFunction> functions;
};

auto lower_hir(const ModulesHir &modules) -> LlirModule;
