/*
 * The Cyphal node. See cyphal.h, and manual/use/cyphal.md for what it does.
 *
 * One task does everything libcanard is asked to do (libcanard is not thread-safe): it reads
 * frames from canbus's receive queue, runs the timers (the listen before going online, the
 * plug-and-play request, the heartbeat, a look at the bus for bus-off) and hands libcanard's
 * transmit queue to canbus one frame at a time, in libcanard's priority order. It sleeps until its
 * next timer or the next frame. The console only starts and stops it (under s_lock: a stop wakes
 * the task and waits for it to end) and reads counters.
 *
 * States: a start goes to LISTENING (a fixed node-ID) or WAITING (plug-and-play); LISTENING goes
 * ONLINE after CYPHAL_LISTEN_MS, or IN_USE if a frame from its node-ID arrives first; ONLINE goes
 * IN_USE the same way. IN_USE sends nothing until the next start. The controller runs in normal
 * mode throughout, so the board always acknowledges other nodes' frames, even while it sends
 * nothing.
 *
 * WAITING is anonymous: libcanard's node-ID is unset, the only subscription is to
 * uavcan.pnp.NodeIDAllocationData.1.0, and the only transfers sent are its requests, single frames
 * at random intervals, one queued at a time (the DSDL's rules A to D; cyphal_util.h). The node
 * listens from its start, as a fixed node-ID does, but for a heartbeat's longest period
 * (CYPHAL_PNP_LISTEN_US) and while it asks. An allocation for this board's pseudo-ID ends the
 * asking (rule D: no more requests, and no more allocations heard, so the first wins) and is held,
 * still WAITING, until that listen is over; then it goes ONLINE, unless the node-ID it gives has
 * been heard on the bus within a heartbeat's offline timeout (cyphal_pnp_verdict()): then IN_USE, as
 * a collision while running would be, without ever sending as that node-ID. Nothing is saved (D5).
 *
 * Nothing is sent before every frame already received has been checked for our node-ID (D9): the
 * task empties the receive queue before it hands a frame to canbus, every time, after the wait for
 * room and just before the hand-over. So a frame from another node with our node-ID stops the node
 * before its next frame; the one frame already inside canbus still goes. Taking up an allocated
 * node-ID is the same: the frames before that moment were counted as heard (cyphal_seen_t), and
 * those after it in the queue are checked for the new node-ID before anything goes out as it.
 * Both listens end by the frames' stamps, not by the clock (cyphal_listen_t): a task held off past
 * the end (a flash write stalls both cores) still checks the frames from before the end first.
 *
 * Sending: canbus is started with one send slot, so libcanard's queue goes to the controller one
 * frame at a time and its priority order holds; canbus_wait_room() waits for that slot and wakes
 * the task as soon as the frame before is on the bus. Every transfer gets a deadline
 * (TX_DEADLINE_US) when queued, applied before canbus_send() since canbus can't take a frame
 * back: a transfer whose deadline passes before its next frame is handed over is dropped whole.
 * So one frame at most goes out late: the one inside canbus while nobody acknowledged it, or
 * while the controller was bus-off.
 *
 * Memory: the node's state and libcanard's heap (CONFIG_CYPHAL_HEAP_SIZE, an IDF multi_heap) are
 * in PSRAM, allocated at the first start and kept; each start begins the heap empty, so a stop
 * needs to free nothing. Only the task's stack and the queues canbus owns are internal RAM.
 */
#include <string.h>
#include "canard.h"
#include "canbus.h"
#include "cyphal.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "multi_heap.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "uavcan/node/GetInfo_1_0.h"
#include "uavcan/node/Heartbeat_1_0.h"
#include "uavcan/pnp/NodeIDAllocationData_1_0.h"

static const char *TAG = "cyphal";

#define NVS_NS          "cyphal"
#define KEY_NODE_ID     "node_id"       /* u8: 0-127, or 255 for plug-and-play */
#define KEY_NAME        "name"          /* str */
#define KEY_BITRATE     "bitrate"       /* u32 */

#define TX_QUEUE_FRAMES     64          /* libcanard's transmit queue; see CONFIG_CYPHAL_HEAP_SIZE */
#define HEARTBEAT_US        1000000
#define TX_DEADLINE_US      1000000     /* a heartbeat period, for heartbeats and responses alike */
#define BUS_CHECK_US        1000000     /* how often the task looks for bus-off */
#define RX_BUDGET           64          /* frames read before the timers get a look (nothing is sent until the queue is empty) */

_Static_assert(sizeof(CONFIG_CYPHAL_UNIQUE_ID_PREFIX) == CYPHAL_UID_PREFIX_SIZE + 1,
               "CONFIG_CYPHAL_UNIQUE_ID_PREFIX is exactly 8 characters");

typedef struct {
    struct CanardInstance canard;
    struct CanardTxQueue txq;
    struct CanardRxSubscription getinfo;    /* must not move while subscribed: s_node is never freed */
    struct CanardRxSubscription allocation; /* NodeIDAllocationData, while waiting; the same */
    uint8_t getinfo_response[uavcan_node_GetInfo_Response_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t getinfo_response_size;           /* serialized once per start: nothing in it changes */
    uint8_t node_id;                        /* the one it runs, or listens, on; CYPHAL_NODE_ID_AUTO while waiting */
    uint8_t allocator;                      /* the node that gave it, with plug-and-play (s_run.node_id AUTO) */
    uint8_t held_id;                        /* allocated, while the listen goes on; CYPHAL_NODE_ID_AUTO for none */
    int64_t held_us;                        /* the allocation's stamp */
    CanardTransferID heartbeat_tid, request_tid;
    cyphal_listen_t listen;                 /* from the start: a fixed node-ID's before going online, plug-and-play's before taking one up */
    cyphal_seen_t seen;                     /* when each node-ID was last heard while waiting: an allocation's check */
    int64_t next_request_us;                /* plug-and-play's request timer (Trequest), while waiting with nothing held */
    int64_t next_heartbeat_us, next_bus_check_us;
    uint32_t bus_off_seen;                  /* canbus's bus-off count, as last looked at */
} node_t;

static SemaphoreHandle_t s_lock;        /* settings, start, stop and status against each other */
static SemaphoreHandle_t s_task_ended;  /* given by the task as it ends */
static cyphal_settings_t s_saved;       /* from the next start */
static cyphal_settings_t s_run;         /* as the node started */
static int s_tx = -1, s_rx = -1;        /* the pins of the last start */
static uint8_t s_uid[CYPHAL_UNIQUE_ID_SIZE];
static uint64_t s_pseudo_id;            /* plug-and-play's, from s_uid */
static node_t *s_node;                  /* from the first start; never freed */
static void *s_heap_mem;                /* CONFIG_CYPHAL_HEAP_SIZE bytes, from the first start */
static multi_heap_handle_t s_heap;      /* in s_heap_mem, made again at each start */
static size_t s_heap_free0;             /* its free size when empty */
static canbus_handle_t s_bus;           /* the controller's run, from a start to its stop; 0 for none */
static TaskHandle_t s_task;
static volatile bool s_running;         /* the task's to go on; a stop clears it */
static volatile bool s_receiving;       /* the task is in canbus_receive(), where a stop may wake it */
static portMUX_TYPE s_wake_lock = portMUX_INITIALIZER_UNLOCKED;    /* s_receiving's clear against the wake */
static volatile cyphal_state_t s_state = CYPHAL_OFF;
static volatile esp_err_t s_error;
static bool s_stop_failed;              /* CYPHAL_FAILED came from a restart's canbus_stop(); under s_lock */
static volatile int64_t s_started_us;
/* Since the start; written by the task, read whole by anyone */
static volatile uint32_t s_tr_sent, s_tr_received, s_tr_dropped, s_frames_queued, s_heap_failed;

static void lock(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_lock);
}

/* ------------------------------------------------------------------ settings */

static bool bitrate_valid(uint32_t b)
{
    return b == 125000 || b == 250000 || b == 500000 || b == 1000000;
}

static void defaults(cyphal_settings_t *s)
{
    *s = (cyphal_settings_t){ .node_id = CYPHAL_NODE_ID_AUTO, .bitrate = CANBUS_DEFAULT_BITRATE };
    char why[96];
    if (cyphal_name_valid(CONFIG_CYPHAL_DEFAULT_NAME, why, sizeof(why))) {
        strlcpy(s->name, CONFIG_CYPHAL_DEFAULT_NAME, sizeof(s->name));
    } else {
        ESP_LOGW(TAG, "CONFIG_CYPHAL_DEFAULT_NAME is no name (%s): \"node\" instead", why);
        strlcpy(s->name, "node", sizeof(s->name));
    }
}

/* All three, every time: a backup of a board that has changed one holds them all */
static esp_err_t save(const cyphal_settings_t *s)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(h, KEY_NODE_ID, s->node_id);
    if (err == ESP_OK) {
        err = nvs_set_str(h, KEY_NAME, s->name);
    }
    if (err == ESP_OK) {
        err = nvs_set_u32(h, KEY_BITRATE, s->bitrate);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

/* What was saved, over the defaults: a value that doesn't read is skipped, with a warning */
static void load(cyphal_settings_t *s)
{
    defaults(s);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint8_t id;
    if (nvs_get_u8(h, KEY_NODE_ID, &id) == ESP_OK) {
        if (id <= CYPHAL_NODE_ID_MAX || id == CYPHAL_NODE_ID_AUTO) {
            s->node_id = id;
        } else {
            ESP_LOGW(TAG, "saved node-ID %u isn't one: plug-and-play", id);
        }
    }
    char name[CYPHAL_NAME_MAX + 1], why[96];
    size_t len = sizeof(name);
    esp_err_t err = nvs_get_str(h, KEY_NAME, name, &len);
    if (err == ESP_OK && cyphal_name_valid(name, why, sizeof(why))) {
        strlcpy(s->name, name, sizeof(s->name));
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "the saved name doesn't do (%s): %s", err == ESP_OK ? why : esp_err_to_name(err), s->name);
    }
    uint32_t bitrate;
    if (nvs_get_u32(h, KEY_BITRATE, &bitrate) == ESP_OK) {
        if (bitrate_valid(bitrate)) {
            s->bitrate = bitrate;
        } else {
            ESP_LOGW(TAG, "saved bitrate %lu isn't one of 125k, 250k, 500k, 1M: 1M", (unsigned long)bitrate);
        }
    }
    nvs_close(h);
}

/* New saved settings, under s_lock: in NVS first, then in use from the next start */
static esp_err_t commit(const cyphal_settings_t *s)
{
    const esp_err_t err = save(s);
    if (err == ESP_OK) {
        s_saved = *s;
    }
    return err;
}

esp_err_t cyphal_set_node_id(uint8_t node_id)
{
    if (node_id > CYPHAL_NODE_ID_MAX && node_id != CYPHAL_NODE_ID_AUTO) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    lock();
    cyphal_settings_t s = s_saved;
    s.node_id = node_id;
    const esp_err_t err = commit(&s);
    unlock();
    return err;
}

esp_err_t cyphal_set_name(const char *name, char *why, size_t why_len)
{
    if (!cyphal_name_valid(name, why, why_len)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    lock();
    cyphal_settings_t s = s_saved;
    strlcpy(s.name, name, sizeof(s.name));
    const esp_err_t err = commit(&s);
    unlock();
    return err;
}

esp_err_t cyphal_set_bitrate(uint32_t bitrate)
{
    if (!bitrate_valid(bitrate)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    lock();
    cyphal_settings_t s = s_saved;
    s.bitrate = bitrate;
    const esp_err_t err = commit(&s);
    unlock();
    return err;
}

esp_err_t cyphal_reset_settings(void)
{
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    cyphal_settings_t s;
    defaults(&s);
    lock();
    const esp_err_t err = commit(&s);
    unlock();
    return err;
}

esp_err_t cyphal_init(void)
{
    if (s_lock != NULL) {
        return ESP_OK;
    }
    s_lock = xSemaphoreCreateMutex();
    s_task_ended = xSemaphoreCreateBinary();
    if (s_lock == NULL || s_task_ended == NULL) {
        return ESP_ERR_NO_MEM;
    }
    load(&s_saved);
    /* The factory MAC, burnt into eFuse: the same after a reflash or a settings wipe */
    uint8_t mac[6] = { 0 };
    esp_err_t err = esp_efuse_mac_get_default(mac);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "no factory MAC (%s): the unique ID ends in zeros", esp_err_to_name(err));
    }
    cyphal_unique_id(s_uid, CONFIG_CYPHAL_UNIQUE_ID_PREFIX, mac);
    s_pseudo_id = cyphal_pseudo_id(s_uid);
    return ESP_OK;
}

/* ------------------------------------------------------------------ libcanard's heap */

/* libcanard asks for max_align_t, 8 bytes here: the uint64_t in its queue items. It asks for 0 bytes for
 * an anonymous frame with nothing but its tail byte (canard.c, rxAcceptFrame) and takes NULL as out of
 * memory, which multi_heap gives for 0: so a byte instead, a block heap_free() frees like any other */
static void *heap_alloc(void *ref, size_t size)
{
    (void)ref;
    void *p = multi_heap_aligned_alloc(s_heap, size > 0 ? size : 1, 8);
    if (p == NULL) {
        s_heap_failed++;
    }
    return p;
}

static void heap_free(void *ref, size_t size, void *p)
{
    (void)ref;
    (void)size;
    multi_heap_free(s_heap, p);
}

/* The node's state and heap: PSRAM if there is any, at the first start; emptied at each */
static esp_err_t make_node(void)
{
    if (s_node == NULL) {
        s_node = heap_caps_malloc_prefer(sizeof(node_t), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_8BIT);
    }
    if (s_heap_mem == NULL) {
        s_heap_mem = heap_caps_malloc_prefer(CONFIG_CYPHAL_HEAP_SIZE, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                             MALLOC_CAP_8BIT);
    }
    if (s_node == NULL || s_heap_mem == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_heap = multi_heap_register(s_heap_mem, CONFIG_CYPHAL_HEAP_SIZE);
    if (s_heap == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_heap_free0 = multi_heap_free_size(s_heap);
    memset(s_node, 0, sizeof(*s_node));
    const struct CanardMemoryResource mem = { .user_reference = NULL, .deallocate = heap_free, .allocate = heap_alloc };
    s_node->canard = canardInit(mem);
    s_node->txq = canardTxInit(TX_QUEUE_FRAMES, CANARD_MTU_CAN_CLASSIC, mem);
    return ESP_OK;
}

/* uavcan.node.GetInfo's response, serialized: the name as the node starts */
static esp_err_t serialize_getinfo(node_t *n)
{
    /* All zero is the type's default (no CRC, no certificate). Not _initialize_(): GCC 15 raises false
     * array-bounds errors in it (tools/gen_dsdl.py, TYPES) */
    uavcan_node_GetInfo_Response_1_0 r = { 0 };
    r.protocol_version = (uavcan_node_Version_1_0){ CANARD_CYPHAL_SPECIFICATION_VERSION_MAJOR,
                                                     CANARD_CYPHAL_SPECIFICATION_VERSION_MINOR };
    r.hardware_version = (uavcan_node_Version_1_0){ CONFIG_CYPHAL_HARDWARE_VERSION_MAJOR,
                                                     CONFIG_CYPHAL_HARDWARE_VERSION_MINOR };
    cyphal_software_version(esp_app_get_description()->version, &r.software_version.major,
                            &r.software_version.minor, &r.software_vcs_revision_id);
    memcpy(r.unique_id, s_uid, sizeof(r.unique_id));
    r.name.count = strlen(s_run.name);
    memcpy(r.name.elements, s_run.name, r.name.count);
    n->getinfo_response_size = sizeof(n->getinfo_response);
    return uavcan_node_GetInfo_Response_1_0_serialize_(&r, n->getinfo_response, &n->getinfo_response_size) < 0
               ? ESP_ERR_INVALID_SIZE : ESP_OK;
}

/* ------------------------------------------------------------------ the task */

/* Queue a transfer for the bus, with its deadline */
static void push(node_t *n, const struct CanardTransferMetadata *meta, const void *data, size_t size, int64_t now)
{
    /* now_usec 0: expiry is flush()'s, which drops whole transfers and counts them */
    const int32_t frames = canardTxPush(&n->txq, &n->canard, (CanardMicrosecond)(now + TX_DEADLINE_US), meta,
                                        (struct CanardPayload){ .size = size, .data = data }, 0, NULL);
    if (frames < 0) {
        s_tr_dropped++;     /* the queue is full (TX_QUEUE_FRAMES) or the heap is */
    }
}

/* An item and the rest of its transfer, out of the queue and freed: they are all still in it,
 * behind this one, since the frames of one transfer share a CAN ID and leave in order */
static void free_transfer(node_t *n, struct CanardTxQueueItem *item)
{
    while (item != NULL) {
        struct CanardTxQueueItem *next = item->next_in_transfer;
        canardTxFree(&n->txq, &n->canard, canardTxPop(&n->txq, item));
        item = next;
    }
}

/* Everything queued, out and freed: how many transfers that was */
static uint32_t clear_tx(node_t *n)
{
    uint32_t transfers = 0;
    struct CanardTxQueueItem *item;
    for (; (item = canardTxPeek(&n->txq)) != NULL; transfers++) {
        free_transfer(n, item);
    }
    return transfers;
}

static void heartbeat(node_t *n, int64_t now)
{
    const uavcan_node_Heartbeat_1_0 hb = {
        .uptime = (uint32_t)((now - s_started_us) / 1000000),
        .health = { uavcan_node_Health_1_0_NOMINAL },
        .mode = { uavcan_node_Mode_1_0_OPERATIONAL },
    };
    uint8_t buf[uavcan_node_Heartbeat_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t size = sizeof(buf);
    if (uavcan_node_Heartbeat_1_0_serialize_(&hb, buf, &size) < 0) {
        return;
    }
    const struct CanardTransferMetadata meta = {
        .priority = CanardPriorityNominal,
        .transfer_kind = CanardTransferKindMessage,
        .port_id = uavcan_node_Heartbeat_1_0_FIXED_PORT_ID_,
        .remote_node_id = CANARD_NODE_ID_UNSET,
        .transfer_id = n->heartbeat_tid++,
    };
    push(n, &meta, buf, size, now);
}

/* Plug-and-play's request (rule B). Anonymous, so a single frame, whose CAN ID libcanard ends with a
 * 7-bit discriminator made from the payload's CRC, as the spec asks: another allocatee, with another
 * pseudo-ID, has another CAN ID 127 times in 128, and the two don't collide bit for bit.
 *
 * A limit: the frame is retransmitted until acknowledged, like every other. pycyphal (and, as recalled,
 * the Cyphal/CAN spec) want anonymous frames sent without automatic retransmission, so that two
 * allocatees whose requests do share a CAN ID collide once, not in lockstep until one of them is
 * error-passive. IDF 6.1's TWAI driver can't do that for one frame: single-shot is the whole node's
 * (twai_onchip_node_config_t.fail_retry_cnt, applied to every frame in esp_twai_onchip.c's
 * _node_start_trans()), fixed until the node is deleted and made again. */
static void request(node_t *n, int64_t now)
{
    uint8_t buf[uavcan_pnp_NodeIDAllocationData_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    const size_t size = cyphal_pnp_request(buf, sizeof(buf), s_pseudo_id);
    if (size == 0) {
        return;
    }
    const struct CanardTransferMetadata meta = {
        .priority = CanardPriorityNominal,
        .transfer_kind = CanardTransferKindMessage,
        .port_id = uavcan_pnp_NodeIDAllocationData_1_0_FIXED_PORT_ID_,
        .remote_node_id = CANARD_NODE_ID_UNSET,
        .transfer_id = n->request_tid++,
    };
    push(n, &meta, buf, size, now);
}

/* Heartbeat and GetInfo on n->node_id, from now: after the listen, or an allocation */
static void go_online(node_t *n, int64_t now)
{
    n->canard.node_id = n->node_id;
    canardRxSubscribe(&n->canard, CanardTransferKindRequest, uavcan_node_GetInfo_1_0_FIXED_PORT_ID_,
                      uavcan_node_GetInfo_Request_1_0_EXTENT_BYTES_, CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
                      &n->getinfo);
    n->next_heartbeat_us = now;
    s_state = CYPHAL_ONLINE;
    if (s_run.node_id == CYPHAL_NODE_ID_AUTO) {     /* online with plug-and-play only by an allocation */
        ESP_LOGI(TAG, "node %u (allocated by node %u): online", n->node_id, n->allocator);
    } else {
        ESP_LOGI(TAG, "node %u (fixed): online", n->node_id);
    }
}

/* Another node has our node-ID: send nothing more, until the next start */
static void stop_in_use(node_t *n)
{
    s_state = CYPHAL_IN_USE;
    s_tr_dropped += clear_tx(n);
    n->canard.node_id = CANARD_NODE_ID_UNSET;
    ESP_LOGW(TAG, "node-ID %u is in use by another node; Cyphal stopped", n->node_id);
}

/* The node-ID an allocation gave, once the listen since the start is over (cyphal_pnp_verdict()):
 * online on it, or stopped if another node has been heard with it lately (an allocator with a stale
 * or wiped table), as a collision while running would stop it (D9), but without a frame sent as that
 * node-ID. Until then nothing: still WAITING, asking no more */
static void take_up(node_t *n, int64_t now)
{
    const cyphal_pnp_verdict_t v = cyphal_pnp_verdict(&n->seen, &n->listen, n->held_id, n->held_us);
    if (v == CYPHAL_PNP_HOLD) {
        return;
    }
    n->node_id = n->held_id;
    n->held_id = CYPHAL_NODE_ID_AUTO;
    if (v == CYPHAL_PNP_TAKEN) {
        stop_in_use(n);
    } else {
        go_online(n, now);
    }
}

/* A NodeIDAllocationData received while waiting: rules C and D */
static void on_allocation(node_t *n, const struct CanardRxTransfer *t, int64_t now)
{
    uint8_t id = 0;
    const cyphal_pnp_result_t r = cyphal_pnp_accept(t->payload.data, t->payload.size,
                                                    t->metadata.remote_node_id == CANARD_NODE_ID_UNSET, s_pseudo_id, &id);
    if (r == CYPHAL_PNP_INVALID) {
        return;
    }
    n->next_request_us = now + cyphal_pnp_interval_us(esp_random());     /* rule C: any allocation message */
    if (r != CYPHAL_PNP_ALLOCATED) {
        return;
    }
    /* Rule D: the request timer stops (timers() asks only while nothing is held) and the subscription
     * ends, so a later allocation, from this allocator or another, isn't heard. A request still queued is
     * pointless; one already inside canbus still goes, and an allocator answers it with the same node-ID,
     * which nobody is listening for any more */
    canardRxUnsubscribe(&n->canard, CanardTransferKindMessage, uavcan_pnp_NodeIDAllocationData_1_0_FIXED_PORT_ID_);
    clear_tx(n);
    n->held_id = id;
    n->held_us = now;
    n->allocator = (uint8_t)t->metadata.remote_node_id;
    take_up(n, now);
}

static void answer_getinfo(node_t *n, const struct CanardRxTransfer *t, int64_t now)
{
    /* The same priority and transfer-ID as the request, back to whoever asked */
    const struct CanardTransferMetadata meta = {
        .priority = t->metadata.priority,
        .transfer_kind = CanardTransferKindResponse,
        .port_id = uavcan_node_GetInfo_1_0_FIXED_PORT_ID_,
        .remote_node_id = t->metadata.remote_node_id,
        .transfer_id = t->metadata.transfer_id,
    };
    push(n, &meta, n->getinfo_response, n->getinfo_response_size, now);
}

static void on_frame(node_t *n, const canbus_frame_t *f)
{
    const int64_t stamp = (int64_t)f->timestamp_us;
    cyphal_listen_frame(&n->listen, stamp);
    const cyphal_state_t state = s_state;
    if (state == CYPHAL_WAITING) {
        /* For an allocation's check, made only while waiting (an allocation held included); waiting
         * comes only at a start, which clears the table, so no frame from before it matters */
        cyphal_seen_frame(&n->seen, cyphal_frame_source(f->id, f->ext, f->rtr, f->len), stamp);
    } else if ((state == CYPHAL_LISTENING || state == CYPHAL_ONLINE) &&
               cyphal_frame_source(f->id, f->ext, f->rtr, f->len) == n->node_id) {
        /* The controller never receives its own frames, so this is another node's (D9) */
        stop_in_use(n);
        return;
    }
    if ((state != CYPHAL_ONLINE && state != CYPHAL_WAITING) || !f->ext || f->rtr) {
        return;
    }
    const struct CanardFrame frame = {
        .extended_can_id = f->id,
        .payload = { .size = f->len, .data = f->data },
    };
    struct CanardRxTransfer t;
    struct CanardRxSubscription *sub = NULL;
    if (canardRxAccept(&n->canard, (CanardMicrosecond)f->timestamp_us, &frame, 0, &t, &sub) != 1) {
        return;
    }
    s_tr_received++;
    if (sub == &n->getinfo) {
        answer_getinfo(n, &t, stamp);       /* the response's deadline from the request's arrival */
    } else if (sub == &n->allocation) {
        on_allocation(n, &t, stamp);
    }
    heap_free(NULL, t.payload.allocated_size, t.payload.data);     /* the instance's memory resource */
}

/* Bus-off: the node brings the controller back itself (canbus counts how often) */
static void check_bus(node_t *n)
{
    canbus_status_t st;
    canbus_status(&st);
    if (st.state == CANBUS_BUS_OFF) {
        if (st.bus_off != n->bus_off_seen) {
            n->bus_off_seen = st.bus_off;
            ESP_LOGW(TAG, "bus-off (%lu times since the start): recovering", (unsigned long)st.bus_off);
        }
        canbus_recover(s_bus);
    }
}

/* Every frame waiting in the receive queue, through on_frame(). True when it was emptied; false
 * when RX_BUDGET frames were read and more may wait, or the node is stopping. */
static bool drain(node_t *n)
{
    canbus_frame_t f;
    for (int budget = RX_BUDGET; budget > 0 && s_running; budget--) {
        const int64_t t = esp_timer_get_time();
        if (!canbus_receive(s_bus, &f, 0)) {
            cyphal_listen_empty(&n->listen, t);
            return true;
        }
        on_frame(n, &f);
    }
    return false;
}

/* The transmit queue's first frame, once the transfers at its head whose deadline has passed are
 * dropped (canbus can't take a frame back, so the deadline is applied before it gets one) */
static struct CanardTxQueueItem *next_due(node_t *n)
{
    const int64_t now = esp_timer_get_time();
    struct CanardTxQueueItem *item;
    while ((item = canardTxPeek(&n->txq)) != NULL && (int64_t)item->tx_deadline_usec < now) {
        free_transfer(n, item);
        s_tr_dropped++;
    }
    return item;
}

/* Hand libcanard's queue to canbus, highest priority first, one frame at a time: room in canbus
 * first (the frame before on the bus; waiting up to a tick for that), then every frame received
 * meanwhile through on_frame(), and only then the next frame. So once a frame from our node-ID
 * is in the receive queue, nothing more goes but the frame already inside canbus. Expired
 * transfers are dropped whether or not there is room, so that a bus with nobody acknowledging
 * can't fill the queue. True when the queue is empty; false when the rest must wait: no room yet
 * (nobody acknowledging), frames still to read, or the controller bus-off, in which case *retry
 * says to wait a tick before trying again. */
static bool flush(node_t *n, bool *retry)
{
    *retry = false;
    while (next_due(n) != NULL) {
        esp_err_t err = canbus_wait_room(s_bus, 1);
        if (err != ESP_OK) {
            *retry = err != ESP_ERR_TIMEOUT;    /* a timeout has waited its tick already */
            return false;
        }
        if (!drain(n)) {
            return false;
        }
        /* Again: time has passed, and a frame just read may have stopped the node (the queue
         * cleared) or queued an answer ahead of what was first */
        struct CanardTxQueueItem *const item = next_due(n);
        if (item == NULL) {
            break;
        }
        canbus_frame_t f = {
            .id = item->frame.extended_can_id,
            .ext = true,
            .len = (uint8_t)item->frame.payload.size,
        };
        memcpy(f.data, item->frame.payload.data, f.len);
        err = canbus_send(s_bus, &f, 0);
        if (err != ESP_OK) {
            *retry = err != ESP_ERR_TIMEOUT;
            return false;
        }
        s_frames_queued++;
        if (f.len > 0 && (f.data[f.len - 1] & 0x40)) {     /* the tail byte's end of transfer */
            s_tr_sent++;
        }
        canardTxFree(&n->txq, &n->canard, canardTxPop(&n->txq, item));
    }
    return true;
}

static void timers(node_t *n, int64_t now)
{
    if (s_state == CYPHAL_LISTENING && cyphal_listen_over(&n->listen)) {
        go_online(n, now);
    }
    if (s_state == CYPHAL_WAITING && n->held_id != CYPHAL_NODE_ID_AUTO) {
        take_up(n, now);
    } else if (s_state == CYPHAL_WAITING && now >= n->next_request_us) {
        n->next_request_us = now + cyphal_pnp_interval_us(esp_random());     /* rule B */
        /* Not while the last is still queued (requests are all a waiting node sends): nobody has
         * acknowledged the one inside canbus, and another would only expire behind it */
        if (canardTxPeek(&n->txq) == NULL) {
            request(n, now);
        }
    }
    if (s_state == CYPHAL_ONLINE && now >= n->next_heartbeat_us) {
        heartbeat(n, now);
        n->next_heartbeat_us += HEARTBEAT_US;
        if (n->next_heartbeat_us <= now) {     /* late by a period or more: no catching up in a burst */
            n->next_heartbeat_us = now + HEARTBEAT_US;
        }
    }
    if (now >= n->next_bus_check_us) {
        n->next_bus_check_us = now + BUS_CHECK_US;
        check_bus(n);
    }
}

/* Ticks to the next timer, rounded up: a second at most, the bus check's period */
static TickType_t ticks_to_next(const node_t *n, int64_t now)
{
    int64_t next = n->next_bus_check_us;
    const bool held = s_state == CYPHAL_WAITING && n->held_id != CYPHAL_NODE_ID_AUTO;
    if ((s_state == CYPHAL_LISTENING || held) && cyphal_listen_wake_us(&n->listen) < next) {
        next = cyphal_listen_wake_us(&n->listen);
    }
    if (s_state == CYPHAL_WAITING && !held && n->next_request_us < next) {
        next = n->next_request_us;
    }
    if (s_state == CYPHAL_ONLINE && n->next_heartbeat_us < next) {
        next = n->next_heartbeat_us;
    }
    const int64_t tick_us = 1000000 / configTICK_RATE_HZ;
    return next <= now ? 0 : (TickType_t)((next - now + tick_us - 1) / tick_us);
}

static void node_task(void *arg)
{
    node_t *n = arg;
    while (s_running) {
        /* What has come first (D9), then the timers, then the transmit queue */
        const bool drained = drain(n);
        timers(n, esp_timer_get_time());
        bool retry = false;
        const bool sent = drained && (canardTxPeek(&n->txq) == NULL || flush(n, &retry));
        /* Frames still to read, or to send; else the next timer, as of after flush(): a frame it read
         * may have changed the state (an allocation taken up makes a heartbeat due at once) */
        const TickType_t wait = !sent ? (retry ? 1 : 0) : ticks_to_next(n, esp_timer_get_time());
        /* Asleep until the next timer or frame; a stop wakes it here (wake_task()) */
        canbus_frame_t f;
        s_receiving = true;
        const bool got = canbus_receive(s_bus, &f, wait);
        taskENTER_CRITICAL(&s_wake_lock);
        s_receiving = false;
        taskEXIT_CRITICAL(&s_wake_lock);
        if (got && s_running) {     /* after a stop's wake, not even this frame: see wake_task() */
            on_frame(n, &f);
        }
    }
    xSemaphoreGive(s_task_ended);
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------ start and stop */

static esp_err_t start_locked(int tx, int rx)
{
    if (s_task != NULL || s_bus != 0) {     /* running, or the controller kept by a failed stop */
        return ESP_ERR_INVALID_STATE;
    }
    s_tx = tx;
    s_rx = rx;
    s_run = s_saved;
    s_tr_sent = s_tr_received = s_tr_dropped = s_frames_queued = s_heap_failed = 0;
    s_error = ESP_OK;
    s_stop_failed = false;
    esp_err_t err = make_node();
    node_t *n = s_node;
    if (err == ESP_OK) {
        err = serialize_getinfo(n);
    }
    if (err == ESP_OK) {
        const canbus_config_t c = {
            .tx_gpio = tx,
            .rx_gpio = rx,
            .bitrate = s_run.bitrate,
            .mode = CANBUS_NORMAL,     /* acknowledging the bus even while the node sends nothing */
            .owner = "the Cyphal node",
            .tx_slots = 1,              /* one frame at a time: libcanard's queue keeps the order */
        };
        err = canbus_start(&c, &s_bus);
    }
    if (err != ESP_OK) {
        s_state = CYPHAL_FAILED;
        s_error = err;
        ESP_LOGW(TAG, "not started on GPIO%d and GPIO%d: %s", tx, rx,
                 err == ESP_ERR_INVALID_STATE ? "the controller is in use" : esp_err_to_name(err));
        return err;
    }
    const int64_t now = esp_timer_get_time();
    s_started_us = now;
    n->node_id = s_run.node_id;
    n->held_id = CYPHAL_NODE_ID_AUTO;
    cyphal_seen_clear(&n->seen);
    n->next_bus_check_us = now + BUS_CHECK_US;
    /* Logged now, before the task can log what happens next. By the setting, not by s_state, which
     * the task (at a higher priority) may change as soon as it exists */
    if (s_run.node_id == CYPHAL_NODE_ID_AUTO) {
        cyphal_listen_start(&n->listen, now, CYPHAL_PNP_LISTEN_US);
        /* Rule A: subscribed, and the first request after a random Trequest, not at once */
        canardRxSubscribe(&n->canard, CanardTransferKindMessage, uavcan_pnp_NodeIDAllocationData_1_0_FIXED_PORT_ID_,
                          uavcan_pnp_NodeIDAllocationData_1_0_EXTENT_BYTES_, CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
                          &n->allocation);
        n->next_request_us = now + cyphal_pnp_interval_us(esp_random());
        s_state = CYPHAL_WAITING;
        ESP_LOGI(TAG, "plug-and-play, %s, on GPIO%d (TX) and GPIO%d (RX): asking for a node-ID, pseudo-ID %012llx",
                 s_run.name, tx, rx, (unsigned long long)s_pseudo_id);
    } else {
        cyphal_listen_start(&n->listen, now, (int64_t)CYPHAL_LISTEN_MS * 1000);
        s_state = CYPHAL_LISTENING;
        ESP_LOGI(TAG, "node %u (fixed), %s, on GPIO%d (TX) and GPIO%d (RX): listening for %d s first", n->node_id,
                 s_run.name, tx, rx, CYPHAL_LISTEN_MS / 1000);
    }
    s_running = true;
    if (xTaskCreatePinnedToCore(node_task, "cyphal", CONFIG_CYPHAL_TASK_STACK, n, CONFIG_CYPHAL_TASK_PRIORITY,
                                &s_task, CONFIG_CYPHAL_TASK_CORE) != pdPASS) {
        s_running = false;
        s_task = NULL;
        canbus_stop(s_bus);
        s_bus = 0;
        s_state = CYPHAL_FAILED;
        s_error = ESP_ERR_NO_MEM;
        ESP_LOGW(TAG, "not started on GPIO%d and GPIO%d: no memory for its task", tx, rx);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/* The task woken and ended, then the controller stopped. The wake: the task may be asleep in
 * canbus_receive() until its next timer, a second away. xTaskAbortDelay() must reach only that
 * queue wait, never a lock the task waits for elsewhere (a log line's), whose take it would make
 * fail. So it is made only while s_receiving is set, under s_wake_lock, which the task needs to
 * clear it: until then the task is in canbus_receive(), asleep or on its way in or out. On its
 * way in, the abort finds it not yet asleep and does nothing, so it is tried again.
 *
 * An abort can also stay pending in the task. If a frame reaches the queue between the abort and
 * the task running, xQueueReceive() returns that frame without passing through
 * xTaskCheckForTimeOut(), which is what clears the abort (IDF 6.1's FreeRTOS, queue.c and
 * tasks.c). The task's next call that would wait then fails at once instead, a lock taken with
 * portMAX_DELAY included: a log line's, or canbus's. So the task, which takes s_wake_lock after
 * this abort and reads s_running after that, does nothing more once it sees the stop: not even
 * the frame. (The other receives are made with no wait, and no abort reaches them.) */
static void wake_task(void)
{
    taskENTER_CRITICAL(&s_wake_lock);
    if (s_receiving) {
        xTaskAbortDelay(s_task);
    }
    taskEXIT_CRITICAL(&s_wake_lock);
}

static esp_err_t stop_locked(void)
{
    if (s_task != NULL) {
        s_running = false;
        do {
            wake_task();
        } while (xSemaphoreTake(s_task_ended, pdMS_TO_TICKS(10)) != pdTRUE);
        s_task = NULL;
    }
    esp_err_t err = ESP_OK;
    if (s_bus != 0) {
        err = canbus_stop(s_bus);
        if (err == ESP_OK) {
            s_bus = 0;
        }
    }
    if (err == ESP_OK) {
        s_state = CYPHAL_OFF;
    } else {
        /* The task has ended, so the node is stopped whatever the controller does. It still runs,
         * on the handle kept for the next try (`cyphal restart`), and the status says so */
        s_state = CYPHAL_FAILED;
        s_error = err;
        s_stop_failed = true;
    }
    return err;
}

esp_err_t cyphal_start(int tx_gpio, int rx_gpio)
{
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    lock();
    const esp_err_t err = start_locked(tx_gpio, rx_gpio);
    unlock();
    return err;
}

esp_err_t cyphal_restart(void)
{
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_tx >= 0) {
        err = stop_locked();
        if (err == ESP_OK) {
            err = start_locked(s_tx, s_rx);
        }
        /* ESP_ERR_INVALID_STATE means never started; why a stop or start failed is in the status */
        if (err == ESP_ERR_INVALID_STATE) {
            err = ESP_FAIL;
        }
    }
    unlock();
    return err;
}

void cyphal_status(cyphal_status_t *out)
{
    *out = (cyphal_status_t){ .state = CYPHAL_OFF, .tx_gpio = -1, .rx_gpio = -1, .heap_size = CONFIG_CYPHAL_HEAP_SIZE };
    if (s_lock == NULL) {
        return;
    }
    lock();
    out->state = s_state;
    out->error = s_error;
    out->stop_failed = s_stop_failed;
    out->running = s_run;
    out->saved = s_saved;
    out->node_id = s_node != NULL && s_state != CYPHAL_OFF ? s_node->node_id : s_run.node_id;
    /* With plug-and-play, ONLINE and IN_USE come only after an allocation */
    out->allocated = s_run.node_id == CYPHAL_NODE_ID_AUTO && (s_state == CYPHAL_ONLINE || s_state == CYPHAL_IN_USE);
    out->tx_gpio = s_tx;
    out->rx_gpio = s_rx;
    memcpy(out->unique_id, s_uid, sizeof(s_uid));
    out->pseudo_id = s_pseudo_id;
    if (s_task != NULL) {
        out->uptime_s = (uint32_t)((esp_timer_get_time() - s_started_us) / 1000000);
        out->stack_unused = uxTaskGetStackHighWaterMark(s_task);   /* bytes, in ESP-IDF */
    }
    out->transfers_sent = s_tr_sent;
    out->transfers_received = s_tr_received;
    out->transfers_dropped = s_tr_dropped;
    out->frames_queued = s_frames_queued;
    if (s_heap != NULL) {
        out->heap_used = s_heap_free0 - multi_heap_free_size(s_heap);
        out->heap_peak = s_heap_free0 - multi_heap_minimum_free_size(s_heap);
    }
    out->heap_failed = s_heap_failed;
    unlock();
}
