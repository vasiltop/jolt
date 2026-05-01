#include "llvm_backend.hpp"

#include "tokens.hpp"

#include <format>
#include <iostream>
#include <unordered_map>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>

#include <cstdlib>
#include <fstream>

namespace {

[[nodiscard]] auto symbol_mangled(std::string_view mod_id, std::string_view nm)
    -> std::string {
  return std::format("{}:{}", mod_id, nm);
}

[[nodiscard]] auto llvm_link_name(std::string_view mangled_jolt_symbol)
    -> std::string {
  std::string o(mangled_jolt_symbol);
  for (auto &c : o) {
    if (c == ':')
      c = '_';
  }
  return o;
}

[[nodiscard]] auto bb_safe_name(std::string_view label) -> std::string {
  std::string o(label);
  for (auto &c : o) {
    if (c == '.' || c == '%' || c == ':' || c == ' ')
      c = '_';
  }
  if (o.empty())
    return "bb";
  return o;
}

[[nodiscard]] auto reg_strip(std::string_view r) -> std::string {
  if (!r.empty() && r.front() == '%')
    return std::string(r.substr(1));
  return std::string(r);
}

struct ModuleSymbols {
  std::unordered_map<std::string, std::string> import_alias_to_target_module;
  std::unordered_map<std::string, const HirStruct *> structs_by_name;
  std::unordered_map<std::string, const HirEnum *> enums_by_name;
};

auto collect_module_symbols(const std::vector<Hir> &items) -> ModuleSymbols {
  ModuleSymbols ms;
  for (const auto &node : items)
    std::visit(
        [&](auto const &arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, HirStruct>)
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

auto is_array_ty_ll(const Type &t) -> bool {
  return std::holds_alternative<std::unique_ptr<ArrayType>>(t.data);
}

auto is_aggregate_storage_ty_ll(
    const Type &t, const ModuleSymbols &cur_mod_syms,
    const std::unordered_map<std::string, ModuleSymbols> &all_syms) -> bool {
  if (is_array_ty_ll(t))
    return true;
  auto *named = std::get_if<std::unique_ptr<NamedType>>(&t.data);
  if (!named || !(*named))
    return false;
  return resolve_named_struct(**named, cur_mod_syms, all_syms) != nullptr;
}

[[nodiscard]] auto fn_module_prefix(std::string_view mangled_full_name)
    -> std::string {
  auto pos = mangled_full_name.find(':');
  if (pos == std::string_view::npos || pos == 0)
    return "module";
  return std::string(mangled_full_name.substr(0, pos));
}

[[nodiscard]] auto fn_suffix_after_colon(std::string_view mangled_full_name)
    -> std::string {
  auto pos = mangled_full_name.find(':');
  if (pos == std::string_view::npos || pos + 1 >= mangled_full_name.size())
    return std::string(mangled_full_name);
  return std::string(mangled_full_name.substr(pos + 1));
}

template <class... Ts> struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

struct Backend {
  const ModulesHir *hir{};
  std::unordered_map<std::string, ModuleSymbols> syms{};
  std::unordered_map<const HirStruct *, std::string> struct_key{};
  std::unordered_map<const HirStruct *, llvm::StructType *> llvm_struct{};

  llvm::LLVMContext ctx{};
  std::unique_ptr<llvm::Module> mod{};
  llvm::IRBuilder<> b{ctx};

  void prime_symbols() {
    for (const auto &[mid, items] : *hir)
      syms[mid] = collect_module_symbols(items);
    for (const auto &[mid, items] : *hir) {
      for (const auto &node : items)
        if (auto *sr = std::get_if<HirStruct>(&node))
          struct_key[sr] = symbol_mangled(mid, sr->name.text);
    }
  }

  void define_struct_types() {
    for (const auto &[stptr, key] : struct_key) {
      llvm_struct[stptr] =
          llvm::StructType::create(ctx, llvm_link_name(std::string_view(key)));
    }
    for (const auto &[stptr, key] : struct_key) {
      (void)key;
      const std::string defining_mod = [&]() -> std::string {
        for (const auto &[mid, items] : *hir) {
          for (const auto &node : items)
            if (auto *sr = std::get_if<HirStruct>(&node))
              if (sr == stptr)
                return mid;
        }
        return fn_module_prefix(key);
      }();
      const auto &cur = syms.at(defining_mod);
      llvm::SmallVector<llvm::Type *> fields;
      for (const auto &fld : stptr->fields) {
        if (fld.type)
          fields.push_back(map_type(*fld.type, cur));
        else
          fields.push_back(llvm::Type::getInt32Ty(ctx));
      }
      llvm_struct[stptr]->setBody(fields);
    }
  }

  auto map_primitive(PrimitiveKind k) -> llvm::Type * {
    switch (k) {
    case PrimitiveKind::Void:
    case PrimitiveKind::Never:
      return llvm::Type::getVoidTy(ctx);
    case PrimitiveKind::Bool:
      return llvm::Type::getInt8Ty(ctx);
    case PrimitiveKind::Char:
      return llvm::Type::getInt8Ty(ctx);
    case PrimitiveKind::U8:
    case PrimitiveKind::I8:
      return llvm::Type::getInt8Ty(ctx);
    case PrimitiveKind::U16:
    case PrimitiveKind::I16:
      return llvm::Type::getInt16Ty(ctx);
    case PrimitiveKind::U32:
    case PrimitiveKind::I32:
      return llvm::Type::getInt32Ty(ctx);
    case PrimitiveKind::U64:
    case PrimitiveKind::I64:
      return llvm::Type::getInt64Ty(ctx);
    case PrimitiveKind::Real:
      return llvm::Type::getDoubleTy(ctx);
    case PrimitiveKind::String:
      return llvm::PointerType::getUnqual(ctx);
    case PrimitiveKind::Null:
      return llvm::PointerType::getUnqual(ctx);
    }
    return llvm::Type::getInt32Ty(ctx);
  }

  auto map_type(const Type &t, const ModuleSymbols &cur_syms) -> llvm::Type * {
    return std::visit(
        [&](const auto &arg) -> llvm::Type * {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, PrimitiveType>) {
            return map_primitive(arg.kind);
          } else if constexpr (std::is_same_v<T,
                                              std::unique_ptr<PointerType>>) {
            if (!arg || !arg->pointee)
              return llvm::PointerType::getUnqual(ctx);
            (void)map_type(*arg->pointee, cur_syms);
            return llvm::PointerType::getUnqual(ctx);
          } else if constexpr (std::is_same_v<T, std::unique_ptr<ArrayType>>) {
            if (!arg || !arg->element)
              return llvm::ArrayType::get(llvm::Type::getInt32Ty(ctx), 0);
            llvm::Type *elem = map_type(*arg->element, cur_syms);
            return llvm::ArrayType::get(elem, arg->size);
          } else if constexpr (std::is_same_v<T, std::unique_ptr<TupleType>>) {
            if (!arg || arg->elements.empty())
              return llvm::StructType::get(ctx);
            llvm::SmallVector<llvm::Type *> elts;
            for (const auto &et : arg->elements)
              elts.push_back(map_type(et, cur_syms));
            return llvm::StructType::get(ctx, elts);
          } else if constexpr (std::is_same_v<T,
                                              std::unique_ptr<FunctionType>>) {
            if (!arg)
              return llvm::PointerType::getUnqual(ctx);
            llvm::SmallVector<llvm::Type *> ps;
            for (auto &tp : arg->params)
              ps.push_back(map_type(tp, cur_syms));
            llvm::Type *ret = llvm::Type::getVoidTy(ctx);
            if (arg->return_type)
              ret = map_type(*arg->return_type, cur_syms);
            (void)llvm::FunctionType::get(ret, ps, false);
            return llvm::PointerType::getUnqual(ctx);
          } else if constexpr (std::is_same_v<T, std::unique_ptr<NamedType>>) {
            if (!arg)
              return llvm::Type::getInt64Ty(ctx);
            if (resolve_named_enum(*arg, cur_syms, syms))
              return llvm::Type::getInt32Ty(ctx);
            auto *stp = resolve_named_struct(*arg, cur_syms, syms);
            if (!stp)
              return llvm::Type::getInt64Ty(ctx);
            return llvm_struct.at(stp);
          }
          return llvm::Type::getInt64Ty(ctx);
        },
        t.data);
  }

  auto make_global_initializer(const LlirGlobal &gl, llvm::Type *gv_ty,
                               ModuleSymbols cur_syms) -> llvm::Constant * {
    (void)cur_syms;
    return std::visit(
        overloaded{
            [&](const std::string &s) -> llvm::Constant * {
              llvm::Constant *chars =
                  llvm::ConstantDataArray::getString(ctx, s, /*AddNull=*/true);
              auto *tmp = new llvm::GlobalVariable(
                  *mod, chars->getType(), gl.is_constant,
                  llvm::GlobalValue::PrivateLinkage, chars,
                  llvm_link_name(gl.name + ".str_body"));
              tmp->setUnnamedAddr(llvm::GlobalVariable::UnnamedAddr::Global);
              return llvm::ConstantExpr::getBitCast(
                  tmp, llvm::PointerType::getUnqual(ctx));
            },
            [&](int64_t v) -> llvm::Constant * {
              if (gv_ty->isIntegerTy())
                return llvm::ConstantInt::get(gv_ty, v, true);
              return llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), v,
                                            true);
            },
            [&](double v) -> llvm::Constant * {
              return llvm::ConstantFP::get(gv_ty, v);
            },
            [&](char v) -> llvm::Constant * {
              return llvm::ConstantInt::get(llvm::Type::getInt8Ty(ctx),
                                            static_cast<uint8_t>(v), false);
            },
            [&](bool v) -> llvm::Constant * {
              return llvm::ConstantInt::get(llvm::Type::getInt8Ty(ctx),
                                            v ? 1u : 0u, false);
            }},
        gl.initial_value);
  }

  void emit_globals(const LlirModule &ll) {
    for (const auto &gl : ll.globals) {
      ModuleSymbols cur = syms.at(fn_module_prefix(gl.name));
      llvm::Type *ty = map_type(gl.type, cur);
      auto *init = make_global_initializer(gl, ty, cur);
      auto *gv = new llvm::GlobalVariable(*mod, ty, gl.is_constant,
                                          llvm::GlobalValue::ExternalLinkage,
                                          init, llvm_link_name(gl.name));
      (void)gv;
    }
  }

  auto function_llvm_type(const LlirFunction &f, const ModuleSymbols &cur_syms)
      -> llvm::FunctionType * {
    llvm::SmallVector<llvm::Type *> ps;
    for (const auto &p : f.params)
      ps.push_back(map_type(p.type, cur_syms));
    llvm::Type *ret = llvm::Type::getVoidTy(ctx);
    auto *pv = std::get_if<PrimitiveType>(&f.return_type.data);
    if (!(pv && pv->kind == PrimitiveKind::Void))
      ret = map_type(f.return_type, cur_syms);
    return llvm::FunctionType::get(ret, ps, f.is_variadic);
  }

  void declare_functions(const LlirModule &ll) {
    for (const auto &f : ll.functions) {
      ModuleSymbols cur = syms.at(f.defining_module);
      llvm::Function *fn = llvm::Function::Create(
          function_llvm_type(f, cur), llvm::Function::ExternalLinkage,
          llvm_link_name(f.name), mod.get());
      (void)fn;
    }
  }

  auto struct_shape_of(const Type &root, const ModuleSymbols &cur_syms)
      -> const HirStruct * {
    const Type *t = &root;
    for (;;) {
      auto *ptr = std::get_if<std::unique_ptr<PointerType>>(&t->data);
      if (!ptr || !*ptr || !(*ptr)->pointee)
        break;
      t = (*ptr)->pointee.get();
    }
    auto *named = std::get_if<std::unique_ptr<NamedType>>(&t->data);
    if (!named || !(*named))
      return nullptr;
    return resolve_named_struct(**named, cur_syms, syms);
  }

  auto field_index(const HirStruct *st, std::string_view field) -> unsigned {
    for (unsigned i = 0; i < st->fields.size(); ++i)
      if (st->fields[i].name.text == field)
        return i;
    return 0;
  }

  auto i1_from(llvm::Value *v) -> llvm::Value * {
    llvm::Type *t = v->getType();

    if (t->isIntegerTy(1))
      return v;

    if (t->isIntegerTy())
      return b.CreateICmpNE(v, llvm::ConstantInt::get(t, 0));

    if (t->isFloatingPointTy())
      return b.CreateFCmpONE(v, llvm::ConstantFP::get(t, 0.0));

    if (t->isPointerTy()) {
      llvm::Type *intPtrTy = mod->getDataLayout().getIntPtrType(ctx);
      return b.CreateICmpNE(b.CreatePtrToInt(v, intPtrTy),
                            llvm::ConstantInt::get(intPtrTy, 0));
    }

    return llvm::ConstantInt::getFalse(ctx);
  }

  [[nodiscard]] auto llvm_array_semantic(const Type &op_ty,
                                         const ModuleSymbols &cur_syms)
      -> llvm::ArrayType * {
    auto *arr = std::get_if<std::unique_ptr<ArrayType>>(&op_ty.data);
    if (!arr || !*arr || !(*arr)->element)
      return nullptr;
    llvm::Type *el = map_type(*(*arr)->element, cur_syms);
    return llvm::ArrayType::get(el, (*arr)->size);
  }

  auto
  emit_operand_value(const LlirOperand &op, const ModuleSymbols &cur_syms,
                     std::unordered_map<std::string, llvm::Value *> &locals,
                     std::unordered_map<std::string, llvm::Value *> &regs)
      -> llvm::Value * {

    using OK = LlirOperand;
    if (op.kind == OK::Literal) {
      return std::visit(
          overloaded{
              [&](const std::string &s) -> llvm::Value * {
                return b.CreateGlobalString(s, ".jolt_str");
              },
              [&](int64_t v) -> llvm::Value * {
                llvm::Type *t = map_type(op.type, cur_syms);
                if (t->isIntegerTy())
                  return llvm::ConstantInt::get(t, v, /*Signed=*/true);
                return llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), v,
                                              true);
              },
              [&](double v) -> llvm::Value * {
                return llvm::ConstantFP::get(map_type(op.type, cur_syms), v);
              },
              [&](char cc) -> llvm::Value * {
                return llvm::ConstantInt::get(llvm::Type::getInt8Ty(ctx),
                                              static_cast<uint8_t>(cc), false);
              },
              [&](bool bb) -> llvm::Value * {
                return llvm::ConstantInt::get(llvm::Type::getInt8Ty(ctx),
                                              bb ? 1u : 0u, false);
              }},
          op.data);
    }
    if (op.kind == OK::Register) {
      return regs.at(reg_strip(std::get<std::string>(op.data)));
    }
    if (op.kind == OK::Local) {
      const std::string slot = std::get<std::string>(op.data);
      llvm::Value *ptr = locals.at(slot);
      llvm::Type *storage_ty = map_type(op.type, cur_syms);
      if (is_aggregate_storage_ty_ll(op.type, cur_syms, syms))
        return ptr;
      return b.CreateLoad(storage_ty, ptr);
    }
    if (op.kind == OK::Global) {
      const std::string nm = std::get<std::string>(op.data);
      llvm::GlobalVariable *glob = mod->getNamedGlobal(llvm_link_name(nm));
      if (!glob) {
        if (llvm::Function *fn = mod->getFunction(llvm_link_name(nm)))
          return fn;
        return llvm::ConstantPointerNull::get(
            llvm::PointerType::getUnqual(ctx));
      }
      llvm::Type *storage_ty = map_type(op.type, cur_syms);
      if (is_aggregate_storage_ty_ll(op.type, cur_syms, syms))
        return glob;
      return b.CreateLoad(storage_ty, glob);
    }
    return llvm::UndefValue::get(llvm::Type::getInt32Ty(ctx));
  }

  auto emit_lvalue_ptr(const LlirOperand &op, const ModuleSymbols &cur_syms,
                       std::unordered_map<std::string, llvm::Value *> &locals,
                       std::unordered_map<std::string, llvm::Value *> &regs)
      -> llvm::Value * {
    (void)regs;
    (void)cur_syms;
    if (op.kind == LlirOperand::Local) {
      return locals.at(std::get<std::string>(op.data));
    }
    if (op.kind == LlirOperand::Global) {
      return mod->getNamedGlobal(
          llvm_link_name(std::get<std::string>(op.data)));
    }
    return nullptr;
  }

  [[nodiscard]] auto widen_cmp_to_scalar(llvm::Value *cond, llvm::Type *wanted)
      -> llvm::Value * {
    if (!wanted || cond->getType() == wanted)
      return cond;
    if (cond->getType()->isIntegerTy(1) && !wanted->isIntegerTy(1)) {
      if (wanted->isIntegerTy(8))
        return b.CreateZExt(cond, wanted);
      if (wanted->isIntegerTy(32))
        return b.CreateZExt(cond, wanted);
      return b.CreateZExt(cond, llvm::Type::getInt32Ty(ctx));
    }
    return cond;
  }

  auto emit_bin(LlirBinaryOp::Kind k, llvm::Value *L, llvm::Value *R,
                const LlirOperand *maybe_dest_hint) -> llvm::Value * {
    (void)maybe_dest_hint;
    const bool fp = L->getType()->isFloatingPointTy();
    if (fp) {
      switch (k) {
      case LlirBinaryOp::Add:
        return b.CreateFAdd(L, R);
      case LlirBinaryOp::Sub:
        return b.CreateFSub(L, R);
      case LlirBinaryOp::Mul:
        return b.CreateFMul(L, R);
      case LlirBinaryOp::Div:
        return b.CreateFDiv(L, R);
      case LlirBinaryOp::Mod:
        return b.CreateFRem(L, R);
      case LlirBinaryOp::Eq:
        return b.CreateFCmpOEQ(L, R);
      case LlirBinaryOp::Ne:
        return b.CreateFCmpONE(L, R);
      case LlirBinaryOp::Lt:
        return b.CreateFCmpOLT(L, R);
      case LlirBinaryOp::Gt:
        return b.CreateFCmpOGT(L, R);
      case LlirBinaryOp::Le:
        return b.CreateFCmpOLE(L, R);
      case LlirBinaryOp::Ge:
        return b.CreateFCmpOGE(L, R);
      default:
        break;
      }
      return L;
    }
    switch (k) {
    case LlirBinaryOp::Add:
      return b.CreateAdd(L, R);
    case LlirBinaryOp::Sub:
      return b.CreateSub(L, R);
    case LlirBinaryOp::Mul:
      return b.CreateMul(L, R);
    case LlirBinaryOp::Div:
      return b.CreateSDiv(L, R);
    case LlirBinaryOp::Mod:
      return b.CreateSRem(L, R);
    case LlirBinaryOp::And:
      return b.CreateAnd(L, R);
    case LlirBinaryOp::Or:
      return b.CreateOr(L, R);
    case LlirBinaryOp::Xor:
      return b.CreateXor(L, R);
    case LlirBinaryOp::Shl:
      return b.CreateShl(L, R);
    case LlirBinaryOp::Shr:
      return b.CreateAShr(L, R);
    case LlirBinaryOp::Eq:
      return b.CreateICmpEQ(L, R);
    case LlirBinaryOp::Ne:
      return b.CreateICmpNE(L, R);
    case LlirBinaryOp::Lt:
      return b.CreateICmpSLT(L, R);
    case LlirBinaryOp::Gt:
      return b.CreateICmpSGT(L, R);
    case LlirBinaryOp::Le:
      return b.CreateICmpSLE(L, R);
    case LlirBinaryOp::Ge:
      return b.CreateICmpSGE(L, R);
    }
    return L;
  }

  auto emit_cast(llvm::Value *v, llvm::Type *dst_t, const LlirOperand &src_op)
      -> llvm::Value * {
    (void)src_op;
    llvm::Type *src_t = v->getType();
    if (src_t == dst_t)
      return v;
    if (src_t->isIntegerTy() && dst_t->isIntegerTy())
      return b.CreateIntCast(v, dst_t, /*Signed=*/true);
    if (src_t->isIntegerTy() && dst_t->isFloatingPointTy())
      return b.CreateSIToFP(v, dst_t);
    if (src_t->isFloatingPointTy() && dst_t->isIntegerTy())
      return b.CreateFPToSI(v, dst_t);
    if (src_t->isFloatingPointTy() && dst_t->isFloatingPointTy())
      return b.CreateFPCast(v, dst_t);
    if (src_t->isPointerTy() && dst_t->isPointerTy())
      return b.CreateBitCast(v, dst_t);
    if (src_t->isPointerTy() && dst_t->isIntegerTy())
      return b.CreatePtrToInt(v, dst_t);
    if (src_t->isIntegerTy() && dst_t->isPointerTy())
      return b.CreateIntToPtr(v, dst_t);
    return v;
  }

  auto materialize_for_store(llvm::Type *dst_ty, llvm::Value *raw)
      -> llvm::Value * {
    if (!dst_ty || !raw)
      return raw;
    const bool dst_agg = llvm::isa<llvm::StructType>(dst_ty) ||
                         llvm::isa<llvm::ArrayType>(dst_ty);
    if (!dst_agg)
      return raw;
    llvm::Type *rv = raw->getType();
    if (rv == dst_ty)
      return raw;
    if (rv->isPointerTy())
      return b.CreateLoad(dst_ty, raw);
    return raw;
  }

  void emit_llir_store(const LlirStore &st, const ModuleSymbols &cur_syms,
                       std::unordered_map<std::string, llvm::Value *> &locals,
                       std::unordered_map<std::string, llvm::Value *> &regs) {
    llvm::Value *dst_ptr = nullptr;
    if (st.dest.kind == LlirOperand::Local)
      dst_ptr = locals.at(std::get<std::string>(st.dest.data));
    else if (st.dest.kind == LlirOperand::Global)
      dst_ptr = mod->getNamedGlobal(
          llvm_link_name(std::get<std::string>(st.dest.data)));
    else if (st.dest.kind == LlirOperand::Register) {
      auto reg_name = std::get<std::string>(st.dest.data);
      dst_ptr = regs.at(reg_strip(reg_name));
    } else
      return;
    llvm::Type *dst_ty = map_type(st.dest.type, cur_syms);
    llvm::Value *raw = emit_operand_value(st.value, cur_syms, locals, regs);
    llvm::Value *val = materialize_for_store(dst_ty, raw);
    b.CreateStore(val, dst_ptr);
  }

  void compile_function(const LlirFunction &lf) {
    if (lf.is_extern)
      return;
    llvm::Function *fn = mod->getFunction(llvm_link_name(lf.name));
    if (!fn)
      return;

    const std::string mod_id = lf.defining_module;
    const ModuleSymbols &cur = syms.at(mod_id);

    std::unordered_map<std::string, llvm::BasicBlock *> label_to_bb{};
    for (const auto &blk : lf.blocks)
      label_to_bb[blk.label] = llvm::BasicBlock::Create(
          ctx, bb_safe_name(std::string_view(blk.label)), fn);

    std::unordered_map<std::string, llvm::Value *> locals{};
    std::unordered_map<std::string, llvm::Value *> regs{};

    unsigned ai = 0;
    for (auto &Arg : fn->args())
      regs["arg" + std::to_string(ai++)] = &Arg;

    for (const auto &blk : lf.blocks) {
      llvm::BasicBlock *bb = label_to_bb.at(blk.label);
      b.SetInsertPoint(bb);

      for (const auto &ins : blk.instructions) {
        std::visit(
            overloaded{
                [&](const LlirAlloca &x) {
                  llvm::Type *ty = map_type(x.type, cur);
                  locals[x.var_name] = b.CreateAlloca(ty, nullptr, x.var_name);
                },
                [&](const LlirStore &x) {
                  emit_llir_store(x, cur, locals, regs);
                },
                [&](const LlirLoad &x) {
                  llvm::Value *ptr = emit_lvalue_ptr(x.src, cur, locals, regs);
                  if (ptr) {
                    llvm::Type *dest_ty = map_type(x.src.type, cur);
                    regs[reg_strip(x.dest_reg)] = b.CreateLoad(dest_ty, ptr);
                  } else {
                    llvm::Value *v = emit_operand_value(x.src, cur, locals, regs);
                    if (v->getType()->isPointerTy()) {
                      llvm::Type *dest_ty = nullptr;
                      if (auto *pee = std::get_if<std::unique_ptr<PointerType>>(&x.src.type.data)) {
                        if (*pee && (*pee)->pointee) {
                          dest_ty = map_type(*(*pee)->pointee, cur);
                        }
                      }
                      if (!dest_ty) {
                        dest_ty = map_type(x.src.type, cur);
                      }
                      regs[reg_strip(x.dest_reg)] = b.CreateLoad(dest_ty, v);
                    } else {
                      regs[reg_strip(x.dest_reg)] = v;
                    }
                  }
                },
                [&](const LlirAddrOf &x) {
                  regs[reg_strip(x.dest_reg)] = locals.at(x.local_slot);
                },
                [&](const LlirIndexedStore &x) {
                  llvm::Value *base =
                      emit_lvalue_ptr(x.array_slot, cur, locals, regs);
                  if (!base)
                    base = emit_operand_value(x.array_slot, cur, locals, regs);
                  llvm::ArrayType *arr_ty =
                      llvm_array_semantic(x.array_slot.type, cur);
                  if (!arr_ty)
                    return;
                  llvm::Value *idx =
                      emit_operand_value(x.index, cur, locals, regs);
                  llvm::Value *ep = b.CreateInBoundsGEP(
                      arr_ty, base,
                      {llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 0),
                       idx});
                  llvm::Type *el_ty = arr_ty->getElementType();
                  llvm::Value *val =
                      emit_operand_value(x.value, cur, locals, regs);
                  val = materialize_for_store(el_ty, val);
                  b.CreateStore(val, ep);
                },
                [&](const LlirFieldLoad &fl) {
                  const HirStruct *hst = struct_shape_of(fl.object.type, cur);
                  if (!hst)
                    return;
                  llvm::StructType *sty = llvm_struct.at(hst);
                  const unsigned fidx = field_index(hst, fl.field);
                  llvm::Type *fty = sty->getElementType(fidx);
                  llvm::Value *obj =
                      emit_operand_value(fl.object, cur, locals, regs);
                  llvm::Value *loaded = nullptr;
                  if (obj->getType() == sty)
                    loaded = b.CreateExtractValue(obj, {fidx});
                  else {

                    llvm::Value *bp =
                        emit_lvalue_ptr(fl.object, cur, locals, regs);

                    llvm::Value *base = bp ? bp : obj;
                    llvm::Value *fp = b.CreateStructGEP(sty, base, fidx);
                    loaded = b.CreateLoad(fty, fp);
                  }
                  regs[reg_strip(fl.dest_reg)] = loaded;
                },
                [&](const LlirFieldStore &fs) {
                  const HirStruct *hst = struct_shape_of(fs.object.type, cur);
                  if (!hst)
                    return;
                  llvm::StructType *sty = llvm_struct.at(hst);
                  const unsigned fidx = field_index(hst, fs.field);
                  llvm::Type *fty = sty->getElementType(fidx);
                  llvm::Value *val =
                      emit_operand_value(fs.value, cur, locals, regs);
                  val = materialize_for_store(fty, val);

                  if (llvm::Value *agg_ptr =
                          emit_lvalue_ptr(fs.object, cur, locals, regs)) {

                    llvm::Value *fp = b.CreateStructGEP(sty, agg_ptr, fidx);
                    b.CreateStore(val, fp);
                    return;
                  }

                  llvm::Value *objv =
                      emit_operand_value(fs.object, cur, locals, regs);
                  if (objv->getType() == sty) {
                    (void)b.CreateInsertValue(objv, val, {fidx});
                  }
                },
                [&](const LlirFieldAddr &fa) {
                  const HirStruct *hst = struct_shape_of(fa.object.type, cur);
                  if (!hst)
                    return;
                  llvm::StructType *sty = llvm_struct.at(hst);
                  const unsigned fidx = field_index(hst, fa.field);
                  llvm::Value *agg_ptr =
                      emit_lvalue_ptr(fa.object, cur, locals, regs);
                  if (!agg_ptr)
                    return;
                  regs[reg_strip(fa.dest_reg)] =
                      b.CreateStructGEP(sty, agg_ptr, fidx);
                },
                [&](const LlirBinaryOp &bp) {
                  llvm::Value *l =
                      emit_operand_value(bp.lhs, cur, locals, regs);
                  llvm::Value *r =
                      emit_operand_value(bp.rhs, cur, locals, regs);
                  if (l->getType()->isIntegerTy() &&
                      r->getType()->isFloatingPointTy())
                    l = b.CreateSIToFP(l, r->getType());
                  else if (l->getType()->isFloatingPointTy() &&
                           r->getType()->isIntegerTy())
                    r = b.CreateSIToFP(r, l->getType());
                  llvm::Value *out = emit_bin(bp.op, l, r, nullptr);
                  if (out->getType()->isIntegerTy(1)) {
                    llvm::Type *dst_ty = map_type(bp.rhs.type, cur);
                    out = widen_cmp_to_scalar(out, dst_ty);
                  }
                  regs[reg_strip(bp.dest_reg)] = out;
                },
                [&](const LlirUnaryOp &u) {
                  llvm::Value *in =
                      emit_operand_value(u.src, cur, locals, regs);
                  llvm::Type *src_ty = map_type(u.src.type, cur);
                  if (u.op == LlirUnaryOp::Neg) {
                    if (in->getType()->isFloatingPointTy())
                      regs[reg_strip(u.dest_reg)] = b.CreateFNeg(in);
                    else
                      regs[reg_strip(u.dest_reg)] = b.CreateNeg(in);
                  } else {
                    if (in->getType()->isFloatingPointTy()) {
                      llvm::Value *cmp = b.CreateFCmpONE(
                          in, llvm::ConstantFP::get(in->getType(), 0.0));
                      regs[reg_strip(u.dest_reg)] =
                          widen_cmp_to_scalar(cmp, llvm::Type::getInt8Ty(ctx));
                    } else if (in->getType()->isIntegerTy(8) &&
                               src_ty->isIntegerTy(8)) {
                      regs[reg_strip(u.dest_reg)] = b.CreateXor(
                          in, llvm::ConstantInt::get(in->getType(), 1));
                    } else if (in->getType()->isIntegerTy()) {
                      regs[reg_strip(u.dest_reg)] = b.CreateNot(in);
                    } else {
                      llvm::Value *cmp = i1_from(in);
                      regs[reg_strip(u.dest_reg)] =
                          widen_cmp_to_scalar(cmp, src_ty);
                    }
                  }
                },
                [&](const LlirBranch &br) {
                  b.CreateBr(label_to_bb.at(br.target_branch));
                },
                [&](const LlirCondBranch &cb) {
                  llvm::Value *c =
                      emit_operand_value(cb.condition, cur, locals, regs);
                  b.CreateCondBr(i1_from(c), label_to_bb.at(cb.true_label),
                                 label_to_bb.at(cb.false_label));
                },
                [&](const LlirReturn &ret) {
                  llvm::Type *want = map_type(lf.return_type, cur);
                  if (!ret.value) {
                    if (want->isVoidTy())
                      b.CreateRetVoid();
                    else
                      b.CreateRet(llvm::UndefValue::get(want));
                    return;
                  }
                  llvm::Value *v =
                      emit_operand_value(*ret.value, cur, locals, regs);
                  v = emit_cast(v, want, *ret.value);
                  b.CreateRet(v);
                },
                [&](const LlirGetElement &ge) {
                  llvm::Value *base =
                      emit_lvalue_ptr(ge.base, cur, locals, regs);
                  if (!base)
                    base = emit_operand_value(ge.base, cur, locals, regs);
                  llvm::ArrayType *arr_ty =
                      llvm_array_semantic(ge.base.type, cur);
                  if (!arr_ty)

                    return;
                  llvm::Value *idx =
                      emit_operand_value(ge.index, cur, locals, regs);
                  llvm::Value *ep = b.CreateInBoundsGEP(
                      arr_ty, base,

                      {llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 0),

                       idx});

                  llvm::Type *ety = arr_ty->getElementType();

                  regs[reg_strip(ge.dest_reg)] = b.CreateLoad(ety, ep);
                },
                [&](const LlirCall &call) {
                  llvm::Function *callee =
                      mod->getFunction(llvm_link_name(call.func_name));
                  if (!callee) {
                    if (call.dest_reg.has_value())

                      regs[reg_strip(*call.dest_reg)] =
                          llvm::UndefValue::get(llvm::Type::getInt32Ty(ctx));
                    return;
                  }

                  llvm::SmallVector<llvm::Value *> args{};
                  llvm::SmallVector<llvm::Type *> want{};
                  for (llvm::Argument &a : callee->args())
                    want.push_back(a.getType());
                  for (size_t i = 0; i < call.args.size(); ++i) {
                    llvm::Value *av =
                        emit_operand_value(call.args[i], cur, locals, regs);
                    if (i < want.size() && av->getType() != want[i])
                      av = emit_cast(av, want[i], call.args[i]);
                    args.push_back(av);
                  }
                  llvm::Type *retty =
                      callee->getFunctionType()->getReturnType();
                  llvm::Value *r = b.CreateCall(callee, args);
                  if (!retty->isVoidTy() && call.dest_reg.has_value())
                    regs[reg_strip(*call.dest_reg)] = r;
                },
                [&](const LlirCast &ca) {
                  llvm::Value *sv =
                      emit_operand_value(ca.source, cur, locals, regs);
                  llvm::Type *dt = map_type(ca.target_type, cur);

                  regs[reg_strip(ca.dest_reg)] = emit_cast(sv, dt, ca.source);
                }},
            ins);
      }
    }
  }

  void maybe_emit_c_main(const LlirModule &ll) {
    const LlirFunction *candidate = nullptr;
    for (const auto &f : ll.functions) {
      if (fn_suffix_after_colon(f.name) != "main")
        continue;
      if (!f.params.empty())
        continue;
      candidate = &f;
      break;
    }
    if (!candidate)
      return;
    llvm::Type *i32 = llvm::Type::getInt32Ty(ctx);
    llvm::FunctionType *fty = llvm::FunctionType::get(i32, {}, false);
    llvm::Function *main_fn = llvm::Function::Create(
        fty, llvm::Function::ExternalLinkage, "main", mod.get());
    llvm::BasicBlock *bb = llvm::BasicBlock::Create(ctx, "entry", main_fn);
    b.SetInsertPoint(bb);
    llvm::Function *target = mod->getFunction(llvm_link_name(candidate->name));
    if (!target) {
      b.CreateRet(llvm::ConstantInt::get(i32, 1, true));
      return;
    }
    llvm::Type *tgt_ret = target->getFunctionType()->getReturnType();
    if (tgt_ret->isVoidTy()) {
      b.CreateCall(target);
      b.CreateRet(llvm::ConstantInt::get(i32, 0, true));

    } else {
      llvm::Value *r = b.CreateCall(target);
      if (r->getType() != i32)
        r = b.CreateIntCast(r, i32, /*isSigned=*/true);
      b.CreateRet(r);
    }
  }

  void lower(const LlirModule &ll) {
    prime_symbols();
    mod = std::make_unique<llvm::Module>("jolt", ctx);
    define_struct_types();
    emit_globals(ll);
    declare_functions(ll);
    for (const auto &f : ll.functions)
      compile_function(f);
    maybe_emit_c_main(ll);
  }
};

auto verify_and_print(llvm::Module &m) -> bool {
  std::string errs;
  llvm::raw_string_ostream os(errs);

  std::error_code ec;
  llvm::raw_fd_ostream file_os("output.ll", ec);
  if (!ec) {
    m.print(file_os, nullptr);
  }

  m.print(llvm::outs(), nullptr);

  if (llvm::verifyModule(m, &os)) {
    std::cerr << errs;
    return false;
  }

  return true;
}

void emit_llvm_ir_impl(const LlirModule &llir, const ModulesHir &modules) {
  Backend be;
  be.hir = &modules;
  be.lower(llir);
  if (verify_and_print(*be.mod)) {
    std::system("clang output.ll -o a.out");
  }
}

} // namespace

void jolt_emit_llvm_ir(const LlirModule &llir, const ModulesHir &modules) {
  emit_llvm_ir_impl(llir, modules);
}
