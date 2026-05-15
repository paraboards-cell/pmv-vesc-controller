#pragma once
#include <stdbool.h>

void castor_init(void);

// active=true → lower castor arm (rear wheels lift for tight turn)
// active=false → raise castor arm (rear wheels return to floor)
void castor_set(bool active);

bool castor_is_active(void);
