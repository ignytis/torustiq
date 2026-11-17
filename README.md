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
The SDK ([torustiq-sdk](https://github.com/ignytis/torustiq-sdk)) is looked up in this order: a local checkout given
with `-DFETCHCONTENT_SOURCE_DIR_TORUSTIQ_SDK=<dir>`, the `TORUSTIQ_SDK_SOURCE_DIR` environment variable, `../torustiq-sdk`
(Debug builds only), an installed package, and finally `FetchContent` from git. The configure step prints which one is used.

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
- `config:` holds plugin parameters (nested YAML is flattened to keys like `auth.user`). Values can reference
  [secrets](docs/secrets.md).
- Data between steps can be bytes, text or structured; see [payloads](docs/payloads.md).

## Documentation
- [Payloads](docs/payloads.md) — bytes, text and structured data
- [Secrets](docs/secrets.md) — keeping credentials out of the definition
- Built-in plugins:
  - [Inputs](docs/plugins/inputs.md)
  - [Transforms](docs/plugins/transforms.md)
  - [Outputs](docs/plugins/outputs.md)
  - [Secret providers](docs/plugins/secrets.md)
- `examples/` — runnable pipelines (`examples/lua_pipeline.yaml` uses Lua for every step)
- Plugins are written against the SDK; see the [SDK README](https://github.com/ignytis/torustiq-sdk).
- Contributor/agent guidance: [AGENTS.md](AGENTS.md)
