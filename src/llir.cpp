#include "llir.hpp"
#include "tokens.hpp"

#include <charconv>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

template <class... Ts> struct overloaded_ts : Ts... {
  using Ts::operator()...;
};
template <class... Ts> overloaded_ts(Ts...) -> overloaded_ts<Ts...>;

auto type_void() -> Type { return Type{PrimitiveType{PrimitiveKind::Void}}; }

auto type_bool() -> Type { return Type{PrimitiveType{PrimitiveKind::Bool}}; }

auto is_void_type(const Type &t) -> bool {
  if (auto *p = std::get_if<PrimitiveType>(&t.data))
    return p->kind == PrimitiveKind::Void;
  return false;
}

auto is_pointer_ty(const Type &t) -> bool {
  return std::holds_alternative<std::unique_ptr<PointerType>>(t.data);
}

auto is_array_ty(const Type &t) -> bool {
  return std::holds_alternative<std::unique_ptr<ArrayType>>(t.data);
}

auto clone_type_deep(const Type &t) -> Type {
  return std::visit(
      [&](const auto &arg) -> Type {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, PrimitiveType>) {
          return Type{arg};
        } else if constexpr (std::is_same_v<T, std::unique_ptr<PointerType>>) {
          auto p = std::make_unique<PointerType>();
          p->pointee = std::make_unique<Type>(clone_type_deep(*arg->pointee));
          return Type{.data = std::move(p)};
        } else if constexpr (std::is_same_v<T, std::unique_ptr<ArrayType>>) {
          auto a = std::make_unique<ArrayType>();
          a->element = std::make_unique<Type>(clone_type_deep(*arg->element));
          a->size = arg->size;
          return Type{.data = std::move(a)};
        } else if constexpr (std::is_same_v<T, std::unique_ptr<TupleType>>) {
          auto u = std::make_unique<TupleType>();
          for (const auto &e : arg->elements)
            u->elements.push_back(clone_type_deep(e));
          return Type{.data = std::move(u)};
        } else if constexpr (std::is_same_v<T, std::unique_ptr<FunctionType>>) {
          auto f = std::make_unique<FunctionType>();
          for (const Type &par : arg->params)
            f->params.push_back(clone_type_deep(par));
          if (arg->return_type)
            f->return_type =
                std::make_unique<Type>(clone_type_deep(*arg->return_type));
          return Type{.data = std::move(f)};
        } else if constexpr (std::is_same_v<T, std::unique_ptr<NamedType>>) {
          auto n = std::make_unique<NamedType>();
          n->module = arg->module;
          n->name = arg->name;
          return Type{.data = std::move(n)};
        }
        return type_void();
      },
      t.data);
}

auto make_ptr_ty(Type inner) -> Type {
  auto p = std::make_unique<PointerType>();
  p->pointee = std::make_unique<Type>(clone_type_deep(std::move(inner)));
  return Type{.data = std::move(p)};
}

auto primitive_from_text_sv(std::string_view s)
    -> std::optional<PrimitiveKind> {
  if (s == "void")
    return PrimitiveKind::Void;
  if (s == "never")
    return PrimitiveKind::Never;
  if (s == "bool")
    return PrimitiveKind::Bool;
  if (s == "u8")
    return PrimitiveKind::U8;
  if (s == "u16")
    return PrimitiveKind::U16;
  if (s == "u32")
    return PrimitiveKind::U32;
  if (s == "u64")
    return PrimitiveKind::U64;
  if (s == "i8")
    return PrimitiveKind::I8;
  if (s == "i16")
    return PrimitiveKind::I16;
  if (s == "i32")
    return PrimitiveKind::I32;
  if (s == "i64")
    return PrimitiveKind::I64;
  if (s == "real")
    return PrimitiveKind::Real;
  if (s == "string")
    return PrimitiveKind::String;
  if (s == "char")
    return PrimitiveKind::Char;
  if (s == "null")
    return PrimitiveKind::Null;
  return std::nullopt;
}

struct ModuleSymbols {
  std::unordered_map<std::string, std::string> import_alias_to_target_module;
  std::unordered_map<std::string, const HirFnDef *> fns_by_name;
  std::unordered_map<std::string, const HirLet *> globals_by_name;
  std::unordered_map<std::string, const HirStruct *> structs_by_name;
  std::unordered_map<std::string, const HirEnum *> enums_by_name;
};

auto collect_module_symbols(const std::vector<Hir> &items) -> ModuleSymbols {
  ModuleSymbols ms;
  for (const auto &node : items) {
    std::visit(
        [&](auto const &arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, HirFnDef>)
            ms.fns_by_name[arg.name.text] = &arg;
          else if constexpr (std::is_same_v<T, HirStruct>)
            ms.structs_by_name[arg.name.text] = &arg;
          else if constexpr (std::is_same_v<T, HirEnum>)
            ms.enums_by_name[arg.name.text] = &arg;
          else if constexpr (std::is_same_v<T, HirImport>) {
            if (!arg.import_alias.empty() && !arg.target_module.empty())
              ms.import_alias_to_target_module[arg.import_alias] =
                  arg.target_module;
          }
        },
        node);
  }
  for (const auto &node : items)
    if (auto *gl = std::get_if<HirLet>(&node))
      ms.globals_by_name[gl->name.text] = gl;
  return ms;
}

auto resolve_named_struct(
    const NamedType &n, const ModuleSymbols &for_module,
    const std::unordered_map<std::string, ModuleSymbols> &all_mods)
    -> const HirStruct * {
  if (!n.module) {
    auto it = for_module.structs_by_name.find(n.name);
    return it == for_module.structs_by_name.end() ? nullptr : it->second;
  }
  auto ali = for_module.import_alias_to_target_module.find(*n.module);
  if (ali == for_module.import_alias_to_target_module.end())
    return nullptr;
  auto mod_it = all_mods.find(ali->second);
  if (mod_it == all_mods.end())
    return nullptr;
  auto st = mod_it->second.structs_by_name.find(n.name);
  return st == mod_it->second.structs_by_name.end() ? nullptr : st->second;
}

auto resolve_named_enum(
    const NamedType &n, const ModuleSymbols &for_module,
    const std::unordered_map<std::string, ModuleSymbols> &all_mods)
    -> const HirEnum * {
  if (!n.module) {
    auto it = for_module.enums_by_name.find(n.name);
    return it == for_module.enums_by_name.end() ? nullptr : it->second;
  }
  auto ali = for_module.import_alias_to_target_module.find(*n.module);
  if (ali == for_module.import_alias_to_target_module.end())
    return nullptr;
  auto mod_it = all_mods.find(ali->second);
  if (mod_it == all_mods.end())
    return nullptr;
  auto e = mod_it->second.enums_by_name.find(n.name);
  return e == mod_it->second.enums_by_name.end() ? nullptr : e->second;
}

auto resolve_fn(
    const std::optional<Token> &module_opt, const std::string &name,
    const ModuleSymbols &for_module,
    const std::unordered_map<std::string, ModuleSymbols> &all_mods)
    -> const HirFnDef * {
  if (!module_opt) {
    auto it = for_module.fns_by_name.find(name);
    return it == for_module.fns_by_name.end() ? nullptr : it->second;
  }
  auto ali = for_module.import_alias_to_target_module.find(module_opt->text);
  if (ali == for_module.import_alias_to_target_module.end())
    return nullptr;
  auto mod_it = all_mods.find(ali->second);
  if (mod_it == all_mods.end())
    return nullptr;
  auto f = mod_it->second.fns_by_name.find(name);
  return f == mod_it->second.fns_by_name.end() ? nullptr : f->second;
}

auto hir_path_to_named(const HirTypePath &tp) -> std::unique_ptr<NamedType> {
  if (!tp.module)
    return nullptr;
  return std::make_unique<NamedType>(
      NamedType{.module = tp.module->text, .name = tp.name.text});
}

auto hir_to_type_llir(
    const HirType &ht, const ModuleSymbols &cur_syms,
    const std::unordered_map<std::string, ModuleSymbols> &all_mods,
    bool &had_error_ref) -> std::optional<Type> {
  return std::visit(
      [&](auto &&arg) -> std::optional<Type> {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, HirTypePath>) {
          const auto &nm = arg.name.text;
          if (!arg.module) {
            if (auto pk = primitive_from_text_sv(nm))
              return Type{PrimitiveType{*pk}};
            if (cur_syms.structs_by_name.contains(nm) ||
                cur_syms.enums_by_name.contains(nm))
              return Type{std::make_unique<NamedType>(NamedType{.name = nm})};
            std::cerr << "llir: unknown type \"" << nm << "\"\n";
            had_error_ref = true;
            return std::nullopt;
          }
          const std::string &mod = arg.module->text;
          auto it = cur_syms.import_alias_to_target_module.find(mod);
          if (it == cur_syms.import_alias_to_target_module.end()) {
            std::cerr << "llir: unknown module alias \"" << mod << "\"\n";
            had_error_ref = true;
            return std::nullopt;
          }
          const ModuleSymbols &ms = all_mods.at(it->second);
          if (!ms.structs_by_name.contains(nm) &&
              !ms.enums_by_name.contains(nm)) {
            std::cerr << "llir: unknown type \"" << mod << ":" << nm << "\"\n";
            had_error_ref = true;
            return std::nullopt;
          }
          return Type{hir_path_to_named(arg)};
        }
        if constexpr (std::is_same_v<T, std::unique_ptr<HirTypePtr>>) {
          if (!arg || !arg->base)
            return std::nullopt;
          auto inner =
              hir_to_type_llir(*arg->base, cur_syms, all_mods, had_error_ref);
          if (!inner)
            return std::nullopt;
          return make_ptr_ty(std::move(*inner));
        }
        return std::nullopt;
      },
      ht.item);
}

auto function_return_type_llir(
    const HirFnDef &fn, const ModuleSymbols &module_syms,
    const std::unordered_map<std::string, ModuleSymbols> &all_mods) -> Type {
  bool err = false;
  if (!fn.return_type)
    return type_void();
  if (auto t = hir_to_type_llir(*fn.return_type, module_syms, all_mods, err);
      t && !err)
    return std::move(*t);
  return type_void();
}

auto symbol_mangled(std::string_view mod_id, std::string_view nm)
    -> std::string {
  return std::format("{}:{}", mod_id, nm);
}

auto binary_op_for_token(TokenKind k) -> LlirBinaryOp::Kind {
  switch (k) {
  case TokenKind::Add:
    return LlirBinaryOp::Add;
  case TokenKind::Sub:
    return LlirBinaryOp::Sub;
  case TokenKind::Mul:
    return LlirBinaryOp::Mul;
  case TokenKind::Div:
    return LlirBinaryOp::Div;
  case TokenKind::Mod:
    return LlirBinaryOp::Mod;
  case TokenKind::And:
    return LlirBinaryOp::And;
  case TokenKind::Or:
    return LlirBinaryOp::Or;
  case TokenKind::LessThan:
    return LlirBinaryOp::Lt;
  case TokenKind::GreaterThan:
    return LlirBinaryOp::Gt;
  case TokenKind::LessThanEqual:
    return LlirBinaryOp::Le;
  case TokenKind::GreaterThanEqual:
    return LlirBinaryOp::Ge;
  case TokenKind::Equal:
    return LlirBinaryOp::Eq;
  case TokenKind::NotEqual:
    return LlirBinaryOp::Ne;
  default:
    return LlirBinaryOp::Add;
  }
}

struct LoopLabels {
  std::string continue_target;
  std::string break_label;
};

auto path_top_level(const HirExpr &e) -> const HirExprPath * {
  return std::get_if<HirExprPath>(&e.item);
}

struct Emitter {
  const std::unordered_map<std::string, ModuleSymbols> *all_syms{};
  LlirModule *out{};
  std::string cur_module;

  LlirFunction *fn{};
  size_t cur_block_ix = 0;
  unsigned next_tmp = 0;
  unsigned next_label = 0;
  unsigned next_anon_slot = 0;

  Type return_ty = type_void();
  std::vector<std::unordered_map<std::string, std::string>> scopes;
  std::vector<LoopLabels> loops;

  void scope_push() { scopes.emplace_back(); }
  void scope_pop() {
    if (!scopes.empty())
      scopes.pop_back();
  }

  void bind(std::string name, std::string slot) {
    if (scopes.empty())
      scopes.emplace_back();
    scopes.back()[std::move(name)] = std::move(slot);
  }

  auto lookup_slot(std::string_view name) -> std::optional<std::string> {
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
      auto j = (*it).find(std::string(name));
      if (j != (*it).end())
        return j->second;
    }
    return std::nullopt;
  }

  auto fresh_reg() -> std::string { return std::format("%t{}", next_tmp++); }

  auto fresh_label(std::string_view prefix) -> std::string {
    return std::format(".{}{}", prefix, next_label++);
  }

  auto fresh_anon_slot(std::string_view hint) -> std::string {
    return std::format("%{}_{}", hint, next_anon_slot++);
  }

  auto cur_syms() const -> const ModuleSymbols & {
    return all_syms->at(cur_module);
  }

  auto current_block() -> LlirBlock & { return fn->blocks[cur_block_ix]; }

  void emit_inst(LlirInstruction &&ins) {
    fn->blocks[cur_block_ix].instructions.push_back(std::move(ins));
  }

  void append_block(std::string label) {
    fn->blocks.push_back(
        LlirBlock{.label = std::move(label), .instructions = {}});
    cur_block_ix = fn->blocks.size() - 1;
  }

  auto type_has_struct_aggregate(const Type &t) -> bool {
    auto *named = std::get_if<std::unique_ptr<NamedType>>(&t.data);
    if (!named)
      return false;
    return resolve_named_struct(**named, cur_syms(), *all_syms) != nullptr;
  }

  auto type_skip_load_in_slot(const Type &t) -> bool {
    return is_array_ty(t) || type_has_struct_aggregate(t);
  }

  auto type_is_enum_named(const Type &t) -> bool {
    auto *named = std::get_if<std::unique_ptr<NamedType>>(&t.data);
    if (!named)
      return false;
    return resolve_named_enum(**named, cur_syms(), *all_syms) != nullptr;
  }

  auto local_slot_op(std::string slot, Type ty) -> LlirOperand {
    return LlirOperand{.kind = LlirOperand::Local,
                       .data = std::move(slot),
                       .type = std::move(ty)};
  }

  auto glob_op(std::string id, Type ty) -> LlirOperand {
    return LlirOperand{.kind = LlirOperand::Global,
                       .data = std::move(id),
                       .type = std::move(ty)};
  }

  auto literal_op(TokenKind tk, std::string_view text,
                  const std::optional<Type> &explicit_ty) -> LlirOperand {
    Type hint = explicit_ty ? clone_type_deep(*explicit_ty)
                            : clone_type_deep(type_void());
    LlirOperand o{.kind = LlirOperand::Literal, .type = std::move(hint)};
    switch (tk) {
    case TokenKind::False:
      o.type = type_bool();
      o.data = false;
      break;
    case TokenKind::True:
      o.type = type_bool();
      o.data = true;
      break;
    case TokenKind::Null:
      o.type = Type{PrimitiveType{PrimitiveKind::Null}};
      o.data = int64_t{0};
      break;
    case TokenKind::Integer: {
      int64_t v = 0;
      std::from_chars(text.data(), text.data() + text.size(), v);
      o.type = explicit_ty.has_value()
                   ? clone_type_deep(*explicit_ty)
                   : Type{PrimitiveType{PrimitiveKind::I32}};
      o.data = v;
      break;
    }
    case TokenKind::Real:
      o.type = Type{PrimitiveType{PrimitiveKind::Real}};
      o.data = std::stod(std::string(text));
      break;
    case TokenKind::Char:
      o.type = Type{PrimitiveType{PrimitiveKind::Char}};
      o.data = text.empty() ? char{0} : text[0];
      break;
    case TokenKind::String:
      o.type = Type{PrimitiveType{PrimitiveKind::String}};
      o.data = std::string{text};
      break;
    default:
      break;
    }
    return o;
  }

  auto materialize_primitive_or_pointer(LlirOperand op) -> LlirOperand;
  auto emit_rvalue(const HirExpr &e) -> LlirOperand;
  void emit_store_to_lvalue(const HirExpr &lhs, LlirOperand rhs);
  void emit_stmt(const HirStmt &stmt);
  void emit_block(const HirBlock &blk);
  auto emit_aggregate_for_member_inner(const HirExpr &object_expr,
                                       bool unwrap_ptr) -> LlirOperand;

  auto path_mangled_path(const HirExprPath &path) -> std::string {
    if (!path.module)
      return symbol_mangled(cur_module, path.name.text);
    auto it = cur_syms().import_alias_to_target_module.find(path.module->text);
    if (it == cur_syms().import_alias_to_target_module.end())
      return symbol_mangled(cur_module, path.name.text);
    return symbol_mangled(it->second, path.name.text);
  }

  auto resolve_qualifier_module_id(const HirExprPath &p) -> std::string {
    if (!p.module)
      return cur_module;
    auto it = cur_syms().import_alias_to_target_module.find(p.module->text);
    if (it == cur_syms().import_alias_to_target_module.end())
      return cur_module;
    return it->second;
  }

  auto emit_address_of(const HirExpr &e) -> LlirOperand;
  auto enum_variant_const(const HirExprEnumVariant &ev) -> LlirOperand;

  void lower_fn_body(const HirFnDef &hir_fn);

  auto block_ends_with_terminal() -> bool {
    auto &instrs = current_block().instructions;
    if (instrs.empty())
      return false;
    auto &tail = instrs.back();
    return std::holds_alternative<LlirReturn>(tail) ||
           std::holds_alternative<LlirBranch>(tail) ||
           std::holds_alternative<LlirCondBranch>(tail);
  }
};

auto Emitter::materialize_primitive_or_pointer(LlirOperand op) -> LlirOperand {
  if (op.kind == LlirOperand::Literal || op.kind == LlirOperand::Register)
    return op;
  Type ty = clone_type_deep(op.type);
  if (type_skip_load_in_slot(ty))
    return op;
  std::string r = fresh_reg();
  emit_inst(LlirLoad{.dest_reg = r, .src = std::move(op)});
  LlirOperand out{.kind = LlirOperand::Register};
  out.data = r;
  out.type = std::move(ty);
  return out;
}

auto Emitter::emit_aggregate_for_member_inner(const HirExpr &object_expr,
                                              bool unwrap_ptr) -> LlirOperand {
  LlirOperand obj = emit_rvalue(object_expr);
  Type t = clone_type_deep(obj.type);
  if (!unwrap_ptr)
    return obj;
  if (!is_pointer_ty(t))
    return obj;
  std::string r = fresh_reg();
  emit_inst(LlirLoad{.dest_reg = r, .src = std::move(obj)});
  LlirOperand out{.kind = LlirOperand::Register, .data = std::move(r)};
  auto *pee = std::get_if<std::unique_ptr<PointerType>>(&t.data);
  if (pee && (*pee)->pointee)
    out.type = clone_type_deep(*(*pee)->pointee);
  else
    out.type = clone_type_deep(t);
  return out;
}

auto Emitter::emit_address_of(const HirExpr &e) -> LlirOperand {
  if (auto *pth = path_top_level(e)) {
    auto slot_opt = lookup_slot(pth->name.text);
    if (!slot_opt) {
      return LlirOperand{.kind = LlirOperand::Register,
                         .type = clone_type_deep(type_void())};
    }
    std::string r = fresh_reg();
    emit_inst(LlirAddrOf{.dest_reg = r, .local_slot = std::move(*slot_opt)});
    auto inner_ty =
        pth->type ? clone_type_deep(*pth->type)
                  : clone_type_deep(Type{PrimitiveType{PrimitiveKind::I32}});
    LlirOperand out{.kind = LlirOperand::Register, .data = std::move(r)};
    out.type = make_ptr_ty(std::move(inner_ty));
    return out;
  }
  auto *uniq_mem = std::get_if<std::unique_ptr<HirExprMember>>(&e.item);
  if (!uniq_mem || !*uniq_mem)
    return LlirOperand{.kind = LlirOperand::Register,
                       .type = clone_type_deep(type_void())};

  HirExprMember *mem = uniq_mem->get();
  LlirOperand base_addr = emit_address_of(*mem->object);
  std::string r = fresh_reg();
  emit_inst(LlirFieldAddr{.dest_reg = r,
                          .object = std::move(base_addr),
                          .field = mem->member.text});
  LlirOperand out{.kind = LlirOperand::Register, .data = std::move(r)};
  Type field_ty =
      mem->type ? clone_type_deep(*mem->type)
                : clone_type_deep(Type{PrimitiveType{PrimitiveKind::I32}});
  out.type = make_ptr_ty(std::move(field_ty));
  return out;
}

auto Emitter::enum_variant_const(const HirExprEnumVariant &ev) -> LlirOperand {
  const HirEnum *en = nullptr;
  const ModuleSymbols *ms = nullptr;
  if (ev.module) {
    auto it_alias =
        cur_syms().import_alias_to_target_module.find(ev.module->text);
    if (it_alias == cur_syms().import_alias_to_target_module.end()) {
      return literal_op(TokenKind::Integer, "0", ev.type);
    }
    auto mod_it = all_syms->find(it_alias->second);
    if (mod_it == all_syms->end())
      return literal_op(TokenKind::Integer, "0", ev.type);
    ms = &mod_it->second;
    auto ze = ms->enums_by_name.find(ev.enum_name.text);
    if (ze == ms->enums_by_name.end())
      return literal_op(TokenKind::Integer, "0", ev.type);
    en = ze->second;
  } else {
    ms = &cur_syms();
    auto ze = ms->enums_by_name.find(ev.enum_name.text);
    if (ze == ms->enums_by_name.end())
      return literal_op(TokenKind::Integer, "0", ev.type);
    en = ze->second;
  }
  int64_t i = 0;
  for (const auto &vx : en->variants) {
    if (vx.text == ev.variant.text) {
      Type et = ev.type ? clone_type_deep(*ev.type)
                        : Type{std::make_unique<NamedType>(
                              NamedType{.name = ev.enum_name.text})};
      LlirOperand o{.kind = LlirOperand::Literal};
      o.data = i;
      o.type = std::move(et);
      return o;
    }
    ++i;
  }
  return literal_op(TokenKind::Integer, "0", ev.type);
}

auto Emitter::emit_rvalue(const HirExpr &e) -> LlirOperand {
  Type hint_ty = e.type ? clone_type_deep(*e.type) : type_void();

  return std::visit(
      overloaded_ts{
          [&](const HirExprLiteral &lit) {
            LlirOperand o = literal_op(lit.tok.kind, lit.tok.text, lit.type);
            if (lit.type)
              o.type = clone_type_deep(*lit.type);
            return o;
          },
          [&](const HirExprPath &p) -> LlirOperand {
            Type pty =
                p.type ? clone_type_deep(*p.type) : clone_type_deep(hint_ty);
            if (!p.module) {
              if (auto slot = lookup_slot(p.name.text)) {
                LlirOperand op = local_slot_op(*slot, clone_type_deep(pty));
                if (type_skip_load_in_slot(pty))
                  return op;
                return materialize_primitive_or_pointer(std::move(op));
              }

              auto gl = cur_syms().globals_by_name.find(p.name.text);
              if (gl != cur_syms().globals_by_name.end()) {
                Type gt = gl->second->type ? clone_type_deep(*gl->second->type)
                                           : clone_type_deep(pty);
                LlirOperand g = glob_op(symbol_mangled(cur_module, p.name.text),
                                        clone_type_deep(gt));
                if (type_skip_load_in_slot(gt))
                  return materialize_primitive_or_pointer(std::move(g));
                return materialize_primitive_or_pointer(std::move(g));
              }

              Type fn_ty =
                  p.type ? clone_type_deep(*p.type) : clone_type_deep(pty);
              return glob_op(symbol_mangled(cur_module, p.name.text),
                             clone_type_deep(fn_ty));
            }

            std::string tgt_mod = resolve_qualifier_module_id(p);
            const ModuleSymbols &ms = (*all_syms).at(tgt_mod);
            auto gl = ms.globals_by_name.find(p.name.text);
            if (gl != ms.globals_by_name.end()) {
              Type gt = gl->second->type ? clone_type_deep(*gl->second->type)
                                         : clone_type_deep(pty);
              LlirOperand g = glob_op(symbol_mangled(tgt_mod, p.name.text),
                                      clone_type_deep(gt));
              return materialize_primitive_or_pointer(std::move(g));
            }
            if (ms.fns_by_name.count(p.name.text))
              return glob_op(symbol_mangled(tgt_mod, p.name.text),
                             clone_type_deep(pty));

            return glob_op(symbol_mangled(tgt_mod, p.name.text),
                           clone_type_deep(pty));
          },
          [&](const HirExprEnumVariant &ev) { return enum_variant_const(ev); },
          [&](const std::unique_ptr<HirExprBinary> &b_ptr) -> LlirOperand {
            const HirExprBinary &bb = *b_ptr;
            LlirOperand L = emit_rvalue(*bb.lhs);
            LlirOperand R = emit_rvalue(*bb.rhs);
            LlirOperand l = materialize_primitive_or_pointer(std::move(L));
            LlirOperand r = materialize_primitive_or_pointer(std::move(R));
            std::string d = fresh_reg();
            emit_inst(LlirBinaryOp{.op = binary_op_for_token(bb.op.kind),
                                   .dest_reg = d,
                                   .lhs = std::move(l),
                                   .rhs = std::move(r)});
            LlirOperand out{.kind = LlirOperand::Register, .data = d};
            out.type =
                bb.type ? clone_type_deep(*bb.type) : clone_type_deep(hint_ty);
            return out;
          },
          [&](const std::unique_ptr<HirExprUnary> &u_ptr) -> LlirOperand {
            const HirExprUnary *un = u_ptr.get();
            switch (un->op.kind) {
            case TokenKind::Ampersand:
              return emit_address_of(*un->expr);
            case TokenKind::Mul: {
              LlirOperand ptr =
                  materialize_primitive_or_pointer(emit_rvalue(*un->expr));
              std::string d = fresh_reg();
              emit_inst(LlirLoad{.dest_reg = d, .src = std::move(ptr)});
              LlirOperand out{.kind = LlirOperand::Register, .data = d};
              out.type = un->type ? clone_type_deep(*un->type)
                                  : clone_type_deep(hint_ty);
              return out;
            }
            default: {
              LlirUnaryOp::Kind uop = un->op.kind == TokenKind::Not
                                          ? LlirUnaryOp::Kind::Not
                                          : LlirUnaryOp::Kind::Neg;
              LlirOperand in =
                  materialize_primitive_or_pointer(emit_rvalue(*un->expr));
              std::string d = fresh_reg();
              emit_inst(
                  LlirUnaryOp{.op = uop, .dest_reg = d, .src = std::move(in)});
              LlirOperand out{.kind = LlirOperand::Register, .data = d};
              out.type = un->type ? clone_type_deep(*un->type)
                                  : clone_type_deep(hint_ty);
              return out;
            }
            }
          },
          [&](const std::unique_ptr<HirExprIndex> &idx_ptr) -> LlirOperand {
            const HirExprIndex *idx = idx_ptr.get();
            LlirOperand base = emit_rvalue(*idx->value);
            LlirOperand index_op =
                materialize_primitive_or_pointer(emit_rvalue(*idx->index));
            std::string d = fresh_reg();
            emit_inst(LlirGetElement{.dest_reg = d,
                                     .base = std::move(base),
                                     .index = std::move(index_op)});
            LlirOperand out{.kind = LlirOperand::Register, .data = d};
            out.type = idx->type ? clone_type_deep(*idx->type)
                                 : clone_type_deep(hint_ty);
            return out;
          },
          [&](const std::unique_ptr<HirExprMember> &memb_ptr) {
            const HirExprMember *mm = memb_ptr.get();
            LlirOperand ob =
                emit_aggregate_for_member_inner(*mm->object, /*unwrap=*/true);
            std::string d = fresh_reg();
            emit_inst(LlirFieldLoad{.dest_reg = d,
                                    .object = std::move(ob),
                                    .field = mm->member.text});
            LlirOperand out{.kind = LlirOperand::Register, .data = d};
            out.type = mm->type ? clone_type_deep(*mm->type)
                                : clone_type_deep(hint_ty);
            return out;
          },
          [&](const std::unique_ptr<HirExprCall> &call_ptr) -> LlirOperand {
            const HirExprCall *call = call_ptr.get();
            auto *callee_path = path_top_level(*call->callee);
            std::string mangled = "unknown_call";
            if (callee_path) {
              auto *fn_def = resolve_fn(callee_path->module, callee_path->name.text, cur_syms(), *all_syms);
              if (fn_def && fn_def->is_extern)
                mangled = callee_path->name.text;
              else
                mangled = path_mangled_path(*callee_path);
            }
            std::vector<LlirOperand> args;
            args.reserve(call->args.size());
            for (const auto &arg : call->args)
              args.push_back(
                  materialize_primitive_or_pointer(emit_rvalue(arg)));
            std::optional<std::string> dest;
            bool void_ret = !call->type || is_void_type(*call->type);
            if (!void_ret)
              dest = fresh_reg();
            emit_inst(LlirCall{.func_name = std::move(mangled),
                               .args = std::move(args),
                               .dest_reg = dest});
            if (!dest)
              return literal_op(TokenKind::Integer, "0", std::nullopt);
            LlirOperand out{.kind = LlirOperand::Register};
            out.data = *dest;
            out.type = call->type ? clone_type_deep(*call->type)
                                  : clone_type_deep(hint_ty);
            return out;
          },
          [&](const std::unique_ptr<HirExprAs> &as_ptr) -> LlirOperand {
            const HirExprAs *as_e = as_ptr.get();
            LlirOperand in =
                materialize_primitive_or_pointer(emit_rvalue(*as_e->expr));
            bool herr = false;
            auto targ =
                hir_to_type_llir(as_e->hir_type, cur_syms(), *all_syms, herr);
            Type cast_ty = targ ? std::move(*targ)
                                : (as_e->type ? clone_type_deep(*as_e->type)
                                              : clone_type_deep(hint_ty));
            std::string d = fresh_reg();
            emit_inst(LlirCast{.dest_reg = d,
                               .source = std::move(in),
                               .target_type = clone_type_deep(cast_ty)});
            LlirOperand out{.kind = LlirOperand::Register, .data = d};
            out.type = std::move(cast_ty);
            return out;
          },
          [&](const std::unique_ptr<HirExprArray> &arr_ptr) -> LlirOperand {
            const HirExprArray *arr = arr_ptr.get();
            Type arr_ty = arr->type ? clone_type_deep(*arr->type)
                                    : clone_type_deep(hint_ty);
            std::string slot = fresh_anon_slot("arr");
            emit_inst(
                LlirAlloca{.var_name = slot, .type = clone_type_deep(arr_ty)});
            Type arr_ty_clone = clone_type_deep(arr_ty);
            for (size_t i = 0; i < arr->elements.size(); ++i) {
              LlirOperand elem = emit_rvalue(arr->elements[i]);
              LlirOperand idx_lit{};
              idx_lit.kind = LlirOperand::Literal;
              idx_lit.data = static_cast<int64_t>(i);
              idx_lit.type = Type{PrimitiveType{PrimitiveKind::I32}};
              emit_inst(LlirIndexedStore{
                  .array_slot =
                      local_slot_op(slot, clone_type_deep(arr_ty_clone)),
                  .index = std::move(idx_lit),
                  .value = std::move(elem),
              });
            }
            return local_slot_op(std::move(slot), std::move(arr_ty));
          },
          [&](const std::unique_ptr<HirExprStruct> &st_ptr) -> LlirOperand {
            const HirExprStruct *stlit = st_ptr.get();
            Type sty = stlit->type ? clone_type_deep(*stlit->type)
                                   : clone_type_deep(hint_ty);
            Type sty_clone = clone_type_deep(sty);
            std::string slot = fresh_anon_slot("struct");
            emit_inst(
                LlirAlloca{.var_name = slot, .type = clone_type_deep(sty)});
            for (const auto &sf : stlit->fields) {
              LlirOperand val = emit_rvalue(*sf.value);
              emit_inst(LlirFieldStore{
                  .object = local_slot_op(slot, clone_type_deep(sty_clone)),
                  .field = sf.name.text,
                  .value = std::move(val),
              });
            }
            return local_slot_op(std::move(slot), std::move(sty));
          },
      },
      e.item);
}

void Emitter::emit_store_to_lvalue(const HirExpr &lhs, LlirOperand rhs) {
  LlirOperand rhs_use = [&]() -> LlirOperand {
    Type rty = clone_type_deep(rhs.type);
    if (!type_skip_load_in_slot(rty) && rhs.kind != LlirOperand::Literal &&
        !(rhs.kind == LlirOperand::Register))
      return materialize_primitive_or_pointer(std::move(rhs));
    if (rhs.kind == LlirOperand::Local || rhs.kind == LlirOperand::Global)
      return materialize_primitive_or_pointer(std::move(rhs));
    return std::move(rhs);
  }();

  std::visit(
      overloaded_ts{
          [&](const HirExprPath &p) {
            if (!p.module) {
              auto slot_opt = lookup_slot(p.name.text);
              if (slot_opt) {
                Type pty = p.type ? clone_type_deep(*p.type) : type_void();
                emit_inst(LlirStore{
                    .dest = local_slot_op(*slot_opt, std::move(pty)),
                    .value = std::move(rhs_use),
                });
                return;
              }
              LlirOperand g = glob_op(symbol_mangled(cur_module, p.name.text),
                                      p.type ? clone_type_deep(*p.type)
                                             : clone_type_deep(rhs_use.type));
              emit_inst(
                  LlirStore{.dest = std::move(g), .value = std::move(rhs_use)});
              return;
            }
            std::string tgt = resolve_qualifier_module_id(p);
            LlirOperand g = glob_op(symbol_mangled(tgt, p.name.text),
                                    p.type ? clone_type_deep(*p.type)
                                           : clone_type_deep(rhs_use.type));
            emit_inst(
                LlirStore{.dest = std::move(g), .value = std::move(rhs_use)});
          },
          [&](const std::unique_ptr<HirExprIndex> &ix) {
            LlirOperand arr = emit_rvalue(*ix->value);
            LlirOperand index_op =
                materialize_primitive_or_pointer(emit_rvalue(*ix->index));
            emit_inst(LlirIndexedStore{
                .array_slot = std::move(arr),
                .index = std::move(index_op),
                .value = std::move(rhs_use),
            });
          },
          [&](const std::unique_ptr<HirExprMember> &memb) {
            LlirOperand ob =
                emit_aggregate_for_member_inner(*memb->object, /*unwrap=*/true);
            emit_inst(LlirFieldStore{
                .object = std::move(ob),
                .field = memb->member.text,
                .value = std::move(rhs_use),
            });
          },
          [&](const std::unique_ptr<HirExprUnary> &u) {
            if (u->op.kind == TokenKind::Mul) {
              LlirOperand ptr =
                  materialize_primitive_or_pointer(emit_rvalue(*u->expr));
              emit_inst(LlirStore{.dest = std::move(ptr),
                                  .value = std::move(rhs_use)});
            }
          },
          [&](auto &) {},
      },
      lhs.item);
}

void Emitter::emit_block(const HirBlock &blk) {
  scope_push();
  for (const auto &s : blk.stmts)
    emit_stmt(s);
  scope_pop();
}

void Emitter::emit_stmt(const HirStmt &stmt) {
  std::visit(
      overloaded_ts{
          [&](const HirReturn &ret) {
            if (ret.expression) {
              LlirOperand v = materialize_primitive_or_pointer(
                  emit_rvalue(*ret.expression));
              emit_inst(LlirReturn{std::move(v)});
            } else
              emit_inst(LlirReturn{});
          },
          [&](const HirLet &let) {
            bool herr = false;
            Type slot_ty = type_void();
            if (let.explicit_type) {
              if (auto t = hir_to_type_llir(*let.explicit_type, cur_syms(),
                                            *all_syms, herr))
                slot_ty = std::move(*t);
              else if (let.type)
                slot_ty = clone_type_deep(*let.type);
              else
                slot_ty =
                    clone_type_deep(Type{PrimitiveType{PrimitiveKind::I32}});
            } else if (let.initializer && let.initializer->type)
              slot_ty = clone_type_deep(*let.initializer->type);
            else if (let.type)
              slot_ty = clone_type_deep(*let.type);
            else
              slot_ty =
                  clone_type_deep(Type{PrimitiveType{PrimitiveKind::I32}});

            std::string slot = let.name.text;
            emit_inst(
                LlirAlloca{.var_name = slot, .type = clone_type_deep(slot_ty)});
            bind(let.name.text, slot);
            if (let.initializer) {
              LlirOperand v = emit_rvalue(*let.initializer);
              if (type_skip_load_in_slot(slot_ty)) {
                LlirOperand dest =
                    local_slot_op(slot, clone_type_deep(slot_ty));
                emit_inst(
                    LlirStore{.dest = std::move(dest), .value = std::move(v)});
              } else {
                emit_inst(LlirStore{
                    .dest = local_slot_op(slot, clone_type_deep(slot_ty)),
                    .value = materialize_primitive_or_pointer(std::move(v)),
                });
              }
            }
          },
          [&](const HirAssign &assign) {
            LlirOperand r = emit_rvalue(assign.rvalue);
            if (assign.rvalue.type &&
                !type_skip_load_in_slot(*assign.rvalue.type))
              r = materialize_primitive_or_pointer(std::move(r));
            emit_store_to_lvalue(assign.lvalue, std::move(r));
          },
          [&](const HirExprStmt &es) { (void)emit_rvalue(es.expr); },
          [&](const HirBreak &) {
            if (!loops.empty())
              emit_inst(LlirBranch{.target_branch = loops.back().break_label});
          },
          [&](const HirContinue &) {
            if (!loops.empty())
              emit_inst(
                  LlirBranch{.target_branch = loops.back().continue_target});
          },
          [&](const std::unique_ptr<HirIf> &ifp) {
            const HirIf &i = *ifp;
            std::string then_l = fresh_label("then");
            std::string merge_l = fresh_label("endif");
            std::string else_l;
            LlirOperand c =
                materialize_primitive_or_pointer(emit_rvalue(i.condition));
            if (i.else_block) {
              else_l = fresh_label("else");
              emit_inst(LlirCondBranch{.condition = std::move(c),
                                       .true_label = then_l,
                                       .false_label = else_l});
            } else {
              emit_inst(LlirCondBranch{.condition = std::move(c),
                                       .true_label = then_l,
                                       .false_label = merge_l});
            }
            append_block(then_l);
            emit_block(i.then_block);
            if (!block_ends_with_terminal())
              emit_inst(LlirBranch{.target_branch = merge_l});
            if (i.else_block) {
              append_block(else_l);
              emit_block(*i.else_block);
              if (!block_ends_with_terminal())
                emit_inst(LlirBranch{.target_branch = merge_l});
            }
            append_block(merge_l);
          },
          [&](const std::unique_ptr<HirWhile> &wp) {
            const HirWhile &w = *wp;
            std::string hdr = fresh_label("wh");
            std::string body = fresh_label("whbody");
            std::string done = fresh_label("whdone");
            emit_inst(LlirBranch{.target_branch = hdr});
            append_block(hdr);
            LlirOperand cond =
                materialize_primitive_or_pointer(emit_rvalue(w.condition));
            emit_inst(LlirCondBranch{.condition = std::move(cond),
                                     .true_label = body,
                                     .false_label = done});
            append_block(body);
            loops.push_back({.continue_target = hdr, .break_label = done});
            emit_block(w.block);
            loops.pop_back();
            if (!block_ends_with_terminal())
              emit_inst(LlirBranch{.target_branch = hdr});
            append_block(done);
          },
          [&](const std::unique_ptr<HirFor> &fp) {
            const HirFor &fr = *fp;
            if (fr.init)
              emit_stmt(*fr.init);
            std::string hdr = fresh_label("forh");
            std::string body = fresh_label("forb");
            std::string inc = fresh_label("forinc");
            std::string done = fresh_label("fordone");
            emit_inst(LlirBranch{.target_branch = hdr});
            append_block(hdr);
            LlirOperand cond =
                materialize_primitive_or_pointer(emit_rvalue(fr.condition));
            emit_inst(LlirCondBranch{.condition = std::move(cond),
                                     .true_label = body,
                                     .false_label = done});
            append_block(body);
            loops.push_back({.continue_target = inc, .break_label = done});
            emit_block(fr.block);
            loops.pop_back();
            if (!block_ends_with_terminal())
              emit_inst(LlirBranch{.target_branch = inc});
            append_block(inc);
            if (fr.update)
              emit_stmt(*fr.update);
            emit_inst(LlirBranch{.target_branch = hdr});
            append_block(done);
          },
      },
      stmt.item);
}

void Emitter::lower_fn_body(const HirFnDef &hir_fn) {
  append_block("entry");
  scope_push();
  size_t ai = 0;
  bool herr = false;
  for (const auto &par : hir_fn.params) {
    Type pty = [&]() -> Type {
      if (auto pt = hir_to_type_llir(par.hir_type, cur_syms(), *all_syms, herr))
        return std::move(*pt);
      if (par.type)
        return clone_type_deep(*par.type);
      return clone_type_deep(Type{PrimitiveType{PrimitiveKind::I32}});
    }();

    emit_inst(
        LlirAlloca{.var_name = par.name.text, .type = clone_type_deep(pty)});
    bind(par.name.text, par.name.text);
    LlirOperand arg{.kind = LlirOperand::Register,
                    .data = std::format("arg{}", ai++),
                    .type = clone_type_deep(pty)};
    emit_inst(
        LlirStore{.dest = local_slot_op(par.name.text, clone_type_deep(pty)),
                  .value = std::move(arg)});
  }

  if (hir_fn.block) {
    for (const auto &st : hir_fn.block->stmts)
      emit_stmt(st);
  }

  if (!block_ends_with_terminal()) {
    if (is_void_type(return_ty))
      emit_inst(LlirReturn{});
    else
      emit_inst(LlirReturn{std::nullopt});
  }
  scope_pop();
}

auto literal_to_operand_data(const HirExprLiteral &lit) -> LlirOperandData {
  switch (lit.tok.kind) {
  case TokenKind::False:
    return false;
  case TokenKind::True:
    return true;
  case TokenKind::Null:
    return int64_t{0};
  case TokenKind::Integer: {
    int64_t v = 0;
    std::from_chars(lit.tok.text.data(),
                    lit.tok.text.data() + lit.tok.text.size(), v);
    return v;
  }
  case TokenKind::Real:
    return std::stod(lit.tok.text);
  case TokenKind::Char:
    return lit.tok.text.empty() ? char{0} : lit.tok.text[0];
  case TokenKind::String:
    return lit.tok.text;
  default:
    return int64_t{0};
  }
}

auto try_lower_global_let(const HirLet &gl, std::string_view mod_id)
    -> std::optional<LlirGlobal> {
  if (!gl.type || !gl.initializer)
    return std::nullopt;
  auto *hlit = std::get_if<HirExprLiteral>(&gl.initializer->item);
  if (!hlit)
    return std::nullopt;
  return LlirGlobal{.name = symbol_mangled(mod_id, gl.name.text),
                    .type = clone_type_deep(*gl.type),
                    .initial_value = literal_to_operand_data(*hlit),
                    .is_constant = gl.is_const};
}

auto lower_hir(const ModulesHir &modules) -> LlirModule {
  LlirModule mod{};
  std::unordered_map<std::string, ModuleSymbols> syms;
  for (const auto &[mid, items] : modules)
    syms[mid] = collect_module_symbols(items);

  for (const auto &[mid, items] : modules) {
    for (const auto &node : items) {
      if (auto *gl = std::get_if<HirLet>(&node)) {
        if (auto lg = try_lower_global_let(*gl, mid))
          mod.globals.push_back(std::move(*lg));
      }
    }
  }

  for (const auto &[mid, items] : modules) {
    for (const auto &node : items) {
      if (auto *f = std::get_if<HirFnDef>(&node)) {
        mod.functions.push_back(LlirFunction{});
        LlirFunction &lf = mod.functions.back();
        if (f->is_extern) {
          lf.name = f->name.text;
        } else {
          lf.name = symbol_mangled(mid, f->name.text);
        }
        lf.is_extern = f->is_extern;
        lf.is_variadic = f->is_variadic;
        lf.defining_module = mid;
        lf.return_type = function_return_type_llir(*f, syms.at(mid), syms);
        lf.params.reserve(f->params.size());
        bool herr_params = false;
        for (const auto &par : f->params) {
          Type pty = [&]() -> Type {
            if (auto pt =
                    hir_to_type_llir(par.hir_type, syms.at(mid), syms, herr_params))
              return clone_type_deep(*pt);
            if (par.type)
              return clone_type_deep(*par.type);
            return clone_type_deep(Type{PrimitiveType{PrimitiveKind::I32}});
          }();
          std::string td =
              par.type ? type_to_string(*par.type) : par.hir_type.to_string();
          lf.params.push_back(LlirParam{.name = par.name.text,
                                        .type_display = std::move(td),
                                        .type = std::move(pty)});
        }

        if (!f->is_extern) {
          Emitter em;
          em.all_syms = &syms;
          em.out = &mod;
          em.cur_module = mid;
          em.fn = &lf;
          em.return_ty = clone_type_deep(lf.return_type);
          em.lower_fn_body(*f);
        }
      }
    }
  }
  return mod;
}
