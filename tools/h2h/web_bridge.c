/* In-process snapshots never come from HTTP clients. Access is serialized by
 * the WSGI application's lock; durable progress uses h2h_save_t validation. */
#include "preview_bridge.c"
void web_init(unsigned tracks,unsigned songs,unsigned seed) { h2h_init(&model,tracks,songs,seed); }
unsigned web_snapshot(void *bytes) { memcpy(bytes,&model,sizeof(model)); return sizeof(model); }
int web_restore(const void *bytes,unsigned size) {
    if(size!=sizeof(model)) return 0;
    memcpy(&model,bytes,size); return 1;
}
