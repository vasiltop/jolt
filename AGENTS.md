# Agent Instructions for Jolt

- **Build Command:** Run `make` to compile the project. This builds `bin/compiler`.
- **Clean:** `make clean`
- **C++ Standard:** Uses C++23.
- **Entry Point:** The main executable logic is in `src/main.cpp`.
- **Testing:** The compiler processes `.jolt` files. Test changes by running `./bin/compiler [path_to_jolt_file]`. By default, running `./bin/compiler` without arguments parses `./examples/values.jolt`. There is currently no automated test suite; verify behavior by running the compiler against files in the `examples/` directory.
- **Project Structure:** Source code is in `src/`. Example code for the Jolt language is in `examples/`.
- **Modules:** There is no `module` keyword. A compilation unit is identified by its file path (relative to the parent of the file passed to the compiler), e.g. `examples/std/io.jolt` → `std::io`.
