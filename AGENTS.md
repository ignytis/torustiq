# AGENTS.md

Guidance for AI agents and contributors working on torustiq.

## What this is
A data pipeline orchestrator (like Redpanda Connect / Vector). The `torustiq` binary takes a YAML
pipeline definition and runs it: one or more inputs, zero or more transforms, one or more outputs.

## Layout
- `src/main.cpp` — CLI (CLI11), signal handling.
- `src/core/` — config parsing (`pipeline_config`), `message`, `plugin_registry`, `bounded_queue`,
  threaded `pipeline` runner. Must not depend on concrete plugins.
- `src/plugins/` — built-in plugins, grouped by kind. They must use **only** the SDK C API
  (`torustiq/sdk.h`), exactly like external plugins will:
  - `inputs/` (`stdin`, `directory`, `lua_input`), `transforms/` (`lua_transform`), `outputs/` (`stdout`, `lua_output`);
  - `_common/` — code shared between plugins, not a plugin kind (`_common/lua/`: sandboxed `LuaRuntime`, `LuaPlugin` base);
  - `builtin_plugins.cpp` registers everything; each plugin exports a `torustiq_register_*` function.
  New plugin → put it in the directory of its kind and list it in `builtin_plugins.cpp` and `CMakeLists.txt`.
- Dynamic plugins: `core/plugin_loader` scans `TORUSTIQ_PLUGINS_DIR` (`.so/.dylib/.dll`), reads each
  library's `torustiq_plugin_get_info` (ID + ABI) and closes it again. Only plugins whose ID is used as a
  pipeline `type` (and is not built in) are loaded and get `torustiq_plugin_init` — once per library; the
  per-step `create` runs per pipeline step. `PluginLoader` must outlive `Pipeline`/`PluginRegistry`.
  `TORUSTIQ_HOME` defaults to the compile-time `TORUSTIQ_DEFAULT_HOME` (`<project>/dev/torustiq_home` when
  `TORUSTIQ_DEV_MODE`, on by default in Debug; otherwise `~/.torustiq`); `TORUSTIQ_PLUGINS_DIR` defaults to
  `TORUSTIQ_HOME/plugins`. A dev build never looks outside the project unless the variables are set. `dev/` is gitignored. Sample plugins live in the `torustiq-plugins` project
  (`scripts/link_dev.sh` symlinks them; examples are in that project's `examples/`).
- `tests/` — one `<unit>_test.cpp` per unit (GoogleTest); each test target compiles only the sources it needs.
- `examples/` — sample pipelines.
- The plugin SDK is a **separate project** (`torustiq-sdk`), fetched via `FetchContent`. API changes go there
  first; do not re-add SDK headers to this repo.

## Rules
- Follow the C++ conventions: C++20, Google style (`snake_case.cpp/.h`, `PascalCase` functions/types,
  `snake_case` variables, `trailing_underscore_` members, `kConstant`), include guards
  `TORUSTIQ_<PATH>_H_`, builds on Linux/Windows/macOS.
- New source files must be added to the target source lists in `CMakeLists.txt` (and `tests/CMakeLists.txt`
  for tests). Add a test with new logic and with every bug fix; tests must be deterministic (use temp dirs).
- Plugin contract: data given to a callback is owned by the caller; copy it. No exceptions across the C boundary.
- Keep the build warning-free (`-Wall -Wextra -Wpedantic`, `/W4`).

## Lua plugins (`src/plugins/{inputs,transforms,outputs}/lua_*`, shared code in `src/plugins/_common/lua/`)
- `LuaRuntime` is the sandboxed interpreter (limited libraries, instruction-count hook, `RequestStop`).
  Lua errors use `longjmp`: never call `lua_error`/`luaL_error` (or an API that can raise) while C++ objects
  with destructors are alive in that frame. Convert messages with raw accessors only (no metamethods).
- Keep code compatible with Lua 5.4 and 5.5. Everything Lua-specific stays behind `TORUSTIQ_WITH_LUA`.

## Build and test
```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
clang-format --dry-run --Werror $(git ls-files '*.cpp' '*.h')
```
If the SDK repo is not pushed or you are changing it, use a local checkout:
`-DFETCHCONTENT_SOURCE_DIR_TORUSTIQ_SDK=$PWD/../torustiq-sdk`.
