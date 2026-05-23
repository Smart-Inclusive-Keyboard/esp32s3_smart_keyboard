/* Ukrainian (UA) layout -- placeholder. See layout_de.c for context. */
#include "kb_layout.h"

static const kb_key_t s_keys[1] = {
    { "UA?", "UA?", HID_USAGE_NONE, KB_KEY_SPECIAL_NONE },
};

const kb_layout_t kb_layout_ua = {
    .name = "UA",
    .rows = 1,
    .cols = 1,
    .keys = s_keys,
};
