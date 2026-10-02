#include "h2h_theme.h"
#include <string.h>

bool h2h_theme_parse(const void *bytes, size_t length, h2h_theme_t *theme) {
    if (!bytes || !theme || length != H2H_THEME_PACK_SIZE) return false;
    const uint8_t *p = bytes;
    static const uint8_t magic[8] = {'H','2','H','S','K','I','N',0};
    if (memcmp(p, magic, sizeof(magic)) != 0 || p[8] != 1 || p[9] > 2 || p[10] > 2 || p[11] != 0) return false;
    for (size_t i = 12; i < H2H_THEME_PACK_SIZE; ++i) if (p[i]) return false;
    theme->outfit = p[9];
    theme->room = p[10];
    return true;
}

void h2h_theme_apply(h2h_model_t *model, const h2h_theme_t *theme) {
    if (!model || !theme) return;
    model->save.outfit = theme->outfit;
    model->save.room = theme->room;
    model->dirty = true;
}
