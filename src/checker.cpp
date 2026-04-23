#include "checker.hpp"
#include "types.hpp"
#include <algorithm>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

template <typename T> struct is_unique_ptr : std::false_type {};

template <typename T>
struct is_unique_ptr<std::unique_ptr<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_unique_ptr_v = is_unique_ptr<T>::value;

static auto type_from_primitive_kind(PrimitiveKind kind) -> Type {
  return Type{.data = PrimitiveType{.kind = kind}};
}

auto primitive_from_text(std::string_view s) -> std::optional<PrimitiveKind> {
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

auto is_integral_primitive(PrimitiveKind k) -> bool {
  switch (k) {
  case PrimitiveKind::I8:
  case PrimitiveKind::I16:
  case PrimitiveKind::I32:
  case PrimitiveKind::I64:
  case PrimitiveKind::U8:
  case PrimitiveKind::U16:
  case PrimitiveKind::U32:
  case PrimitiveKind::U64:
    return true;
  default:
    return false;
  }
}

auto is_numeric_primitive(PrimitiveKind k) -> bool {
  return is_integral_primitive(k) || k == PrimitiveKind::Real;
}

auto is_integral(const Type &t) -> bool {
  const auto *p = std::get_if<PrimitiveType>(&t.data);
  return p && is_integral_primitive(p->kind);
}

auto is_numeric(const Type &t) -> bool {
  const auto *p = std::get_if<PrimitiveType>(&t.data);
  return p && is_numeric_primitive(p->kind);
}

static constexpr Pos kUnknownPos{1, 1, 0};

static auto hir_type_start_pos(const HirType &ht) -> Pos {
  return std::visit(
      [](auto &&arg) -> Pos {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, HirTypePath>) {
          if (arg.path.empty())
            return kUnknownPos;
          return arg.path[0].pos;
        } else if constexpr (std::is_same_v<T, std::unique_ptr<HirTypePtr>>) {
          if (!arg || !arg->base)
            return kUnknownPos;
          return hir_type_start_pos(*arg->base);
        }
        return kUnknownPos;
      },
      ht.item);
}

static auto expr_start_pos(const HirExpr &expr) -> Pos {
  return std::visit(
      [](auto &&arg) -> Pos {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, HirExprLiteral>) {
          return arg.tok.pos;
        } else if constexpr (std::is_same_v<T, HirExprPath>) {
          if (arg.segments.empty())
            return kUnknownPos;
          return arg.segments[0].pos;
        } else if constexpr (std::is_same_v<T,
                                            std::unique_ptr<HirExprBinary>>) {
          return expr_start_pos(*arg->lhs);
        } else if constexpr (std::is_same_v<T, std::unique_ptr<HirExprUnary>>) {
          return arg->op.pos;
        } else if constexpr (std::is_same_v<T, std::unique_ptr<HirExprCall>>) {
          return expr_start_pos(*arg->callee);
        } else if constexpr (std::is_same_v<T, std::unique_ptr<HirExprIndex>>) {
          return expr_start_pos(*arg->value);
        } else if constexpr (std::is_same_v<T,
                                            std::unique_ptr<HirExprMember>>) {
          return expr_start_pos(*arg->object);
        } else if constexpr (std::is_same_v<T, std::unique_ptr<HirExprAs>>) {
          return expr_start_pos(*arg->expr);
        } else if constexpr (std::is_same_v<T, std::unique_ptr<HirExprArray>>) {
          if (arg->elements.empty())
            return kUnknownPos;
          return expr_start_pos(arg->elements[0]);
        } else if constexpr (std::is_same_v<T,
                                            std::unique_ptr<HirExprStruct>>) {
          return hir_type_start_pos(arg->hir_type);
        }
        return kUnknownPos;
      },
      expr.item);
}

auto token_is_comparison(TokenKind k) -> bool {
  switch (k) {
  case TokenKind::LessThan:
  case TokenKind::GreaterThan:
  case TokenKind::LessThanEqual:
  case TokenKind::GreaterThanEqual:
  case TokenKind::Equal:
  case TokenKind::NotEqual:
    return true;
  default:
    return false;
  }
}

auto clone_type_impl(const Type &ty) -> Type {
  return std::visit(
      [&](const auto &arg) -> Type {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, PrimitiveType>) {
          return Type{arg};
        } else if constexpr (std::is_same_v<T, std::unique_ptr<PointerType>>) {
          auto p = std::make_unique<PointerType>();
          p->pointee = std::make_unique<Type>(clone_type_impl(*arg->pointee));
          return Type{.data = std::move(p)};
        } else if constexpr (std::is_same_v<T, std::unique_ptr<ArrayType>>) {
          auto a = std::make_unique<ArrayType>();
          a->element = std::make_unique<Type>(clone_type_impl(*arg->element));
          a->size = arg->size;
          return Type{.data = std::move(a)};
        } else if constexpr (std::is_same_v<T, std::unique_ptr<TupleType>>) {
          auto u = std::make_unique<TupleType>();
          for (const auto &e : arg->elements)
            u->elements.push_back(clone_type_impl(e));
          return Type{.data = std::move(u)};
        } else if constexpr (std::is_same_v<T, std::unique_ptr<FunctionType>>) {
          auto f = std::make_unique<FunctionType>();
          for (const auto &p : arg->params)
            f->params.push_back(clone_type_impl(p));
          if (arg->return_type)
            f->return_type =
                std::make_unique<Type>(clone_type_impl(*arg->return_type));
          return Type{.data = std::move(f)};
        } else if constexpr (std::is_same_v<T, std::unique_ptr<NamedType>>) {
          auto n = std::make_unique<NamedType>();
          n->path = arg->path;
          return Type{.data = std::move(n)};
        }

        // Unreachable
        return type_from_primitive_kind(PrimitiveKind::I32);
      },
      ty.data);
}

auto type_from_expr_item(const HirExprItem &item) -> std::optional<Type> {
  return std::visit(
      [](const auto &arg) -> std::optional<Type> {
        using T = std::decay_t<decltype(arg)>;

        const auto *node = [&]() -> const auto * {
          if constexpr (is_unique_ptr_v<T>)
            return arg.get();
          else
            return &arg;
        }();

        if (node && node->type)
          return clone_type_impl(*node->type);

        return std::nullopt;
      },
      item);
}

auto element_type_for_indexing(const Type &value_ty) -> std::optional<Type> {
  if (const auto *a = std::get_if<std::unique_ptr<ArrayType>>(&value_ty.data)) {
    return clone_type_impl(*(*a)->element);
  }
  return std::nullopt;
}

auto peel_pointer(const Type &t) -> const Type * {
  if (const auto *p = std::get_if<std::unique_ptr<PointerType>>(&t.data))
    return (*p)->pointee.get();
  return nullptr;
}

auto is_null_literal_type(const Type &t) -> bool {
  const auto *p = std::get_if<PrimitiveType>(&t.data);
  return p && p->kind == PrimitiveKind::Null;
}

auto is_pointer_semantic_type(const Type &t) -> bool {
  return std::holds_alternative<std::unique_ptr<PointerType>>(t.data);
}

auto strip_pointer_layers(Type t) -> Type {
  while (const Type *inner = peel_pointer(t))
    t = clone_type_impl(*inner);
  return t;
}

auto Checker::clone_type(const Type &t) -> Type { return clone_type_impl(t); }

auto Checker::types_compatible(const Type &a, const Type &b) -> bool {
  if (type_to_string(a) == type_to_string(b))
    return true;
  if (is_null_literal_type(a) && is_pointer_semantic_type(b))
    return true;
  if (is_null_literal_type(b) && is_pointer_semantic_type(a))
    return true;
  return false;
}

auto Checker::lookup_let(Scope &scope, std::string_view name) -> HirLet * {
  for (Scope *p = &scope; p; p = p->parent) {
    auto it = p->variables.find(std::string(name));
    if (it != p->variables.end())
      return it->second;
  }
  return nullptr;
}

auto Checker::lookup_param(Scope &scope, std::string_view name)
    -> HirTypedIdent * {
  for (Scope *p = &scope; p; p = p->parent) {
    auto it = p->fn_params.find(std::string(name));
    if (it != p->fn_params.end())
      return it->second;
  }
  return nullptr;
}

auto Checker::lookup_fn(Scope &scope, std::string_view name) -> HirFnDef * {
  for (Scope *p = &scope; p; p = p->parent) {
    auto it = p->functions.find(std::string(name));
    if (it != p->functions.end())
      return it->second;
  }
  return nullptr;
}

auto Checker::lookup_struct(Scope &scope, std::string_view name)
    -> HirStruct * {
  for (Scope *p = &scope; p; p = p->parent) {
    auto it = p->structs.find(std::string(name));
    if (it != p->structs.end())
      return it->second;
  }
  return nullptr;
}

auto Checker::lookup_enum(Scope &scope, std::string_view name) -> HirEnum * {
  for (Scope *p = &scope; p; p = p->parent) {
    auto it = p->enums.find(std::string(name));
    if (it != p->enums.end())
      return it->second;
  }
  return nullptr;
}

auto Checker::hir_to_type(const HirType &ht, std::vector<Error> &errors,
                          const Pos &pos, Scope &scope) -> std::optional<Type> {
  return std::visit(
      [this, &errors, &pos, &scope](auto &&arg) -> std::optional<Type> {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, HirTypePath>) {
          std::vector<std::string> path;
          path.reserve(arg.path.size());
          for (const auto &tok : arg.path)
            path.push_back(tok.text);
          if (path.size() == 1) {
            if (auto pk = primitive_from_text(path[0]))
              return type_from_primitive_kind(*pk);
            if (lookup_struct(scope, path[0]) || lookup_enum(scope, path[0]))
              return Type{std::make_unique<NamedType>(
                  NamedType{.path = std::move(path)})};
            this->add_error(errors, pos,
                            std::format("unknown type `{}`", path[0]));
            return std::nullopt;
          }
          return Type{
              std::make_unique<NamedType>(NamedType{.path = std::move(path)})};
        } else if constexpr (std::is_same_v<T, std::unique_ptr<HirTypePtr>>) {
          if (!arg || !arg->base)
            return std::nullopt;
          auto inner = this->hir_to_type(*arg->base, errors, pos, scope);
          if (!inner)
            return std::nullopt;
          auto p = std::make_unique<PointerType>();
          p->pointee = std::make_unique<Type>(std::move(*inner));
          return Type{.data = std::move(p)};
        }
        return std::nullopt;
      },
      ht.item);
}

auto Checker::function_type_for(const HirFnDef &fn, std::vector<Error> &errors,
                                Scope &scope) -> std::optional<Type> {
  std::vector<Type> params;
  for (const auto &p : fn.params) {
    auto t = hir_to_type(p.hir_type, errors, p.name.pos, scope);
    if (!t)
      return std::nullopt;
    params.push_back(std::move(*t));
  }
  std::unique_ptr<Type> ret_ty;
  if (fn.return_type) {
    auto t = hir_to_type(*fn.return_type, errors, fn.name.pos, scope);
    if (!t)
      return std::nullopt;
    ret_ty = std::make_unique<Type>(std::move(*t));
  } else {
    ret_ty =
        std::make_unique<Type>(type_from_primitive_kind(PrimitiveKind::Void));
  }
  auto ft = std::make_unique<FunctionType>();
  ft->params = std::move(params);
  ft->return_type = std::move(ret_ty);
  return Type{.data = std::move(ft)};
}

auto Checker::resolve_struct_named(const NamedType &named, Scope &scope)
    -> HirStruct * {
  if (named.path.empty())
    return nullptr;
  if (named.path.size() == 1)
    return lookup_struct(scope, named.path[0]);
  if (!all_modules_)
    return nullptr;
  const size_t n = named.path.size();
  std::string mod_id;
  for (size_t i = 0; i < n - 1; ++i) {
    if (i)
      mod_id += "::";
    mod_id += named.path[i];
  }
  // TODO: Handle relative resolutions
  if (all_modules_->contains(mod_id)) {
    const std::string &item = named.path[n - 1];
    auto sit = module_global_scope.find(mod_id);
    if (sit == module_global_scope.end())
      return nullptr;
    return lookup_struct(*sit->second, item);
  }
  return nullptr;
}

auto Checker::resolve_enum_named(const NamedType &named, Scope &scope)
    -> HirEnum * {
  if (named.path.empty())
    return nullptr;
  if (named.path.size() == 1)
    return lookup_enum(scope, named.path[0]);
  if (!all_modules_)
    return nullptr;
  const size_t n = named.path.size();
  std::string mod_id;
  for (size_t i = 0; i < n - 1; ++i) {
    if (i)
      mod_id += "::";
    mod_id += named.path[i];
  }
  if (all_modules_->contains(mod_id)) {
    const std::string &item = named.path[n - 1];
    auto sit = module_global_scope.find(mod_id);
    if (sit == module_global_scope.end())
      return nullptr;
    return lookup_enum(*sit->second, item);
  }
  return nullptr;
}

auto Checker::type_of_field(HirStruct *st, std::string_view field,
                            std::vector<Error> &errors, const Pos &pos,
                            Scope &scope) -> std::optional<Type> {
  for (const auto &f : st->fields) {
    if (f.name.text == field)
      return hir_to_type(f.hir_type, errors, pos, scope);
  }
  add_error(errors, pos,
            std::format("struct `{}` has no field `{}`", st->name.text, field));
  return std::nullopt;
}

void Checker::resolve_path(HirExprPath &path, std::vector<Error> &errors,
                           Scope &scope) {
  const auto &segs = path.segments;
  if (segs.empty())
    return;

  if (segs.size() == 1) {
    const auto &n = segs[0].text;
    if (auto *param = lookup_param(scope, n)) {
      if (param->HirBase::type)
        path.type = clone_type(*param->HirBase::type);
      return;
    }
    if (auto *l = lookup_let(scope, n)) {
      if (l->HirBase::type)
        path.type = clone_type(*l->HirBase::type);
      else
        add_error(
            errors, segs[0].pos,
            std::format("cannot infer type of `{}` before initialization", n));
      return;
    }
    if (auto *f = lookup_fn(scope, n)) {
      auto ft = function_type_for(*f, errors, scope);
      if (ft)
        path.type = std::move(*ft);
      return;
    }
    if (lookup_struct(scope, n)) {
      std::vector<std::string> p{n};
      path.type =
          Type{std::make_unique<NamedType>(NamedType{.path = std::move(p)})};
      return;
    }
    if (lookup_enum(scope, n)) {
      std::vector<std::string> p{n};
      path.type =
          Type{std::make_unique<NamedType>(NamedType{.path = std::move(p)})};
      return;
    }
    add_error(errors, segs[0].pos, std::format("unknown name `{}`", n));
    return;
  }

  if (segs.size() == 2) {
    if (auto *en = lookup_enum(scope, segs[0].text)) {
      bool ok = false;
      for (const auto &v : en->variants) {
        if (v.text == segs[1].text) {
          ok = true;
          break;
        }
      }
      if (ok) {
        std::vector<std::string> p{segs[0].text};
        path.type =
            Type{std::make_unique<NamedType>(NamedType{.path = std::move(p)})};
        return;
      }
    }
  }

  if (all_modules_) {
    const size_t n = segs.size();
    std::string mod_id;
    for (size_t i = 0; i < n - 1; ++i) {
      if (i)
        mod_id += "::";
      mod_id += segs[i].text;
    }
    if (all_modules_->contains(mod_id)) {
      const std::string item = segs[n - 1].text;
      auto mit = module_global_scope.find(mod_id);
      if (mit != module_global_scope.end()) {
        Scope *ms = mit->second;
        if (auto *f = lookup_fn(*ms, item)) {
          auto ft = function_type_for(*f, errors, scope);
          if (ft)
            path.type = std::move(*ft);
          return;
        }
        if (auto *gl = lookup_let(*ms, item)) {
          if (gl->HirBase::type)
            path.type = clone_type(*gl->HirBase::type);
          else
            add_error(
                errors, segs.back().pos,
                std::format("module `{}` has no inferred type for `{}` yet",
                            mod_id, item));
          return;
        }
        if (lookup_struct(*ms, item) || lookup_enum(*ms, item)) {
          std::vector<std::string> p;
          p.reserve(segs.size());
          for (const auto &t : segs)
            p.push_back(t.text);
          path.type = Type{
              std::make_unique<NamedType>(NamedType{.path = std::move(p)})};
          return;
        }
      }
    }
  }

  add_error(errors, segs[0].pos, "could not resolve this path expression");
}

auto Checker::collect_import_edges(const ModulesHir &modules)
    -> std::unordered_map<std::string, std::vector<std::string>> {
  std::unordered_map<std::string, std::vector<std::string>> res;

  for (auto &[name, hir] : modules) {
    for (auto &node : hir) {
      std::visit(
          [&](auto &&arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, HirImport>) {
              res[name].push_back(arg.target_module);
            }
          },
          node);
    }
  }

  return res;
}

static auto modules_in_dependency_order(
    const ModulesHir &modules,
    const std::unordered_map<std::string, std::vector<std::string>>
        &import_edges) -> std::vector<std::string> {
  std::unordered_map<std::string, int> indeg; // how many modules we import
  std::unordered_map<std::string, std::vector<std::string>>
      importers_of; // ex. B is imported by A and C

  for (const auto &[m, _] : modules)
    indeg[m] = 0;

  for (const auto &[importer, deps] : import_edges) {
    if (!modules.contains(importer))
      continue;
    for (const auto &dep : deps) {
      if (!modules.contains(dep))
        continue;
      importers_of[dep].push_back(importer);
      indeg[importer]++;
    }
  }

  std::vector<std::string> ready;
  for (const auto &[m, d] : indeg)
    if (d == 0)
      ready.push_back(m);

  std::vector<std::string> order;
  order.reserve(modules.size());
  while (!ready.empty()) {
    const std::string u = ready.back();
    ready.pop_back();
    order.push_back(u);
    for (const auto &imp : importers_of[u]) {
      if (--indeg[imp] == 0)
        ready.push_back(imp);
    }
  }

  if (order.size() != modules.size()) {
    for (const auto &[m, _] : modules) {
      if (std::find(order.begin(), order.end(), m) == order.end())
        order.push_back(m);
    }
  }
  return order;
}

auto Checker::find_import_pos(const ModulesHir &modules,
                              const std::string &from_module,
                              const std::string &to_module) -> Pos {
  auto mit = modules.find(from_module);
  if (mit == modules.end())
    return Pos{1, 1, 0};
  for (auto &node : mit->second) {
    if (auto *imp = std::get_if<HirImport>(&node)) {
      if (imp->target_module == to_module && !imp->path.empty())
        return imp->path[0].pos;
    }
  }
  return Pos{1, 1, 0};
}

auto Checker::has_import_cycles(
    const ModulesHir &modules,
    const std::unordered_map<std::string, std::vector<std::string>> &edges,
    std::vector<Error> &errors) -> bool {
  enum class State { NotStarted, InProgress, Done };
  std::unordered_map<std::string, State> state;
  std::vector<std::string> path;
  bool found = false;

  std::function<bool(const std::string &)> dfs =
      [&](const std::string &id) -> bool {
    auto &st = state[id];
    if (st == State::InProgress) {
      if (!found) {
        std::string cycle;
        for (size_t i = 0; i < path.size(); ++i) {
          if (i)
            cycle += " -> ";
          cycle += path[i];
        }
        if (!path.empty())
          cycle += " -> ";
        cycle += id;

        const std::string &from_module = path.empty() ? id : path.back();
        Pos pos = find_import_pos(modules, from_module, id);
        add_error(errors, from_module, pos,
                  std::format("import cycle: {}", cycle));
        found = true;
      }
      return true;
    }
    if (st == State::Done)
      return false;

    st = State::InProgress;
    path.push_back(id);

    auto it = edges.find(id);
    if (it != edges.end()) {
      for (const auto &to : it->second) {
        if (dfs(to))
          return true;
      }
    }

    path.pop_back();
    st = State::Done;
    return false;
  };

  for (const auto &[id, _] : edges) {
    if (state[id] == State::NotStarted && dfs(id))
      return true;
  }

  return found;
}

void Checker::check(HirStmt &stmt, std::vector<Error> &errors, Scope &scope) {
  std::visit(
      [&](auto &&arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::unique_ptr<HirIf>>)
          check(*arg, errors, scope);
        else if constexpr (std::is_same_v<T, std::unique_ptr<HirWhile>>)
          check(*arg, errors, scope);
        else if constexpr (std::is_same_v<T, std::unique_ptr<HirFor>>)
          check(*arg, errors, scope);
        else
          check(arg, errors, scope);
      },
      stmt.item);
}

void Checker::check(HirBlock &block, std::vector<Error> &errors, Scope &scope) {
  Scope block_scope{.parent = &scope};
  for (auto &stmt : block.stmts)
    check(stmt, errors, block_scope);
}

void Checker::check(HirReturn &ret, std::vector<Error> &errors, Scope &scope) {
  if (ret.expression)
    check(*ret.expression, errors, scope);
}

void Checker::check(HirBreak &brk, std::vector<Error> &errors, Scope &scope) {}

void Checker::check(HirContinue &cont, std::vector<Error> &errors,
                    Scope &scope) {}

void Checker::check(HirLet &let, std::vector<Error> &errors, Scope &scope) {
  scope.variables[let.name.text] = &let;
  std::optional<Type> explicit_ty;
  if (let.explicit_type) {
    check(*let.explicit_type, errors, scope);
    explicit_ty = hir_to_type(*let.explicit_type, errors, let.name.pos, scope);
  }
  if (let.initializer)
    check(*let.initializer, errors, scope);

  if (explicit_ty && let.initializer && let.initializer->type) {
    // TODO: fix let small: u8 = 16; use the explicit type
    if (!types_compatible(*explicit_ty, *let.initializer->type))
      add_error(
          errors, let.name.pos,
          std::format("initializer type `{}` does not match explicit type `{}`",
                      type_to_string(*let.initializer->type),
                      type_to_string(*explicit_ty)));
    let.HirBase::type = clone_type(*explicit_ty);
  } else if (explicit_ty) {
    let.HirBase::type = clone_type(*explicit_ty);
  } else if (let.initializer && let.initializer->type) {
    let.HirBase::type = clone_type(*let.initializer->type);
  }
}

void Checker::check(HirAssign &assign, std::vector<Error> &errors,
                    Scope &scope) {
  check(assign.lvalue, errors, scope);
  check(assign.rvalue, errors, scope);
}

void Checker::check(HirExprStmt &expr_stmt, std::vector<Error> &errors,
                    Scope &scope) {
  check(expr_stmt.expr, errors, scope);
}

void Checker::check(HirIf &if_stmt, std::vector<Error> &errors, Scope &scope) {
  check(if_stmt.condition, errors, scope);
  check(if_stmt.then_block, errors, scope);
  if (if_stmt.else_block)
    check(*if_stmt.else_block, errors, scope);
}

void Checker::check(HirWhile &while_stmt, std::vector<Error> &errors,
                    Scope &scope) {
  check(while_stmt.condition, errors, scope);
  check(while_stmt.block, errors, scope);
}

void Checker::check(HirFor &for_stmt, std::vector<Error> &errors,
                    Scope &scope) {
  if (for_stmt.init)
    check(*for_stmt.init, errors, scope);
  check(for_stmt.condition, errors, scope);
  if (for_stmt.update)
    check(*for_stmt.update, errors, scope);
  check(for_stmt.block, errors, scope);
}

void Checker::check(HirExprLiteral &literal, std::vector<Error> &errors,
                    Scope &scope) {
  switch (literal.tok.kind) {
  case TokenKind::Integer: {
    literal.type = type_from_primitive_kind(PrimitiveKind::I32);
  } break;
  case TokenKind::String: {
    literal.type = type_from_primitive_kind(PrimitiveKind::String);
  } break;
  case TokenKind::Char: {
    literal.type = type_from_primitive_kind(PrimitiveKind::Char);
  } break;
  case TokenKind::Real: {
    literal.type = type_from_primitive_kind(PrimitiveKind::Real);
  } break;
  case TokenKind::Null: {
    literal.type = type_from_primitive_kind(PrimitiveKind::Null);
  } break;
  case TokenKind::True:
  case TokenKind::False: {
    literal.type = type_from_primitive_kind(PrimitiveKind::Bool);
  } break;
  default:
    add_error(errors, literal.tok.pos,
              "Invalid token type for expression literal");
  }
}

void Checker::check(HirExprPath &path, std::vector<Error> &errors,
                    Scope &scope) {
  resolve_path(path, errors, scope);
}

void Checker::check(HirExprBinary &binary, std::vector<Error> &errors,
                    Scope &scope) {
  check(*binary.lhs, errors, scope);
  check(*binary.rhs, errors, scope);

  if (token_is_comparison(binary.op.kind) || binary.op.kind == TokenKind::And ||
      binary.op.kind == TokenKind::Or) {
    binary.type = type_from_primitive_kind(PrimitiveKind::Bool);
    return;
  }

  const auto *lt = binary.lhs->type ? &*binary.lhs->type : nullptr;
  const auto *rt = binary.rhs->type ? &*binary.rhs->type : nullptr;
  if (!lt || !rt)
    return;

  if (binary.op.kind == TokenKind::Mod) {
    if (!is_integral(*lt) || !is_integral(*rt)) {
      add_error(errors, binary.op.pos, "`%` expects integral operands");
      return;
    }
    binary.type = clone_type(*lt);
    return;
  }

  if (binary.op.kind == TokenKind::Add || binary.op.kind == TokenKind::Sub ||
      binary.op.kind == TokenKind::Mul || binary.op.kind == TokenKind::Div) {
    if (is_numeric(*lt) && is_numeric(*rt)) {
      const auto *pl = std::get_if<PrimitiveType>(&lt->data);
      const auto *pr = std::get_if<PrimitiveType>(&rt->data);
      if (pl && pr &&
          (pl->kind == PrimitiveKind::Real || pr->kind == PrimitiveKind::Real))
        binary.type = type_from_primitive_kind(PrimitiveKind::Real);
      else if (types_compatible(*lt, *rt))
        binary.type = clone_type(*lt);
      else
        binary.type = type_from_primitive_kind(PrimitiveKind::I32);
      return;
    }
    add_error(errors, binary.op.pos, "invalid operands to arithmetic operator");
  }
}

void Checker::check(HirExprUnary &unary, std::vector<Error> &errors,
                    Scope &scope) {
  check(*unary.expr, errors, scope);
  const auto *inner = unary.expr->type ? &*unary.expr->type : nullptr;
  if (!inner)
    return;

  switch (unary.op.kind) {
  case TokenKind::Sub:
    if (is_numeric(*inner))
      unary.type = clone_type(*inner);
    else
      add_error(errors, unary.op.pos, "`-` expects a numeric operand");
    break;
  case TokenKind::Not:
    if (const auto *p = std::get_if<PrimitiveType>(&inner->data);
        p && p->kind == PrimitiveKind::Bool)
      unary.type = type_from_primitive_kind(PrimitiveKind::Bool);
    else
      add_error(errors, unary.op.pos, "`not` expects a bool operand");
    break;
  case TokenKind::Mul: // deref
    if (const auto *ptr =
            std::get_if<std::unique_ptr<PointerType>>(&inner->data))
      unary.type = clone_type_impl(*(*ptr)->pointee);
    else
      add_error(errors, unary.op.pos, "`*` expects a pointer operand");
    break;
  case TokenKind::Ampersand: {
    auto p = std::make_unique<PointerType>();
    p->pointee = std::make_unique<Type>(clone_type_impl(*inner));
    unary.type = Type{.data = std::move(p)};
    break;
  }
  default:
    add_error(errors, unary.op.pos, "unsupported unary operator");
  }
}

void Checker::check(HirExprIndex &index, std::vector<Error> &errors,
                    Scope &scope) {
  check(*index.value, errors, scope);
  check(*index.index, errors, scope);
  if (!index.value->type) {
    return;
  }
  if (index.index->type) {
    const auto *ik = std::get_if<PrimitiveType>(&index.index->type->data);
    if (!ik || !is_integral_primitive(ik->kind))
      add_error(errors, index.index->type ? Pos{1, 1, 0} : Pos{1, 1, 0},
                "array index must be an integer");
  }
  if (auto el = element_type_for_indexing(*index.value->type))
    index.type = std::move(*el);
  else
    add_error(errors, Pos{1, 1, 0}, "cannot index this type");
}

void Checker::check(HirExprMember &member, std::vector<Error> &errors,
                    Scope &scope) {
  check(*member.object, errors, scope);
  if (!member.object->type)
    return;

  Type obj = strip_pointer_layers(clone_type_impl(*member.object->type));
  const auto *named = std::get_if<std::unique_ptr<NamedType>>(&obj.data);
  if (!named) {
    add_error(errors, member.member.pos,
              "field access requires a struct or pointer-to-struct value");
    return;
  }

  if (resolve_enum_named(**named, scope)) {
    add_error(
        errors, member.member.pos,
        "field access is not valid on an enum value (use `::` for variants)");
    return;
  }

  HirStruct *st = resolve_struct_named(**named, scope);
  if (!st) {
    add_error(errors, member.member.pos,
              "unknown struct type for field access");
    return;
  }
  if (auto ft = type_of_field(st, member.member.text, errors, member.member.pos,
                              scope))
    member.type = std::move(*ft);
}

void Checker::check(HirExprCall &call, std::vector<Error> &errors,
                    Scope &scope) {
  check(*call.callee, errors, scope);
  for (auto &a : call.args)
    check(a, errors, scope);

  if (!call.callee->type)
    return;
  const auto *fn_ty =
      std::get_if<std::unique_ptr<FunctionType>>(&call.callee->type->data);
  const Pos call_pos = expr_start_pos(*call.callee);
  if (!fn_ty) {
    add_error(errors, call_pos, "called value is not a function");
    return;
  }
  const auto &sig = **fn_ty;
  if (sig.params.size() != call.args.size()) {
    add_error(errors, call_pos,
              std::format("expected {} arguments, got {}", sig.params.size(),
                          call.args.size()));
    return;
  }
  for (size_t i = 0; i < call.args.size(); ++i) {
    if (call.args[i].type &&
        !types_compatible(sig.params[i], *call.args[i].type))
      add_error(
          errors, expr_start_pos(call.args[i]),
          std::format("argument {} type mismatch: expected `{}`, got `{}`",
                      i + 1, type_to_string(sig.params[i]),
                      type_to_string(*call.args[i].type)));
  }
  if (sig.return_type)
    call.type = clone_type_impl(*sig.return_type);
  else
    call.type = type_from_primitive_kind(PrimitiveKind::Void);
}

void Checker::check(HirExprAs &as_expr, std::vector<Error> &errors,
                    Scope &scope) {
  check(*as_expr.expr, errors, scope);
  check(as_expr.hir_type, errors, scope);
  if (auto t = hir_to_type(as_expr.hir_type, errors, Pos{1, 1, 0}, scope))
    as_expr.HirBase::type = std::move(*t);
}

void Checker::check(HirExprArray &array, std::vector<Error> &errors,
                    Scope &scope) {
  std::optional<Type> elem_ty;
  for (auto &e : array.elements) {
    check(e, errors, scope);
    if (!e.type)
      continue;
    if (!elem_ty)
      elem_ty = clone_type_impl(*e.type);
    else if (!types_compatible(*elem_ty, *e.type))
      add_error(errors, Pos{1, 1, 0},
                "array literal elements have mismatched types");
  }
  if (elem_ty) {
    auto a = std::make_unique<ArrayType>();
    a->element = std::make_unique<Type>(clone_type_impl(*elem_ty));
    a->size = array.elements.size();
    array.type = Type{.data = std::move(a)};
  }
}

void Checker::check(HirExprStruct &struct_expr, std::vector<Error> &errors,
                    Scope &scope) {
  check(struct_expr.hir_type, errors, scope);
  if (auto st_ty =
          hir_to_type(struct_expr.hir_type, errors, Pos{1, 1, 0}, scope))
    struct_expr.HirBase::type = clone_type_impl(*st_ty);
  else {
    for (auto &f : struct_expr.fields)
      check(f, errors, scope);
    return;
  }

  const auto *named =
      std::get_if<std::unique_ptr<NamedType>>(&struct_expr.type->data);
  HirStruct *st = named ? resolve_struct_named(**named, scope) : nullptr;
  if (!st) {
    for (auto &f : struct_expr.fields)
      check(f, errors, scope);
    return;
  }

  for (auto &f : struct_expr.fields) {
    check(f, errors, scope);
    if (auto ft = type_of_field(st, f.name.text, errors, f.name.pos, scope)) {
      if (f.value && f.value->type && !types_compatible(*ft, *f.value->type))
        add_error(errors, f.name.pos,
                  std::format("field `{}` expects `{}`, got `{}`", f.name.text,
                              type_to_string(*ft),
                              type_to_string(*f.value->type)));
    }
  }
}

void Checker::check(StructExprField &field, std::vector<Error> &errors,
                    Scope &scope) {
  if (field.value)
    check(*field.value, errors, scope);
}

void Checker::check(HirExpr &expr, std::vector<Error> &errors, Scope &scope) {
  std::visit(
      [&](auto &&arg) {
        if constexpr (is_unique_ptr_v<std::decay_t<decltype(arg)>>) {
          if (arg)
            check(*arg, errors, scope);
        } else {
          check(arg, errors, scope);
        }
      },
      expr.item);

  expr.type = type_from_expr_item(expr.item);
}

void Checker::check(HirTypePtr &ptr, std::vector<Error> &errors, Scope &scope) {
  if (ptr.base)
    check(*ptr.base, errors, scope);
}

void Checker::check(HirType &type, std::vector<Error> &errors, Scope &scope) {
  std::visit(
      [&](auto &&arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::unique_ptr<HirTypePtr>>) {
          if (arg)
            check(*arg, errors, scope);
        }
      },
      type.item);
}

void Checker::check(HirTypedIdent &typed, std::vector<Error> &errors,
                    Scope &scope) {
  check(typed.hir_type, errors, scope);
  if (auto t = hir_to_type(typed.hir_type, errors, typed.name.pos, scope))
    typed.HirBase::type = std::move(*t);
}

void Checker::check(HirFnDef &fn, std::vector<Error> &errors, Scope &scope) {
  scope.functions[fn.name.text] = &fn;
  Scope body{.parent = &scope};
  for (auto &p : fn.params) {
    check(p, errors, body);
    body.fn_params[p.name.text] = &p;
  }
  if (fn.return_type)
    check(*fn.return_type, errors, scope);
  check(fn.block, errors, body);
}

void Checker::check(HirStruct &strct, std::vector<Error> &errors,
                    Scope &scope) {
  scope.structs[strct.name.text] = &strct;
  for (auto &f : strct.fields)
    check(f, errors, scope);
}

void Checker::check(HirEnum &enm, std::vector<Error> &errors, Scope &scope) {
  scope.enums[enm.name.text] = &enm;
}

void Checker::check(HirImport &imp, std::vector<Error> &errors, Scope &scope) {
  scope.imported_modules.insert(&imp);
}

void Checker::add_error(std::vector<Error> &errors, Pos pos,
                        std::string_view message) {
  add_error(errors, current_module_, pos, message);
}

void Checker::scan_module_exports(const std::string &module_name,
                                  std::vector<Hir> &hir, Scope &scope,
                                  std::vector<Error> &errors) {
  current_module_ = module_name;
  for (auto &item : hir) {
    std::visit(
        [&](auto &&arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, HirFnDef>) {
            if (!scope.functions.emplace(arg.name.text, &arg).second)
              add_error(errors, arg.name.pos,
                        std::format("duplicate function `{}`", arg.name.text));
          } else if constexpr (std::is_same_v<T, HirStruct>) {
            if (!scope.structs.emplace(arg.name.text, &arg).second)
              add_error(errors, arg.name.pos,
                        std::format("duplicate struct `{}`", arg.name.text));
          } else if constexpr (std::is_same_v<T, HirEnum>) {
            if (!scope.enums.emplace(arg.name.text, &arg).second)
              add_error(errors, arg.name.pos,
                        std::format("duplicate enum `{}`", arg.name.text));
          } else if constexpr (std::is_same_v<T, HirLet>) {
            if (!scope.variables.emplace(arg.name.text, &arg).second)
              add_error(
                  errors, arg.name.pos,
                  std::format("duplicate `{}` at module scope", arg.name.text));
          } else if constexpr (std::is_same_v<T, HirImport>) {
            scope.imported_modules.insert(&arg);
          }
        },
        item);
  }
}

void Checker::add_error(std::vector<Error> &errors,
                        const std::string &module_id, Pos pos,
                        std::string_view message) {
  if (!sources_) {
    errors.push_back(Error{.msg = std::format("{}:{}:{}: error: {}", module_id,
                                              pos.line, pos.col, message)});
    return;
  }
  auto it = sources_->find(module_id);
  if (it == sources_->end()) {
    errors.push_back(Error{.msg = std::format("{}:{}:{}: error: {}", module_id,
                                              pos.line, pos.col, message)});
    return;
  }
  errors.push_back(
      make_source_error(it->second.path, it->second.text, pos, message));
}

void Checker::check_modules(
    ModulesHir &modules, std::vector<Error> &errors,
    const std::unordered_map<std::string, ModuleSource> &sources) {
  sources_ = &sources;
  module_scope_storage_.clear();
  module_global_scope.clear();

  const auto edges = collect_import_edges(modules);
  if (has_import_cycles(modules, edges, errors))
    return;

  for (auto &[name, hir] : modules) {
    auto scope = std::make_unique<Scope>();
    scope->parent = nullptr;
    scan_module_exports(name, hir, *scope, errors);
    module_global_scope[name] = scope.get();
    module_scope_storage_[name] = std::move(scope);
  }

  all_modules_ = &modules;
  const auto mod_order = modules_in_dependency_order(modules, edges);

  for (const auto &name : mod_order) {
    Scope *scope = module_global_scope[name];
    current_module_ = name;
    auto &hir = modules[name];
    for (auto &item : hir) {
      if (auto *let = std::get_if<HirLet>(&item))
        check(*let, errors, *scope);
    }
  }

  for (const auto &name : mod_order) {
    Scope *scope = module_global_scope[name];
    current_module_ = name;
    auto &hir = modules[name];
    for (auto &item : hir) {
      if (std::holds_alternative<HirLet>(item))
        continue;
      std::visit([&](auto &&arg) { check(arg, errors, *scope); }, item);
    }
  }
  all_modules_ = nullptr;
}
