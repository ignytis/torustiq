#ifndef TORUSTIQ_PLUGINS_TRANSFORMS_JSON_TRANSFORMS_H_
#define TORUSTIQ_PLUGINS_TRANSFORMS_JSON_TRANSFORMS_H_

#include "torustiq/sdk.h"

extern "C" {
// Registers two transforms that convert between the payload kinds:
//   `decode_json`  bytes -> structured: parses the bytes as one JSON document.
//   `encode_json`  structured -> bytes: serialises the value as compact JSON.
// JSON null/bool/integer/float/string/array/object map to the matching value kinds (object member
// order is kept). Integers above the int64 range become floats. A BYTES member cannot be encoded.
// Headers pass through. A document that cannot be parsed / encoded fails the pipeline.
int32_t torustiq_register_json_transforms(const torustiq_registrar* registrar);
}

#endif // TORUSTIQ_PLUGINS_TRANSFORMS_JSON_TRANSFORMS_H_
