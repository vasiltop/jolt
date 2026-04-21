#pragma once

#include "hir.hpp"
#include <iostream>

inline std::string type_to_string(const std::optional<Type>& type) {
    if (!type) return "<untyped>";
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, PrimitiveType>) {
            if (arg.kind == PrimitiveKind::Int) return "Int";
        }
        return "Unknown";
    }, type->data);
}

inline void print_indent(int indent) {
    for (int i = 0; i < indent; ++i) std::cout << "  ";
}

template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

inline void print_hir(const Hir& hir, int indent = 0);
inline void print_hir(const HirStmt& stmt, int indent);

inline void print_hir(const HirReturn& ret, int indent) {
    print_indent(indent);
    std::cout << "Return: [expr]" << " [" << type_to_string(ret.type) << "]\n";
}

inline void print_hir(const HirStmtItem& item, int indent) {
    std::visit([indent](auto&& arg) { 
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, HirReturn>) {
            print_hir(arg, indent);
        } else {
            print_indent(indent);
            std::cout << "<other stmt>\n";
        }
    }, item);
}

inline void print_hir(const HirStmt& stmt, int indent) {
    print_indent(indent);
    std::cout << "Stmt [" << type_to_string(stmt.type) << "]\n";
    print_hir(stmt.item, indent + 1);
}

inline void print_hir(const HirBlock& block, int indent) {
    print_indent(indent);
    std::cout << "Block [" << type_to_string(block.type) << "]\n";
    for (const auto& stmt : block.stmts) {
        print_hir(stmt, indent + 1);
    }
}

inline void print_hir(const HirFnDef& fn, int indent) {
    print_indent(indent);
    std::cout << "FnDef: " << fn.name.text << "() -> " << (fn.return_type ? fn.return_type->text : "void") << " [" << type_to_string(fn.type) << "]\n";
    print_hir(fn.block, indent + 1);
}

inline void print_hir(const Hir& hir, int indent) {
    std::visit([indent](auto&& arg) { 
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, HirFnDef>) {
            print_hir(arg, indent);
        } else {
            print_indent(indent);
            std::cout << "<other top-level item>\n";
        }
    }, hir);
}

inline void print_modules(const ModulesHir& modules) {
    for (const auto& [name, hir_list] : modules) {
        std::cout << "Module: " << name << "\n";
        for (const auto& node : hir_list) {
            print_hir(node, 1);
        }
    }
}
