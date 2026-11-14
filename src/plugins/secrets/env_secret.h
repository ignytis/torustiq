#ifndef TORUSTIQ_PLUGINS_SECRETS_ENV_SECRET_H_
#define TORUSTIQ_PLUGINS_SECRETS_ENV_SECRET_H_

#include "torustiq/sdk.h"

extern "C" {
// Registers the "env" secret provider. Config: `key` (required), the environment variable name.
// An unset variable is an error; an empty one is a valid (empty) secret.
int32_t torustiq_register_env_secret(const torustiq_registrar* registrar);
}

#endif // TORUSTIQ_PLUGINS_SECRETS_ENV_SECRET_H_
