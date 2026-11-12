#ifndef TORUSTIQ_PLUGINS_TRANSFORMS_TEXT_TRANSFORMS_H_
#define TORUSTIQ_PLUGINS_TRANSFORMS_TEXT_TRANSFORMS_H_

#include "torustiq/sdk.h"

extern "C" {
// Registers two transforms that convert between bytes and text payloads:
//   `decode_text`  bytes -> text: the bytes must be valid UTF-8. Config `invalid`:
//                  `error` (default) fails the pipeline on invalid input, `replace` substitutes
//                  every invalid sequence with U+FFFD.
//   `encode_text`  text -> bytes: the UTF-8 content of the string.
// Headers pass through.
int32_t torustiq_register_text_transforms(const torustiq_registrar* registrar);
}

#endif // TORUSTIQ_PLUGINS_TRANSFORMS_TEXT_TRANSFORMS_H_
