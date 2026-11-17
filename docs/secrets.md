# Secrets

Keep passwords and tokens out of the definition: declare providers under `secrets` and reference them.
```yaml
secrets:
  db_password:
    kind: file          # built in: `file` (config: path), `env` (config: key)
    config: {path: /run/secrets/db_password}

inputs:
  db:
    type: mysql
    config:
      user: myuser      # same as {_kind: string, value: myuser}
      password: {_kind: ref_secret, id: db_password}
```
- A parameter that is a map with `_kind: ref_secret` is replaced by the secret's value at startup, before any
  step is created; plugins only see the plain value. Any other scalar is a static string.
- Only referenced secrets are resolved, each once. A missing file/variable or an unknown id stops the start.
- Providers can also come from dynamic plugins (the plugin ID is the `kind`).

The built-in providers are described in [Secret providers](plugins/secrets.md).
See also `examples/secrets.yaml`.
