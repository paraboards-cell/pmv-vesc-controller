#pragma once
#include "joycon_types.h"
#include <stdbool.h>

// Parse a raw HID input report into joycon_state_t.
// Returns true if report was recognised and state updated.
bool joycon_parse_report(const uint8_t *data, uint16_t len, joycon_state_t *out);
