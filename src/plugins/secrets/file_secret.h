#ifndef TORUSTIQ_PLUGINS_SECRETS_FILE_SECRET_H_
#define TORUSTIQ_PLUGINS_SECRETS_FILE_SECRET_H_

#include "torustiq/sdk.h"

extern "C" {
// Registers the "file" secret provider. Config: `path` (required). The secret is the file content
// without one trailing newline ("\n" or "\r\n").
int32_t torustiq_register_file_secret(const torustiq_registrar* registrar);
}

#endif // TORUSTIQ_PLUGINS_SECRETS_FILE_SECRET_H_
