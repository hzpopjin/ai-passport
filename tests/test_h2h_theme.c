#include "h2h_theme.h"
#include <assert.h>
#include <string.h>

int main(void) {
    unsigned char data[H2H_THEME_PACK_SIZE] = {'H','2','H','S','K','I','N',0,1,2,2,0,0,0,0,0};
    h2h_theme_t theme;
    assert(h2h_theme_parse(data, sizeof(data), &theme));
    assert(theme.outfit == 2 && theme.room == 2);
    h2h_model_t model;
    h2h_init(&model, 1, 1, 7);
    h2h_theme_apply(&model, &theme);
    assert(model.save.outfit == 2 && model.save.room == 2 && model.dirty);
    h2h_save_t save;
    h2h_save_snapshot(&model, &save);
    h2h_model_t restored;
    h2h_init(&restored, 1, 1, 9);
    assert(h2h_load(&restored, &save, sizeof(save)));
    assert(restored.save.outfit == 2 && restored.save.room == 2 && restored.save.cards == 0);
    data[8] = 2; assert(!h2h_theme_parse(data, sizeof(data), &theme)); data[8] = 1;
    data[9] = 3; assert(!h2h_theme_parse(data, sizeof(data), &theme)); data[9] = 2;
    data[12] = 1; assert(!h2h_theme_parse(data, sizeof(data), &theme)); data[12] = 0;
    assert(!h2h_theme_parse(data, sizeof(data) - 1, &theme));
    return 0;
}
