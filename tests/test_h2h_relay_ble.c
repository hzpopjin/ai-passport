#include <assert.h>
#include <stdio.h>
#include "../main/h2h/h2h_ble.c"
static int access(unsigned chr,int op,const uint8_t *p,size_t n,struct os_mbuf *m){memset(m,0,sizeof(*m));if(n){memcpy(m->bytes,p,n);m->length=n;}struct ble_gatt_access_ctxt c={op,&services[0].characteristics[chr],m};return access_characteristic(1,0,&c,NULL);}
int main(void){struct os_mbuf m;assert(h2h_ble_start("0123456789abcdef0123456789abcdef")==ESP_OK);assert(ble_hs_cfg.sm_sc&&ble_hs_cfg.sm_mitm&&ble_hs_cfg.sm_bonding);assert(test_store_initializations==1&&ble_hs_cfg.store_status_cb==ble_store_util_status_rr);assert(ble_hs_cfg.sm_our_key_dist==(BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID)&&ble_hs_cfg.sm_their_key_dist==ble_hs_cfg.sm_our_key_dist);uint32_t connection=h2h_ble_relay_connection_generation();assert(connection);
 int flags=services[0].characteristics[2].flags;assert((flags&(BLE_GATT_CHR_F_READ_AUTHEN|BLE_GATT_CHR_F_WRITE_AUTHEN|BLE_GATT_CHR_F_WRITE))==(BLE_GATT_CHR_F_READ_AUTHEN|BLE_GATT_CHR_F_WRITE_AUTHEN|BLE_GATT_CHR_F_WRITE));
 test_authenticated=false;assert(access(1,BLE_GATT_ACCESS_OP_READ_CHR,NULL,0,&m)==BLE_ATT_ERR_INSUFFICIENT_AUTHEN);assert(access(2,BLE_GATT_ACCESS_OP_READ_CHR,NULL,0,&m)==BLE_ATT_ERR_INSUFFICIENT_AUTHEN);uint8_t begin[]={0x10,1,0,1,0};assert(access(2,BLE_GATT_ACCESS_OP_WRITE_CHR,begin,5,&m)==BLE_ATT_ERR_INSUFFICIENT_AUTHEN);
 test_authenticated=true;assert(access(1,BLE_GATT_ACCESS_OP_READ_CHR,NULL,0,&m)==0&&m.length==16&&m.bytes[0]==1&&m.bytes[15]==0xef);
 assert(!h2h_ble_authenticated_for_connection(connection));
 struct ble_gap_event connected_event={.type=BLE_GAP_EVENT_CONNECT,.connect={.status=0,.conn_handle=1}};
 assert(gap_event(&connected_event,NULL)==0&&!h2h_ble_authenticated_for_connection(connection));
 struct ble_gap_event pin_event={.type=BLE_GAP_EVENT_PASSKEY_ACTION,.passkey={.params={.action=BLE_SM_IOACT_DISP},.conn_handle=1}};
 uint32_t ignored_pin;assert(gap_event(&pin_event,NULL)==0&&h2h_ble_passkey(&ignored_pin)&&test_passkey_injections==1);
 struct ble_gap_event encrypted_event={.type=BLE_GAP_EVENT_ENC_CHANGE,.enc_change={.status=0,.conn_handle=1}};
 assert(gap_event(&encrypted_event,NULL)==0&&h2h_ble_authenticated_for_connection(connection));
 assert(!h2h_ble_passkey(&ignored_pin));
 assert(!h2h_ble_authenticated_for_connection(connection+1));
 assert(access(2,BLE_GATT_ACCESS_OP_WRITE_CHR,begin,5,&m)==0);uint8_t data[]={0x11,1,0,0,0,'x'};assert(access(0,BLE_GATT_ACCESS_OP_WRITE_CHR,data,6,&m)==0);uint8_t execute[]={0x12,1,0};assert(access(2,BLE_GATT_ACCESS_OP_WRITE_CHR,execute,3,&m)==0);uint32_t generation;size_t length;char *bytes=h2h_ble_relay_take(&generation,&length);assert(bytes&&length==1&&bytes[0]=='x');strcpy(bytes,"reply");h2h_ble_relay_finish(generation,5);
 uint8_t select[]={0x13,1,0,0,0};assert(access(0,BLE_GATT_ACCESS_OP_WRITE_CHR,select,5,&m)==0);assert(access(2,BLE_GATT_ACCESS_OP_READ_CHR,NULL,0,&m)==0&&m.length==13&&m.bytes[1]==1&&!memcmp(m.bytes+8,"reply",5));
 assert(h2h_ble_relay_connection_generation()==connection);struct ble_gap_event event={.type=BLE_GAP_EVENT_DISCONNECT};assert(gap_event(&event,NULL)==0);assert(h2h_ble_relay_disconnected()&&h2h_ble_relay_connection_generation()!=connection);assert(access(2,BLE_GATT_ACCESS_OP_READ_CHR,NULL,0,&m)==0&&m.length==8&&m.bytes[1]==0);
 connection=h2h_ble_relay_connection_generation();assert(gap_event(&connected_event,NULL)==0&&!h2h_ble_passkey(&ignored_pin));
 assert(gap_event(&encrypted_event,NULL)==0&&h2h_ble_authenticated_for_connection(connection)&&!h2h_ble_passkey(&ignored_pin));
 assert(test_passkey_injections==1&&test_security_requests==2);
 struct ble_gap_event repeated_pairing={.type=BLE_GAP_EVENT_REPEAT_PAIRING};
 assert(gap_event(&repeated_pairing,NULL)==BLE_GAP_REPEAT_PAIRING_IGNORE&&test_store_initializations==1&&test_passkey_injections==1);
 assert(h2h_ble_stop()==ESP_OK);puts("Actual BLE aa01/aa03 write, authenticated identity/read, SC/MITM/bond-store configuration, first PIN versus bonded ENC_CHANGE callbacks and disconnect fencing: PASS (NVS/iOS radio persistence requires device test)");return 0;}
