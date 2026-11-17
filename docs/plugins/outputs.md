# Output plugins

| Type | Description |
|------|-------------|
| `stdout` | Prints `format` (default `%BODY%`) per record. |
| `lua` | Output written in Lua. |

Outputs get whatever payload kind is wired to them; see [Payloads](../payloads.md).

## `stdout`
Prints `format` per record; supports `%BODY%` and `%HEADERS[name]%`. Bytes or text.
```yaml
outputs:
  out:
    type: stdout
    config:
      format: "%BODY% / %HEADERS[some_header]%"
```

## `lua`
Parameters: `script` (inline code) or `file` (path), and optional `max_instructions` (default `10000000`,
`0` = unlimited), which caps the work done per call so a runaway script fails with an error instead of
hanging the stage. The `config:` section is visible to the script as the global table `config`.

```yaml
outputs:
  printer:
    type: lua
    config:
      script: |
        local total = 0
        function write(msg)
          total = total + 1
          print(total .. ": " .. msg.body)
        end
        function flush() print("total: " .. total) end
```
```lua
function write(msg) end   -- called for every record
function flush() end      -- optional: called at the end
```
```lua
-- message:            { body = "bytes", headers = { key = "value" } }
-- text message:       { text = "string", headers = { ... } }
-- structured message: { value = <table or scalar>, headers = { ... } } (no body)
```
In a structured value a table that is a sequence `{1, 2, 3}` is an array, a table with string keys is a map,
and an empty table is an empty map. The global `null` stands for a null member.

Scripts are sandboxed: only the base library (without `load`, `dofile`, `loadfile`, `require`), `string`,
`table`, `math`, `utf8` and `coroutine` are available; there is no `io`, `os`, `package` or `debug`.
Lua is optional: configure with `-DTORUSTIQ_WITH_LUA=OFF` to build without it. The system Lua (5.4+) is
used if found, otherwise Lua 5.4 is downloaded and built.

See `examples/lua_pipeline.yaml`.
