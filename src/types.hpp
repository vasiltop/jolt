#pragma once

#include <cstddef>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

enum class PrimitiveKind {
  Void,
  Never,
  Bool,
  U8,
  U16,
  U32,
  U64,
  I8,
  I16,
  I32,
  I64,
  Real,
  String,
  Char,
  Null,
};

struct PrimitiveType {
  PrimitiveKind kind;
};

struct Type;

struct PointerType {
  std::unique_ptr<Type> pointee;
};

struct ArrayType {
  std::unique_ptr<Type> element;
  std::size_t size;
};

struct TupleType;
struct FunctionType;
struct NamedType;

struct Type {
  using Data =
      std::variant<PrimitiveType, std::unique_ptr<PointerType>,
                   std::unique_ptr<ArrayType>, std::unique_ptr<TupleType>,
                   std::unique_ptr<FunctionType>, std::unique_ptr<NamedType>>;
  Data data;
};

struct TupleType {
  std::vector<Type> elements;
};

struct FunctionType {
  std::vector<Type> params;
  std::unique_ptr<Type> return_type;
};

struct NamedType {
  std::optional<std::string> module;
  std::string name;
};

inline std::string primitive_kind_string(PrimitiveKind k) {
  switch (k) {
  case PrimitiveKind::Void:
    return "void";
  case PrimitiveKind::Never:
    return "never";
  case PrimitiveKind::Bool:
    return "bool";
  case PrimitiveKind::U8:
    return "u8";
  case PrimitiveKind::U16:
    return "u16";
  case PrimitiveKind::U32:
    return "u32";
  case PrimitiveKind::U64:
    return "u64";
  case PrimitiveKind::I8:
    return "i8";
  case PrimitiveKind::I16:
    return "i16";
  case PrimitiveKind::I32:
    return "i32";
  case PrimitiveKind::I64:
    return "i64";
  case PrimitiveKind::Real:
    return "real";
  case PrimitiveKind::String:
    return "string";
  case PrimitiveKind::Char:
    return "char";
  case PrimitiveKind::Null:
    return "null";
  }
  return "<?>";
}

inline std::string type_to_string(const Type &ty);

inline std::string type_to_string(const std::optional<Type> &ty) {
  if (!ty)
    return "<untyped>";
  return type_to_string(*ty);
}

inline std::string type_to_string(const Type &ty) {
  return std::visit(
      [&](const auto &arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, PrimitiveType>) {
          return primitive_kind_string(arg.kind);
        } else if constexpr (std::is_same_v<T, std::unique_ptr<PointerType>>) {
          return std::format("{}*", type_to_string(*arg->pointee));
        } else if constexpr (std::is_same_v<T, std::unique_ptr<ArrayType>>) {
          const auto &inner = type_to_string(*arg->element);
          if (arg->size)
            return std::format("[{}; {}]", inner, arg->size);
          return std::format("[{}]", inner);
        } else if constexpr (std::is_same_v<T, std::unique_ptr<TupleType>>) {
          std::string out = "(";
          for (size_t i = 0; i < arg->elements.size(); ++i) {
            if (i)
              out += ", ";
            out += type_to_string(arg->elements[i]);
          }
          out += ")";
          return out;
        } else if constexpr (std::is_same_v<T, std::unique_ptr<FunctionType>>) {
          std::string out = "fn(";
          for (size_t i = 0; i < arg->params.size(); ++i) {
            if (i)
              out += ", ";
            out += type_to_string(arg->params[i]);
          }
          out += ")";
          if (arg->return_type)
            out += std::format(" -> {}", type_to_string(*arg->return_type));
          else
            out += " -> void";
          return out;
        } else if constexpr (std::is_same_v<T, std::unique_ptr<NamedType>>) {
          if (arg->module) {
            return std::format("{}:{}", *(arg->module), arg->name);
          }

          return arg->name;
        }
        return "<?>";
      },
      ty.data);
}
