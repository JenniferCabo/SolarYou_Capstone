/* Execute the actual ESP adapter with fake ESP/NimBLE APIs. No RF or scheduler. */
#include "../ESP_SolarYou/main/ble_telemetry.c"
#include <stdio.h>
#include <setjmp.h>

static unsigned assertions;
#define CHECK(x) do { assert(x); ++assertions; } while (0)

static jmp_buf sender_return;
static void stop_sender(void) { longjmp(sender_return, 1); }
static void run_sender_once(void)
{
    stub_notifications = 0;
    stub_on_delay = stop_sender;
    if (setjmp(sender_return) == 0) sender_task(NULL);
    stub_on_delay = NULL;
    CHECK(stub_notifications == 1U);
    stub_packets[0][stub_packet_sizes[0]] = 0;
}

static void publish_during_peek(QueueHandle_t queue)
{
    if (queue != latest_queue) return;
    stub_before_peek = NULL;
    ++stub_now;
    sy_telemetry_sample_t fresh = {
        .pitch = 42, .roll = 2, .imu_valid = true, .captured_ms = stub_now
    };
    ble_telemetry_publish(&fresh);
}

int main(void)
{
    CHECK(ble_telemetry_init() == ESP_OK);
    CHECK(ble_hs_cfg.sync_cb != NULL && ble_hs_cfg.reset_cb != NULL);
    ble_hs_cfg.sync_cb();
    CHECK(stub_advertisements == 1U);
#if CONFIG_SOLARYOU_RUNTIME_ESP_ONLY_TEST
    CHECK(strcmp(stub_advertised_name, SY_BLE_TEST_DEVICE_NAME) == 0);
#else
    CHECK(strcmp(stub_advertised_name, SY_BLE_DEVICE_NAME) == 0);
#endif
    CHECK(services[0].characteristics[0].access_cb != NULL);
    struct ble_gap_event event = {.type=BLE_GAP_EVENT_CONNECT,.connect={.status=0,.conn_handle=7}};
    CHECK(stub_gap_cb(&event, NULL) == 0 && session.connected && !session.subscribed);
    event.type=BLE_GAP_EVENT_SUBSCRIBE;
    event.subscribe.attr_handle=telemetry_handle; event.subscribe.cur_notify=1;
    stub_gap_cb(&event,NULL);
    CHECK(session.subscribed);

    sy_telemetry_sample_t sample = {.imu_valid=true,.pitch=1,.roll=2,.captured_ms=1};
    ble_telemetry_publish(&sample); sample.pitch=3; sample.captured_ms=2; ble_telemetry_publish(&sample);
    sy_telemetry_sample_t latest;
    CHECK(xQueuePeek(latest_queue,&latest,0) == pdTRUE && latest.pitch == 3 && latest.captured_ms == 2U);
    char json[SY_BLE_JSON_CAPACITY]; size_t length=sy_telemetry_format(json,sizeof(json),&sample);
    sy_ble_session_t state=session;
    CHECK(send_json(json,length,&state,connection,1));
    CHECK(stub_notifications > 1U);
    for(unsigned i=0;i<stub_notifications;i++) CHECK(stub_packet_sizes[i] <= 20U);

    struct os_mbuf buffer={0};
    const char *command="{\"set_rate_hz\":2}";
    memcpy(buffer.data,command,strlen(command));buffer.len=(uint16_t)strlen(command);
    struct ble_gatt_access_ctxt context={.op=BLE_GATT_ACCESS_OP_WRITE_CHR,.om=&buffer};
    CHECK(access_rate(7,43,&context,NULL) == 0 && session.rate_hz == 2U);
    command="{\"set_rate_hz\":3}";
    memcpy(buffer.data,command,strlen(command));buffer.len=(uint16_t)strlen(command);
    CHECK(access_rate(7,43,&context,NULL) == BLE_ATT_ERR_VALUE_NOT_ALLOWED && session.rate_hz == 2U);
    context.op=BLE_GATT_ACCESS_OP_READ_CHR;buffer.len=0;
    CHECK(access_rate(7,43,&context,NULL) == 0);
    CHECK(buffer.len == strlen("{\"rate_hz\":2}") && memcmp(buffer.data,"{\"rate_hz\":2}",buffer.len)==0);

    /* A disconnect during a fragment burst aborts the remaining stale message. */
    stub_notifications=0;stub_disconnect_after=1;state=session;
    CHECK(!send_json(json,length,&state,connection,2));
    CHECK(stub_notifications==1U && !session.connected && !session.subscribed);
    CHECK(stub_advertisements==2U);
    event.type=BLE_GAP_EVENT_CONNECT;event.connect.status=0;event.connect.conn_handle=7;stub_gap_cb(&event,NULL);
    CHECK(session.rate_hz==2U && session.mtu==23U && !session.subscribed);
    event.type=BLE_GAP_EVENT_SUBSCRIBE;event.subscribe.attr_handle=telemetry_handle;event.subscribe.cur_notify=1;stub_gap_cb(&event,NULL);
    event.type=BLE_GAP_EVENT_MTU;event.mtu.value=185;stub_gap_cb(&event,NULL);
    CHECK(session.mtu==185U);
    stub_notifications=0;stub_disconnect_after=0;state=session;
    CHECK(send_json(json,length,&state,connection,3) && stub_notifications==1U);
    CHECK(stub_packet_sizes[0]==length && stub_packets[0][0]=='{');
    stub_fail_allocation=true;
    CHECK(!send_json(json,length,&state,connection,4));
    CHECK(stub_notifications==1U);

    /* A newer sample can arrive on the other core after the sender checks its
     * deadline but before it copies the queue. It must not become null. */
    stub_fail_allocation=false;
    xQueueReset(reply_queue);
    stub_now=100U; session.next_due_ms=stub_now;
    stub_before_peek=publish_during_peek;
    run_sender_once();
    CHECK(strstr((char *)stub_packets[0], "\"pitch\":42.00") != NULL);
    CHECK(strstr((char *)stub_packets[0], "\"roll\":2.00") != NULL);

    /* Real stale values remain null, and freshness survives timer rollover. */
    stub_now=1200U; session.next_due_ms=stub_now;
    run_sender_once();
    CHECK(strstr((char *)stub_packets[0], "\"pitch\":null") != NULL);
    CHECK(strstr((char *)stub_packets[0], "\"roll\":null") != NULL);
    sample.captured_ms=UINT32_MAX-24U; sample.imu_valid=true;
    ble_telemetry_publish(&sample);
    stub_now=25U; session.next_due_ms=stub_now;
    run_sender_once();
    CHECK(strstr((char *)stub_packets[0], "\"pitch\":3.00") != NULL);

    ble_hs_cfg.reset_cb(99);
    CHECK(!session.connected && connection==BLE_HS_CONN_HANDLE_NONE);
    printf("%u ESP adapter assertions passed (fake ESP/NimBLE APIs; no RF)\n",assertions);
    return 0;
}
