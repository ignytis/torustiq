# Payloads

A message carries a payload of one of three kinds.

## Bytes
Simple data: the whole content is one piece of data (a line, a file). This is what inputs such as `stdin`
and `directory` emit by default.

## Text
A UTF-8 string. Text steps work on a string; `decode_text` / `encode_text` convert from and to bytes.

```yaml
transforms:
  to_text:   {type: decode_text, config: {invalid: replace}}   # bytes -> text (invalid UTF-8: error|replace)
  shout:     {type: lua, config: {script: "function process(m) return m.text:upper() end"}}
  to_bytes:  {type: encode_text}                               # text -> bytes
```
See `examples/text_pipeline.yaml`.

## Structured
A tree of null/bool/int/double/array/map that components access by key. Several transforms can edit the
same document without re-parsing it.

```yaml
transforms:
  parse: {type: decode_json}            # bytes -> structured
  edit:  {type: lua, config: {script: "function process(m) m.value.user.name = 'x' return m end"}}
  dump:  {type: encode_json}            # structured -> bytes
```
See `examples/json_pipeline.yaml`.

## Kind negotiation
The kind is agreed once, when the pipeline is built: each step is told the kind of the step before it and
declares the kind it emits. A step that cannot handle what it receives makes the pipeline fail at startup,
naming the step (no silent conversion, no per-message checks). Convert with ordinary steps, see
[transforms](plugins/transforms.md).
