#ifndef ESP_HOST_STUBS_H
#define ESP_HOST_STUBS_H
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
static inline void host_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
#define ESP_LOGI(...) host_log(__VA_ARGS__)
#define ESP_LOGE(...) host_log(__VA_ARGS__)
#define ESP_LOGW(...) host_log(__VA_ARGS__)
#define ESP_LOGD(...) host_log(__VA_ARGS__)
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
typedef struct { uint8_t *data; size_t size; bool present; } host_queue;
typedef host_queue *QueueHandle_t;
static uint32_t stub_now;
static void (*stub_before_peek)(QueueHandle_t);
static void (*stub_on_delay)(void);
static inline int64_t esp_timer_get_time(void) { return (int64_t)stub_now * 1000; }
static inline QueueHandle_t xQueueCreate(unsigned n, size_t size) {
    assert(n == 1); host_queue *q = calloc(1, sizeof(*q)); assert(q);
    q->data = calloc(1, size); assert(q->data); q->size = size; return q;
}
static inline int xQueueOverwrite(QueueHandle_t q, const void *p) { memcpy(q->data,p,q->size); q->present=true; return 1; }
static inline int xQueuePeek(QueueHandle_t q, void *p, unsigned timeout) { (void)timeout; if(stub_before_peek)stub_before_peek(q); if(!q->present)return 0; memcpy(p,q->data,q->size); return 1; }
static inline int xQueueReceive(QueueHandle_t q, void *p, unsigned timeout) { int r=xQueuePeek(q,p,timeout); q->present=false; return r; }
static inline int xQueueReset(QueueHandle_t q) { q->present=false; return 1; }
static inline int xTaskCreate(void (*fn)(void*), const char*n, unsigned stack,void*a,unsigned priority,void*h) {
    (void)fn;(void)n;(void)stack;(void)a;(void)priority;(void)h; return 1;
}
static inline void vTaskDelay(unsigned ticks) { (void)ticks; if(stub_on_delay)stub_on_delay(); }
static inline esp_err_t nvs_flash_init(void) { return 0; }
static inline esp_err_t nimble_port_init(void) { return 0; }
static inline void nimble_port_run(void) {}
static inline void nimble_port_freertos_deinit(void) {}
static inline void nimble_port_freertos_init(void (*fn)(void*)) { (void)fn; }
typedef struct { uint8_t type; } ble_uuid_t;
typedef struct { ble_uuid_t u; uint8_t value[16]; } ble_uuid128_t;
#define BLE_UUID128_INIT(...) { .u={.type=128}, .value={__VA_ARGS__} }
#define BLE_HS_CONN_HANDLE_NONE 65535
#define BLE_GATT_ACCESS_OP_READ_CHR 1
#define BLE_GATT_ACCESS_OP_WRITE_CHR 2
#define BLE_GATT_CHR_F_NOTIFY 1
#define BLE_GATT_CHR_F_READ 2
#define BLE_GATT_CHR_F_WRITE 4
#define BLE_GATT_SVC_TYPE_PRIMARY 1
#define BLE_ATT_ERR_READ_NOT_PERMITTED 2
#define BLE_ATT_ERR_WRITE_NOT_PERMITTED 3
#define BLE_ATT_ERR_INSUFFICIENT_RES 17
#define BLE_ATT_ERR_VALUE_NOT_ALLOWED 19
#define BLE_HS_ADV_F_DISC_GEN 2
#define BLE_HS_ADV_F_BREDR_UNSUP 4
#define BLE_GAP_CONN_MODE_UND 1
#define BLE_GAP_DISC_MODE_GEN 1
#define BLE_HS_FOREVER -1
#define BLE_GAP_EVENT_CONNECT 1
#define BLE_GAP_EVENT_DISCONNECT 2
#define BLE_GAP_EVENT_SUBSCRIBE 3
#define BLE_GAP_EVENT_MTU 4
#define BLE_GAP_EVENT_ADV_COMPLETE 5
struct os_mbuf { uint8_t data[512]; uint16_t len; };
#define OS_MBUF_PKTLEN(om) ((om)->len)
struct ble_gatt_access_ctxt { uint8_t op; struct os_mbuf *om; };
typedef int (*access_fn)(uint16_t,uint16_t,struct ble_gatt_access_ctxt*,void*);
struct ble_gatt_chr_def { const ble_uuid_t *uuid; access_fn access_cb; unsigned flags; uint16_t *val_handle; };
struct ble_gatt_svc_def { unsigned type; const ble_uuid_t *uuid; struct ble_gatt_chr_def *characteristics; };
struct ble_hs_adv_fields { uint8_t flags; ble_uuid128_t *uuids128; uint8_t num_uuids128,uuids128_is_complete; const uint8_t *name; uint8_t name_len,name_is_complete; };
struct ble_gap_adv_params { uint8_t conn_mode,disc_mode; };
struct ble_gap_event {
    int type;
    struct { int status; uint16_t conn_handle; } connect;
    struct { uint16_t attr_handle; uint8_t cur_notify; } subscribe;
    struct { uint16_t value; } mtu;
};
static struct { void (*reset_cb)(int); void (*sync_cb)(void); } ble_hs_cfg;
static int (*stub_gap_cb)(struct ble_gap_event*,void*);
static unsigned stub_advertisements, stub_notifications;
static unsigned stub_disconnect_after;
static bool stub_fail_allocation;
static uint8_t stub_packets[32][512];
static size_t stub_packet_sizes[32];
static char stub_advertised_name[32];
static inline int ble_hs_util_ensure_addr(int a) { (void)a; return 0; }
static inline int ble_hs_id_infer_auto(int a,uint8_t*b) { (void)a; *b=0; return 0; }
static inline int ble_gap_adv_set_fields(const struct ble_hs_adv_fields *f) { assert(f->num_uuids128==1); return 0; }
static inline int ble_gap_adv_rsp_set_fields(const struct ble_hs_adv_fields *f) {
    memcpy(stub_advertised_name,f->name,f->name_len); stub_advertised_name[f->name_len]=0; return 0;
}
static inline int ble_gap_adv_start(uint8_t a,const void*b,int duration,const struct ble_gap_adv_params*p,int(*cb)(struct ble_gap_event*,void*),void*arg) {
    (void)a;(void)b;(void)duration;(void)p;(void)arg; stub_gap_cb=cb; ++stub_advertisements; return 0;
}
static inline int os_mbuf_append(struct os_mbuf *om,const void *p,uint16_t n) { if(om->len+n>512)return 1; memcpy(om->data+om->len,p,n); om->len=(uint16_t)(om->len+n); return 0; }
static inline int ble_hs_mbuf_to_flat(struct os_mbuf *om,void*p,size_t cap,uint16_t*n) {
    if(om->len>cap)return 1; memcpy(p,om->data,om->len);*n=om->len;return 0;
}
static inline struct os_mbuf *ble_hs_mbuf_from_flat(const void*p,uint16_t n) {
    if(stub_fail_allocation)return NULL; struct os_mbuf*om=calloc(1,sizeof(*om)); assert(om); memcpy(om->data,p,n);om->len=n;return om;
}
static inline int ble_gatts_notify_custom(uint16_t conn,uint16_t handle,struct os_mbuf*om) {
    (void)conn;(void)handle; assert(stub_notifications<32);
    memcpy(stub_packets[stub_notifications],om->data,om->len);stub_packet_sizes[stub_notifications]=om->len;++stub_notifications;free(om);
    if(stub_disconnect_after && stub_notifications==stub_disconnect_after) {struct ble_gap_event e={.type=BLE_GAP_EVENT_DISCONNECT};stub_gap_cb(&e,NULL);}
    return 0;
}
static inline void ble_svc_gap_init(void) {}
static inline void ble_svc_gatt_init(void) {}
static inline int ble_svc_gap_device_name_set(const char*n) {(void)n;return 0;}
static inline int ble_att_set_preferred_mtu(unsigned mtu) {assert(mtu==185);return 0;}
static inline int ble_gatts_count_cfg(const struct ble_gatt_svc_def*s) {
    for(size_t i=0;s[i].type;i++)for(size_t j=0;s[i].characteristics[j].uuid;j++)
        if(!s[i].characteristics[j].access_cb)return 1; /* Actual NimBLE sanity requirement. */
    return 0;
}
static inline int ble_gatts_add_svcs(const struct ble_gatt_svc_def*s) {
    for(size_t i=0;s[i].type;i++)for(size_t j=0;s[i].characteristics[j].uuid;j++)
        if(s[i].characteristics[j].val_handle)*s[i].characteristics[j].val_handle=(uint16_t)(42+j);
    return 0;
}
#endif
