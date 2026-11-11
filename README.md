# torustiq
Torustiq (_Estonian: torustik - a pipeline_) is a data pipeline constructor: reads records from an input,
optionally transforms them, and writes them to an output.
Pipelines can be finite (a directory of files) or continuous (a stream); processing is multithreaded with
backpressure between stages.

## Build
```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```
Dependencies (yaml-cpp, CLI11, nlohmann_json, GoogleTest) are used from the system if installed, otherwise fetched.
The plugin SDK ([torustiq-sdk](https://github.com/ignytis/torustiq-sdk)) is fetched with `FetchContent`;
to build against a local checkout add `-DFETCHCONTENT_SOURCE_DIR_TORUSTIQ_SDK=$PWD/../torustiq-sdk`.

## Usage
```sh
build/torustiq <pipeline.yaml>
printf 'a\nb\n' | build/torustiq examples/pipeline.yaml
```
Exit code is `0` on success and `1` on a configuration or runtime error. Ctrl-C stops a running pipeline.

## Pipeline definition
```yaml
name: My pipeline
inputs:
  my_input:
    type: stdin
transforms:              # optional
  my_transform:
    type: some_transform
outputs:
  my_output:
    inputs: [my_transform]   # optional: defaults to the previous component
    type: stdout
    config:
      format: "%BODY% / %HEADERS[some_header]%"
```
- Components are listed under `inputs`, `transforms` and `outputs` as `id: {type, config}`.
- `inputs:` wires a component to earlier components (an output may list several, fanning in; one component
  may feed several outputs). When omitted, a component reads from the previous one in the chain.
- `config:` holds plugin parameters (nested YAML is flattened to keys like `auth.user`).

## Payloads
A message carries either simple data (**bytes**: the whole content is one piece of data) or structured data
(**structured**: a tree of null/bool/int/double/string/array/map that components access by key).
The kind is agreed once, when the pipeline is built: each step is told the kind of the step before it and
declares the kind it emits. A step that cannot handle what it receives makes the pipeline fail at startup,
naming the step (no silent conversion, no per-message checks). Convert with ordinary steps:

```yaml
transforms:
  parse: {type: decode_json}            # bytes -> structured
  edit:  {type: lua, config: {script: "function process(m) m.value.user.name = 'x' return m end"}}
  dump:  {type: encode_json}            # structured -> bytes
```
Several transforms can edit the same document without re-parsing it. See `examples/json_pipeline.yaml`.

## Built-in plugins
| Type | Kind | Description / config |
|------|------|----------------------|
| `stdin` | input | Each line is a record. |
| `directory` | input | Each file in `path` (sorted) is a record; headers `filename`, `path`. |
| `lua` | input, transform, output | Scripts in Lua, see below. |
| `decode_json` | transform | Bytes -> structured: parses the body as a JSON document. |
| `encode_json` | transform | Structured -> bytes: serialises the value as compact JSON. |
| `stdout` | output | Prints `format` (default `%BODY%`) per record; supports `%BODY%` and `%HEADERS[name]%`. Bytes only. |

### Lua plugins
`type: lua` works as an input, a transform and an output. Parameters: `script` (inline code) or `file`
(path), and optional `max_instructions` (default `10000000`, `0` = unlimited), which caps the work done per
call so a runaway script fails with an error instead of hanging the stage. For inputs it applies between
two `emit()` calls. The `config:` section is visible to the script as the global table `config`.
`payload: bytes|value` sets the kind a script input emits (default `bytes`) or a transform emits (default:
the kind it receives). Outputs and transforms get whatever kind is wired to them.

```lua
-- input:     function run(emit)    -- emit(msg) returns false when the pipeline is stopping; sleep(seconds) exists
-- transform: function process(msg) -- return nil (drop), a message, or a list of messages
-- output:    function write(msg)   -- optional: function flush() at the end
-- message:   { body = "bytes", headers = { key = "value" } }   (a plain string means body only)
-- structured message: { value = <table or scalar>, headers = { ... } }   (no body)
```
In a structured value a table that is a sequence `{1, 2, 3}` is an array, a table with string keys is a map
(keys are emitted sorted), and an empty table is an empty map. The global `null` stands for a null member
(a `nil` member is absent). Functions, userdata, mixed tables and non-string keys are errors.
Scripts are sandboxed: only the base library (without `load`, `dofile`, `loadfile`, `require`), `string`,
`table`, `math`, `utf8` and `coroutine` are available; there is no `io`, `os`, `package` or `debug`.
Lua is optional: configure with `-DTORUSTIQ_WITH_LUA=OFF` to build without it. The system Lua (5.4+) is
used if found, otherwise Lua 5.4 is downloaded and built.

See `examples/` for runnable pipelines (`examples/lua_pipeline.yaml` uses Lua for every step). Plugins are written against the SDK; see the SDK README.
Contributor/agent guidance is in [AGENTS.md](AGENTS.md).
