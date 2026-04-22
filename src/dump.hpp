#pragma once

#include "hir.hpp"
#include <iostream>

inline std::string type_to_string(const std::optional<Type> &type) {
  if (!type)
    return "<untyped>";
  return std::visit(
      [](auto &&arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, PrimitiveType>) {
          if (arg.kind == PrimitiveKind::Int)
            return "Int";
        }
        return "Unknown";
      },
      type->data);
}

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

inline void print_hir(const HirExprIdent &ident, int indent) {
  print_indent(indent);
  std::cout << "Ident: " << ident.tok.text << " [" << type_to_string(ident.type)
            << "]\n";
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
  for (size_t i = 0; i < path_expr.segments.size(); ++i) {
    std::cout << path_expr.segments[i].text;
    if (i + 1 < path_expr.segments.size())
      std::cout << "::";
  }
  std::cout << " [" << type_to_string(path_expr.type) << "]\n";
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
  std::cout << "Type: " << as_expr.type.to_string() << "\n";
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
        } else if constexpr (std::is_same_v<T, std::unique_ptr<HirExprPath>>) {
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
  std::cout << "StructInit: " << struct_expr.type.to_string() << " ["
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

inline void print_hir(const HirLet &let, int indent) {
  print_indent(indent);
  std::cout << "Let: " << let.name.text;
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
            << typed_ident.type.to_string() << " ["
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
  std::cout << "Import: ";
  for (size_t i = 0; i < import_.path.size(); ++i) {
    std::cout << import_.path[i].text;
    if (i + 1 < import_.path.size())
      std::cout << "::";
  }
  std::cout << " [" << type_to_string(import_.type) << "]\n";
}

inline void print_hir(const HirModuleLet &ml, int indent) {
  print_indent(indent);
  std::cout << "Module let: " << ml.name.text << " ["
            << type_to_string(ml.type) << "]\n";
  if (ml.explicit_type) {
    print_indent(indent + 1);
    std::cout << "Type: " << ml.explicit_type->to_string() << "\n";
  }
  if (ml.initializer) {
    print_hir(*ml.initializer, indent + 1);
  }
}

inline void print_hir(const HirConst &cnst, int indent) {
  print_indent(indent);
  std::cout << "Const: " << cnst.name.text << " [" << type_to_string(cnst.type)
            << "]\n";
  if (cnst.explicit_type) {
    print_indent(indent + 1);
    std::cout << "Type: " << cnst.explicit_type->to_string() << "\n";
  }
  print_hir(cnst.initializer, indent + 1);
}

inline void print_hir(const Hir &hir, int indent) {
  std::visit([indent](auto &&arg) { print_hir(arg, indent); }, hir);
}

inline void print_modules(const ModulesHir &modules) {
  for (const auto &[name, scope] : modules) {
    std::cout << "Module: " << name << "\n";
    for (const auto &node : scope.items) {
      print_hir(node, 1);
    }
  }
}
