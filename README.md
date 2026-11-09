# torustiq
A data pipeline constructor: reads records from an input, optionally transforms them, and writes them to an output.
Pipelines can be finite (a directory of files) or continuous (a stream); processing is multithreaded with
backpressure between stages.

## Build
```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```
Dependencies (yaml-cpp, CLI11, GoogleTest) are used from the system if installed, otherwise fetched.
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

## Built-in plugins
| Type | Kind | Description / config |
|------|------|----------------------|
| `stdin` | input | Each line is a record. |
| `directory` | input | Each file in `path` (sorted) is a record; headers `filename`, `path`. |
| `stdout` | output | Prints `format` (default `%BODY%`) per record; supports `%BODY%` and `%HEADERS[name]%`. |

See `examples/` for runnable pipelines. Plugins are written against the SDK; see the SDK README.
Contributor/agent guidance is in [AGENTS.md](AGENTS.md).
