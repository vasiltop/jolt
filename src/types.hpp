#pragma once
#include <variant>

enum class PrimitiveKind {
    Int,
};

struct PrimitiveType {
    PrimitiveKind kind;
};

struct Type {
	std::variant<PrimitiveType> data;
};
