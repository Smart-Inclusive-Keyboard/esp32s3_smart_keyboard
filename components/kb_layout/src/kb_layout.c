/*
 * kb_layout: active-layout selector and registry.
 */

#include "kb_layout.h"

#include <string.h>

#include "sdkconfig.h"

static const kb_layout_t *const s_all[] = {
    &kb_layout_us,
    &kb_layout_de,
    &kb_layout_fr,
    &kb_layout_ua,
};

static const int s_count = sizeof(s_all) / sizeof(s_all[0]);

static const kb_layout_t *s_active = &kb_layout_us;

static const char *kconfig_default_name(void)
{
#if defined(CONFIG_SK_LAYOUT_US)
    return "US";
#elif defined(CONFIG_SK_LAYOUT_DE)
    return "DE";
#elif defined(CONFIG_SK_LAYOUT_FR)
    return "FR";
#elif defined(CONFIG_SK_LAYOUT_UA)
    return "UA";
#else
    return "US";
#endif
}

static void resolve_kconfig_default_once(void)
{
    static bool s_done;
    if (s_done) return;
    s_done = true;
    const kb_layout_t *l = kb_layout_by_name(kconfig_default_name());
    if (l) s_active = l;
}

const kb_layout_t *kb_layout_active(void)
{
    resolve_kconfig_default_once();
    return s_active;
}

bool kb_layout_set_active_by_name(const char *name)
{
    const kb_layout_t *l = kb_layout_by_name(name);
    if (!l) return false;
    s_active = l;
    return true;
}

int kb_layout_count(void) { return s_count; }

const kb_layout_t *kb_layout_by_index(int i)
{
    if (i < 0 || i >= s_count) return NULL;
    return s_all[i];
}

const kb_layout_t *kb_layout_by_name(const char *name)
{
    if (!name) return NULL;
    for (int i = 0; i < s_count; ++i) {
        if (strcmp(s_all[i]->name, name) == 0) return s_all[i];
    }
    return NULL;
}
