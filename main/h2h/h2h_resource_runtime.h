#pragma once
#include "h2h_resource.h"
#include "esp_err.h"
typedef struct { const unsigned char *bytes; h2h_resource_labels_t labels; uint32_t generation; } h2h_resource_view_t;
typedef bool (*h2h_resource_fetch_fn)(size_t offset, void *out, size_t length, void *ctx);
/* Empty by default. Production builds must explicitly supply approved public keys. */
bool h2h_resource_key_at(unsigned index, char id[33], unsigned char key[32]);
esp_err_t h2h_resource_runtime_start(void);
bool h2h_resource_runtime_available(void);
unsigned h2h_resource_runtime_keys(char ids[8][33]);
bool h2h_resource_runtime_prepare(const h2h_resource_manifest_t *m,uint64_t epoch,uint64_t model_generation,
                                  uint32_t revision,h2h_resource_fetch_fn fetch,void *ctx);
void h2h_resource_runtime_approve(uint64_t epoch,uint32_t revision,bool accepted);
void h2h_resource_runtime_builtin(uint64_t epoch,uint64_t model_generation,uint32_t revision);
bool h2h_resource_runtime_installed(uint64_t epoch,uint32_t revision,const unsigned char *sha256);
bool h2h_resource_runtime_failed(uint64_t epoch,uint32_t revision);
void h2h_resource_runtime_retry(void);
/* Hold returned view through LVGL rebuild. End releases old mappings only afterward. */
const h2h_resource_view_t *h2h_resource_runtime_view_begin(uint64_t epoch,uint64_t model_generation);
void h2h_resource_runtime_view_end(void);
