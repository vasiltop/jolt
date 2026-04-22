#include "checker.hpp"
#include <variant>

void Checker::check(HirFnDef & /* fn */, std::vector<Error> & /* errors */) {}

void Checker::check(HirStruct & /* strct */, std::vector<Error> & /* errors */) {}

void Checker::check(HirEnum & /* enm */, std::vector<Error> & /* errors */) {}

void Checker::check(HirImport & /* imp */, std::vector<Error> & /* errors */) {}

void Checker::check(HirConst & /* cnst */, std::vector<Error> & /* errors */) {}

void Checker::check(HirLet & /* let */, std::vector<Error> & /* errors */) {}

void Checker::check_modules(ModulesHir &modules, std::vector<Error> &errors) {
  for (auto &[_name, scope] : modules) {
    (void)_name;
    for (auto &item : scope.items) {
      std::visit(
          [this, &errors](auto &&arg) { this->check(arg, errors); }, item);
    }
  }
}
