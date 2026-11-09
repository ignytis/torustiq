#ifndef TORUSTIQ_PLUGINS_LUA_PLUGINS_H_
#define TORUSTIQ_PLUGINS_LUA_PLUGINS_H_

#include "torustiq/sdk.h"

extern "C" {
// Registers the "lua" input, transform and output. Each takes the parameters `script` (inline
// code) or `file`, and an optional `max_instructions` (see lua_runtime.h). The script sees its
// parameters as the global table `config`.
//
// input:     function run(emit)    -- call emit(msg) per message; emit returns false when the
//                                     pipeline wants no more data. `sleep(seconds)` is available.
//                                     max_instructions applies between two emit() calls.
// transform: function process(msg) -- return nil/false (drop), a message, or a list of messages.
// output:    function write(msg)   -- consume a message; optional function flush() at the end.
// A message is a table {body = "bytes", headers = {key = "value"}} (a plain string means body
// only).
int32_t torustiq_register_lua_plugins(const torustiq_registrar* registrar);
}

#endif // TORUSTIQ_PLUGINS_LUA_PLUGINS_H_
