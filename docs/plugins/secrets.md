# Secret providers

Providers are declared in the `secrets` section of a pipeline, see [Secrets](../secrets.md).

| Type | Config | Description |
|------|--------|-------------|
| `file` | `path` | The file content without one trailing newline. |
| `env` | `key` | The value of the environment variable; it must be set. |

Providers can also come from dynamic plugins (the plugin ID is the `kind`).

## `file`
```yaml
secrets:
  db_password:
    kind: file
    config: {path: /run/secrets/db_password}
```

## `env`
```yaml
secrets:
  api_token:
    kind: env
    config: {key: MY_ENV_VAR_NAME}
```

Both are used through `{_kind: ref_secret, id: <secret id>}`, see `../../examples/secrets.yaml`.
