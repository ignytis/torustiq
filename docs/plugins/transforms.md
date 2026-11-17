# Transform plugins

| Type | Description |
|------|-------------|
| `decode_text` | Bytes -> text: the body must be valid UTF-8; `invalid: error` (default) or `replace` (U+FFFD). |
| `encode_text` | Text -> bytes. |
| `decode_json` | Bytes or text -> structured: parses the data as a JSON document. |
| `encode_json` | Structured -> bytes: serialises the value as compact JSON. |
| `lua` | Transform written in Lua. |

Kinds are agreed when the pipeline is built; see [Payloads](../payloads.md).

## `decode_text` / `encode_text`
```yaml
transforms:
  to_text:  {type: decode_text, config: {invalid: replace}}   # invalid UTF-8: error|replace
  to_bytes: {type: encode_text}
```

## `decode_json` / `encode_json`
`decode_json` parses bytes or text into a structured value; `encode_json` writes it back as compact JSON.
```yaml
transforms:
  parse: {type: decode_json}   # bytes or text -> structured
  dump:  {type: encode_json}   # structured -> bytes
```
See `examples/json_pipeline.yaml`.

## `lua`
Parameters: `script` (inline code) or `file` (path), and optional `max_instructions` (default `10000000`,
`0` = unlimited), which caps the work done per call so a runaway script fails with an error instead of
hanging the stage. The `config:` section is visible to the script as the global table `config`.
`payload: bytes|text|value` sets the kind the transform emits (default: the kind it receives).

```yaml
transforms:
  shout_odd:
    type: lua
    config:
      script: |
        function process(msg)
          if tonumber(msg.headers.seq) % 2 == 0 then return nil end
          msg.body = msg.body:upper()
          return msg
        end
```
```lua
function process(msg)  -- return nil (drop), a message, or a list of messages
  return msg
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
