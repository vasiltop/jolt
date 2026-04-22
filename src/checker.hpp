#pragma once

#include "errors.hpp"
#include "hir.hpp"
#include <vector>

class Checker {
public:
  void check_modules(ModulesHir &modules, std::vector<Error> &errors);

private:
  void check(HirFnDef &fn, std::vector<Error> &errors);
  void check(HirStruct &strct, std::vector<Error> &errors);
  void check(HirEnum &enm, std::vector<Error> &errors);
  void check(HirImport &imp, std::vector<Error> &errors);
  void check(HirConst &cnst, std::vector<Error> &errors);
  void check(HirLet &let, std::vector<Error> &errors);
};
