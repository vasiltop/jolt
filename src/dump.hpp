#pragma once

#include "hir.hpp"
#include "llir.hpp"
#include <iostream>

inline void print_indent(int indent) {
  for (int i = 0; i < indent; ++i)
    std::cout << "  ";
}

template <class... Ts> struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

inline void print_hir(const Hir &hir, int indent = 0);
inline void print_hir(const HirStmt &stmt, int indent);
inline void print_hir(const HirExpr &expr, int indent);

inline void print_hir(const HirExprLiteral &literal, int indent) {
  print_indent(indent);
  std::cout << "Literal: " << literal.tok.text << " ["
            << type_to_string(literal.type) << "]\n";
}

inline void print_hir(const HirExprBinary &binary, int indent) {
  print_indent(indent);
  std::cout << "BinaryOp: " << binary.op.text << " ["
            << type_to_string(binary.type) << "]\n";
  print_hir(*binary.lhs, indent + 1);
  print_hir(*binary.rhs, indent + 1);
}

inline void print_hir(const HirExprUnary &unary, int indent) {
  print_indent(indent);
  std::cout << "UnaryOp: " << unary.op.text << " ["
            << type_to_string(unary.type) << "]\n";
  print_hir(*unary.expr, indent + 1);
}

inline void print_hir(const HirExprPath &path_expr, int indent) {
  print_indent(indent);
  std::cout << "Path: ";
  if (path_expr.module) {
    std::cout << path_expr.module->text << ":" << path_expr.name.text;
  } else
    std::cout << path_expr.name.text;
  std::cout << " [" << type_to_string(path_expr.type) << "]\n";
}

inline void print_hir(const HirExprEnumVariant &ev, int indent) {
  print_indent(indent);
  std::cout << "EnumVariant: ";
  if (ev.module)
    std::cout << ev.module->text << ":";
  std::cout << ev.enum_name.text << "::" << ev.variant.text << " ["
            << type_to_string(ev.type) << "]\n";
}

inline void print_hir(const HirExprCall &call, int indent) {
  print_indent(indent);
  std::cout << "Call [" << type_to_string(call.type) << "]\n";
  print_indent(indent + 1);
  std::cout << "Callee:\n";
  print_hir(*call.callee, indent + 2);
  print_indent(indent + 1);
  std::cout << "Args:\n";
  for (const auto &arg : call.args) {
    print_hir(arg, indent + 2);
  }
}

inline void print_hir(const HirExprIndex &index, int indent) {
  print_indent(indent);
  std::cout << "Index [" << type_to_string(index.type) << "]\n";
  print_indent(indent + 1);
  std::cout << "Value:\n";
  print_hir(*index.value, indent + 2);
  print_indent(indent + 1);
  std::cout << "IndexExpr:\n";
  print_hir(*index.index, indent + 2);
}

inline void print_hir(const HirExprMember &member, int indent) {
  print_indent(indent);
  std::cout << "Member [" << type_to_string(member.type) << "]\n";
  print_indent(indent + 1);
  std::cout << "Object:\n";
  print_hir(*member.object, indent + 2);
  print_indent(indent + 1);
  std::cout << "Field: " << member.member.text << "\n";
}

inline void print_hir(const HirExprAs &as_expr, int indent) {
  print_indent(indent);
  std::cout << "As [" << type_to_string(as_expr.HirBase::type) << "]\n";
  print_indent(indent + 1);
  std::cout << "Expr:\n";
  print_hir(*as_expr.expr, indent + 2);
  print_indent(indent + 1);
  std::cout << "Type: " << as_expr.hir_type.to_string() << "\n";
}

inline void print_hir(const HirExprArray &array, int indent) {
  print_indent(indent);
  std::cout << "Array [" << type_to_string(array.type) << "]\n";
  for (const auto &elem : array.elements) {
    print_hir(elem, indent + 1);
  }
}

inline void print_hir(const HirExprStruct &struct_expr, int indent);

inline void print_hir(const HirExprItem &item, int indent) {
  std::visit(
      [indent](auto &&arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::unique_ptr<HirExprBinary>>) {
          print_hir(*arg, indent);
        } else if constexpr (std::is_same_v<T, std::unique_ptr<HirExprUnary>>) {
          print_hir(*arg, indent);
        } else if constexpr (std::is_same_v<T, std::unique_ptr<HirExprCall>>) {
          print_hir(*arg, indent);
        } else if constexpr (std::is_same_v<T, std::unique_ptr<HirExprIndex>>) {
          print_hir(*arg, indent);
        } else if constexpr (std::is_same_v<T,
                                            std::unique_ptr<HirExprMember>>) {
          print_hir(*arg, indent);
        } else if constexpr (std::is_same_v<T, std::unique_ptr<HirExprAs>>) {
          print_hir(*arg, indent);
        } else if constexpr (std::is_same_v<T, std::unique_ptr<HirExprArray>>) {
          print_hir(*arg, indent);
        } else if constexpr (std::is_same_v<T,
                                            std::unique_ptr<HirExprStruct>>) {
          print_hir(*arg, indent);
        } else {
          print_hir(arg, indent);
        }
      },
      item);
}
inline void print_hir(const HirExprStruct &struct_expr, int indent) {
  print_indent(indent);
  std::cout << "StructInit: " << struct_expr.hir_type.to_string() << " ["
            << type_to_string(struct_expr.HirBase::type) << "]\n";
  for (const auto &field : struct_expr.fields) {
    print_indent(indent + 1);
    std::cout << "Field: " << field.name.text << "\n";
    print_hir(field.value->item, indent + 2);
  }
}

inline void print_hir(const HirExpr &expr, int indent) {
  print_indent(indent);
  std::cout << "Expr [" << type_to_string(expr.type) << "]\n";
  print_hir(expr.item, indent + 1);
}

inline void print_hir(const HirReturn &ret, int indent) {
  print_indent(indent);
  std::cout << "Return [" << type_to_string(ret.type) << "]\n";
  if (ret.expression) {
    print_hir(*ret.expression, indent + 1);
  }
}

inline void print_hir(const HirBreak &brk, int indent) {
  print_indent(indent);
  std::cout << "Break [" << type_to_string(brk.type) << "]\n";
}

inline void print_hir(const HirContinue &cont, int indent) {
  print_indent(indent);
  std::cout << "Continue [" << type_to_string(cont.type) << "]\n";
}

inline void print_hir(const HirLet &let, int indent) {
  print_indent(indent);
  std::cout << (let.is_const ? "Const: " : "Let: ") << let.name.text;
  if (let.explicit_type)
    std::cout << ": " << let.explicit_type->to_string();
  std::cout << " [" << type_to_string(let.type) << "]\n";
  if (let.initializer) {
    print_hir(*let.initializer, indent + 1);
  }
}

inline void print_hir(const HirAssign &assign, int indent) {
  print_indent(indent);
  std::cout << "Assign [" << type_to_string(assign.type) << "]\n";
  print_indent(indent + 1);
  std::cout << "LValue:\n";
  print_hir(assign.lvalue, indent + 2);
  print_indent(indent + 1);
  std::cout << "RValue:\n";
  print_hir(assign.rvalue, indent + 2);
}

inline void print_hir(const HirExprStmt &expr_stmt, int indent) {
  print_indent(indent);
  std::cout << "ExprStmt [" << type_to_string(expr_stmt.type) << "]\n";
  print_hir(expr_stmt.expr, indent + 1);
}

inline void print_hir(const HirBlock &block, int indent);

inline void print_hir(const HirIf &if_stmt, int indent) {
  print_indent(indent);
  std::cout << "If [" << type_to_string(if_stmt.type) << "]\n";
  print_indent(indent + 1);
  std::cout << "Condition:\n";
  print_hir(if_stmt.condition, indent + 2);
  print_indent(indent + 1);
  std::cout << "Then:\n";
  print_hir(if_stmt.then_block, indent + 2);
  if (if_stmt.else_block) {
    print_indent(indent + 1);
    std::cout << "Else:\n";
    print_hir(*if_stmt.else_block, indent + 2);
  }
}

inline void print_hir(const HirWhile &while_stmt, int indent) {
  print_indent(indent);
  std::cout << "While [" << type_to_string(while_stmt.type) << "]\n";
  print_indent(indent + 1);
  std::cout << "Condition:\n";
  print_hir(while_stmt.condition, indent + 2);
  print_indent(indent + 1);
  std::cout << "Block:\n";
  print_hir(while_stmt.block, indent + 2);
}

inline void print_hir(const HirFor &for_stmt, int indent) {
  print_indent(indent);
  std::cout << "For [" << type_to_string(for_stmt.type) << "]\n";
  if (for_stmt.init) {
    print_indent(indent + 1);
    std::cout << "Init:\n";
    print_hir(*for_stmt.init, indent + 2);
  }
  print_indent(indent + 1);
  std::cout << "Condition:\n";
  print_hir(for_stmt.condition, indent + 2);
  if (for_stmt.update) {
    print_indent(indent + 1);
    std::cout << "Update:\n";
    print_hir(*for_stmt.update, indent + 2);
  }
  print_indent(indent + 1);
  std::cout << "Block:\n";
  print_hir(for_stmt.block, indent + 2);
}

inline void print_hir(const HirStmtItem &item, int indent) {
  std::visit(
      [indent](auto &&arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::unique_ptr<HirIf>>) {
          print_hir(*arg, indent);
        } else if constexpr (std::is_same_v<T, std::unique_ptr<HirWhile>>) {
          print_hir(*arg, indent);
        } else if constexpr (std::is_same_v<T, std::unique_ptr<HirFor>>) {
          print_hir(*arg, indent);
        } else {
          print_hir(arg, indent);
        }
      },
      item);
}

inline void print_hir(const HirStmt &stmt, int indent) {
  print_indent(indent);
  std::cout << "Stmt [" << type_to_string(stmt.type) << "]\n";
  print_hir(stmt.item, indent + 1);
}

inline void print_hir(const HirBlock &block, int indent) {
  print_indent(indent);
  std::cout << "Block [" << type_to_string(block.type) << "]\n";
  for (const auto &stmt : block.stmts) {
    print_hir(stmt, indent + 1);
  }
}

inline void print_hir(const HirFnDef &fn, int indent) {
  print_indent(indent);
  std::cout << "FnDef: " << fn.name.text << "() -> "
            << (fn.return_type ? fn.return_type->to_string() : "void") << " ["
            << type_to_string(fn.type) << "]\n";
  print_hir(fn.block, indent + 1);
}

inline void print_hir(const HirTypedIdent &typed_ident, int indent) {
  print_indent(indent);
  std::cout << "TypedIdent: " << typed_ident.name.text << ": "
            << typed_ident.hir_type.to_string() << " ["
            << type_to_string(typed_ident.HirBase::type) << "]\n";
}

inline void print_hir(const HirStruct &strct, int indent) {
  print_indent(indent);
  std::cout << "Struct: " << strct.name.text << " ["
            << type_to_string(strct.type) << "]\n";
  for (const auto &field : strct.fields) {
    print_hir(field, indent + 1);
  }
}

inline void print_hir(const HirEnum &enm, int indent) {
  print_indent(indent);
  std::cout << "Enum: " << enm.name.text << " [" << type_to_string(enm.type)
            << "]\n";
  for (const auto &variant : enm.variants) {
    print_indent(indent + 1);
    std::cout << "Variant: " << variant.text << "\n";
  }
}

inline void print_hir(const HirImport &import_, int indent) {
  print_indent(indent);
  std::cout << "Import: \"" << import_.path << "\" as `" << import_.import_alias
            << "`";
  if (!import_.target_module.empty())
    std::cout << " -> " << import_.target_module;
  std::cout << " [" << type_to_string(import_.type) << "]\n";
}

inline void print_hir(const Hir &hir, int indent) {
  std::visit([indent](auto &&arg) { print_hir(arg, indent); }, hir);
}

inline void print_modules(const ModulesHir &modules) {
  for (const auto &[name, items] : modules) {
    std::cout << "Module: " << name << "\n";
    for (const auto &node : items) {
      print_hir(node, 1);
    }
  }
}

inline void print_llir_operand_kind(int indent, const LlirOperand &op) {
  print_indent(indent);
  const char *k = "?";
  if (op.kind == LlirOperand::Local)
    k = "local";
  else if (op.kind == LlirOperand::Register)
    k = "reg";
  else if (op.kind == LlirOperand::Global)
    k = "global";
  else if (op.kind == LlirOperand::Literal)
    k = "literal";
  std::visit(
      [&](const auto &v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::string>)
          std::cout << k << ": \"" << v << "\" " << type_to_string(op.type) << "\n";
        else if constexpr (std::is_same_v<T, int64_t>)
          std::cout << k << ": " << v << " " << type_to_string(op.type) << "\n";
        else if constexpr (std::is_same_v<T, double>)
          std::cout << k << ": " << v << " " << type_to_string(op.type) << "\n";
        else if constexpr (std::is_same_v<T, char>)
          std::cout << k << ": '" << v << "' " << type_to_string(op.type) << "\n";
        else if constexpr (std::is_same_v<T, bool>)
          std::cout << k << ": " << (v ? "true" : "false") << " "
                    << type_to_string(op.type) << "\n";
      },
      op.data);
}

inline void print_llir_inst(const LlirInstruction &ins, int indent) {
  print_indent(indent);
  std::visit(
      overloaded{
          [&](const LlirAlloca &x) {
            std::cout << "alloca " << x.var_name << " "
                      << type_to_string(x.type) << "\n";
          },
          [&](const LlirStore &x) {
            std::cout << "store "
                      << [&] {
                           if (x.dest.kind == LlirOperand::Local)
                             return "local:" +
                                    std::get<std::string>(x.dest.data);
                           if (x.dest.kind == LlirOperand::Global)
                             return "global:" +
                                    std::get<std::string>(x.dest.data);
                           return std::string("dest");
                         }()
                      << "\n";
            print_llir_operand_kind(indent + 1, x.value);
          },
          [&](const LlirLoad &x) {
            std::cout << "load " << x.dest_reg << " <-\n";
            print_llir_operand_kind(indent + 1, x.src);
          },
          [&](const LlirAddrOf &x) {
            std::cout << "addrof " << x.dest_reg << " &" << x.local_slot << "\n";
          },
          [&](const LlirIndexedStore &x) {
            std::cout << "indexed_store\n";
            print_indent(indent + 1);
            std::cout << "array:\n";
            print_llir_operand_kind(indent + 2, x.array_slot);
            print_indent(indent + 1);
            std::cout << "index:\n";
            print_llir_operand_kind(indent + 2, x.index);
            print_indent(indent + 1);
            std::cout << "value:\n";
            print_llir_operand_kind(indent + 2, x.value);
          },
          [&](const LlirFieldLoad &x) {
            std::cout << "field_load " << x.dest_reg << " ." << x.field << "\n";
            print_llir_operand_kind(indent + 1, x.object);
          },
          [&](const LlirFieldStore &x) {
            std::cout << "field_store ." << x.field << "\n";
            print_llir_operand_kind(indent + 1, x.object);
            print_llir_operand_kind(indent + 1, x.value);
          },
          [&](const LlirFieldAddr &x) {
            std::cout << "field_addr " << x.dest_reg << " ." << x.field << "\n";
            print_llir_operand_kind(indent + 1, x.object);
          },
          [&](const LlirBinaryOp &x) {
            std::cout << "binop " << static_cast<int>(x.op) << " "
                      << x.dest_reg << "\n";
            print_llir_operand_kind(indent + 1, x.lhs);
            print_llir_operand_kind(indent + 1, x.rhs);
          },
          [&](const LlirUnaryOp &x) {
            std::cout << "unary " << static_cast<int>(x.op) << " "
                      << x.dest_reg << "\n";
            print_llir_operand_kind(indent + 1, x.src);
          },
          [&](const LlirBranch &x) {
            std::cout << "branch " << x.target_branch << "\n";
          },
          [&](const LlirCondBranch &x) {
            std::cout << "cond_branch " << x.true_label << " / "
                      << x.false_label << "\n";
            print_llir_operand_kind(indent + 1, x.condition);
          },
          [&](const LlirReturn &x) {
            std::cout << "return";
            if (x.value)
              std::cout << "\n";
            else {
              std::cout << " void\n";
              return;
            }
            print_llir_operand_kind(indent + 1, *x.value);
          },
          [&](const LlirGetElement &x) {
            std::cout << "getelem " << x.dest_reg << "\n";
            print_llir_operand_kind(indent + 1, x.base);
            print_llir_operand_kind(indent + 1, x.index);
          },
          [&](const LlirCall &x) {
            std::cout << "call " << x.func_name;
            if (x.dest_reg)
              std::cout << " -> " << *x.dest_reg;
            std::cout << "\n";
            for (const auto &a : x.args)
              print_llir_operand_kind(indent + 1, a);
          },
          [&](const LlirCast &x) {
            std::cout << "cast " << x.dest_reg << " as "
                      << type_to_string(x.target_type) << "\n";
            print_llir_operand_kind(indent + 1, x.source);
          },
      },
      ins);
}

inline void print_llir_block(const LlirBlock &blk, int indent) {
  print_indent(indent);
  std::cout << "block " << blk.label << "\n";
  for (const auto &i : blk.instructions)
    print_llir_inst(i, indent + 1);
}

inline void print_llir_function(const LlirFunction &f, int indent) {
  print_indent(indent);
  std::cout << "fn " << f.name << " -> "
            << type_to_string(f.return_type) << "\n";
  print_indent(indent + 1);
  std::cout << "params:\n";
  for (const auto &p : f.params) {
    print_indent(indent + 2);
    std::cout << p.name << ": " << p.type_display << "\n";
  }
  for (const auto &b : f.blocks)
    print_llir_block(b, indent + 1);
}

inline void print_llir_global(const LlirGlobal &g, int indent) {
  print_indent(indent);
  std::cout << (g.is_constant ? "global const " : "global ") << g.name << " "
            << type_to_string(g.type) << " = ";
  std::visit(
      overloaded{
          [](const std::string &s) { std::cout << "\"" << s << "\""; },
          [](int64_t v) { std::cout << v; },
          [](double v) { std::cout << v; },
          [](char v) { std::cout << "'" << v << "'"; },
          [](bool v) { std::cout << (v ? "true" : "false"); }},
      g.initial_value);
  std::cout << "\n";
}

inline void print_llir(const LlirModule &m) {
  std::cout << "---- LLIR ----\n";
  for (const auto &gl : m.globals)
    print_llir_global(gl, 1);
  for (const auto &fn : m.functions)
    print_llir_function(fn, 1);
  std::cout << "--------------\n";
}

