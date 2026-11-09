#ifndef TORUSTIQ_PLUGINS_TRANSFORMS_LUA_TRANSFORM_H_
#define TORUSTIQ_PLUGINS_TRANSFORMS_LUA_TRANSFORM_H_

#include "torustiq/sdk.h"

extern "C" {
// Registers the "lua" transform. Parameters: `script` (inline code) or `file`, optional
// `max_instructions` (see lua_runtime.h).
//
//   function process(msg)  -- return nil/false (drop), a message, or a list of messages
//
// A message is a table {body = "bytes", headers = {key = "value"}}; a plain string means body only.
int32_t torustiq_register_lua_transform(const torustiq_registrar* registrar);
}

#endif // TORUSTIQ_PLUGINS_TRANSFORMS_LUA_TRANSFORM_H_
