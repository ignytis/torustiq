# Input plugins

| Type | Description |
|------|-------------|
| `stdin` | Each line is a record. |
| `directory` | Each file in `path` (sorted) is a record; headers `filename`, `path`. |
| `lua` | Input written in Lua. |

Inputs emit **bytes** by default; see [Payloads](../payloads.md).

## `stdin`
Reads standard input; each line becomes one record.

## `directory`
Reads every file in `path` (sorted) as one record. Headers: `filename`, `path`. The pipeline finishes
when all files are read.

## `lua`
Parameters: `script` (inline code) or `file` (path), and optional `max_instructions` (default `10000000`,
`0` = unlimited), which caps the work done between two `emit()` calls so a runaway script fails with an
error instead of hanging the stage. The `config:` section is visible to the script as the global table
`config`. `payload: bytes|text|value` sets the kind the input emits (default `bytes`).

```lua
function run(emit)    -- emit(msg) returns false when the pipeline is stopping; sleep(seconds) exists
  while emit("hello") do sleep(1) end
end
```
```lua
-- message:            { body = "bytes", headers = { key = "value" } }   (a plain string means body only)
-- text message:       { text = "string", headers = { ... } }            (a plain string means text only)
-- structured message: { value = <table or scalar>, headers = { ... } } (no body)
```
In a structured value a table that is a sequence `{1, 2, 3}` is an array, a table with string keys is a map
(keys are emitted sorted), and an empty table is an empty map. The global `null` stands for a null member
(a `nil` member is absent). Functions, userdata, mixed tables and non-string keys are errors.

Scripts are sandboxed: only the base library (without `load`, `dofile`, `loadfile`, `require`), `string`,
`table`, `math`, `utf8` and `coroutine` are available; there is no `io`, `os`, `package` or `debug`.
Lua is optional: configure with `-DTORUSTIQ_WITH_LUA=OFF` to build without it. The system Lua (5.4+) is
used if found, otherwise Lua 5.4 is downloaded and built.

See `examples/lua_pipeline.yaml`.
