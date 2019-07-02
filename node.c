#include "lv2h.h"

#define LV2H_DEFAULT_INTERVAL_MS 1000

static int lv2h_event_cmp(lv2h_event_t *a, lv2h_event_t *b);
static int lv2h_process_tick(lv2h_t *host);
static int lv2h_is_node_at_count_limit(lv2h_node_t *node);
static int lv2h_process_node(lv2h_event_t *ev);


int lv2h_run(lv2h_t *host) {
    struct timespec ts;
    long sleep_ns;

    // TODO figure out what to keep in main vs here
    // TODO ?start audio thread
    // TODO ?start tui/keyboard/midi polling thread
    // TODO ?init script engine

    while (!host->done) {
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
        host->ts_now_ns = ts.tv_sec * 1000000000L + ts.tv_nsec;
        host->ts_next_ns = host->ts_now_ns + host->tick_ns;
        lv2h_process_tick(host);
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
        sleep_ns = host->tick_ns - ((ts.tv_sec * 1000000000L + ts.tv_nsec) - host->ts_now_ns);
        if (sleep_ns < 0) sleep_ns = 0;
        ts.tv_sec = sleep_ns / 1000000000L;
        ts.tv_nsec = sleep_ns % 1000000000L;
        // printf("sleep_ns=%lu\n", sleep_ns);
        nanosleep(&ts, NULL);
    }
    return LV2H_OK;
}

int lv2h_node_new(lv2h_t *host, lv2h_node_callback_fn callback, void *udata, lv2h_node_t **out_node) {
    lv2h_node_t *node;
    node = calloc(1, sizeof(lv2h_node_t));
    node->host = host;
    node->callback = callback;
    node->callback_udata = udata;
    node->interval_ns = LV2H_DEFAULT_INTERVAL_MS * 1000000L;
    node->interval_ns_double = (double)node->interval_ns;
    node->interval_factor = 1.0;
    node->divisor = 1;
    node->multiplier = 1;
    LL_APPEND2(host->parent_node_list, node, next_parent);
    lv2h_schedule_event(host, 0, 0, lv2h_process_node, node);
    *out_node = node;
    return LV2H_OK;
}

int lv2h_node_free(lv2h_node_t *node) {
    free(node);
    return LV2H_OK;
}

int lv2h_node_set_offset(lv2h_node_t *node, long offset_ms) {
    node->offset_ns = offset_ms * 1000000L;
    return LV2H_OK;
}

int lv2h_node_set_interval(lv2h_node_t *node, long interval_ms) {
    node->interval_ns = interval_ms * 1000000L;
    node->interval_ns_double = (double)node->interval_ns;
    return LV2H_OK;
}

int lv2h_node_set_interval_factor(lv2h_node_t *node, double factor) {
    node->interval_factor = factor;
    return LV2H_OK;
}

int lv2h_node_set_divisor(lv2h_node_t *node, long divisor) {
    node->divisor = divisor > 0L ? divisor : 1L;
    return LV2H_OK;
}

int lv2h_node_set_multiplier(lv2h_node_t *node, long multiplier) {
    node->multiplier = multiplier > 0L ? multiplier : 1L;
    return LV2H_OK;
}

int lv2h_node_set_count(lv2h_node_t *node, int count) {
    node->count = count;
    return LV2H_OK;
}

int lv2h_node_set_count_limit(lv2h_node_t *node, int count_limit) {
    node->count_limit = count_limit;
    return LV2H_OK;
}

int lv2h_node_follow(lv2h_node_t *node, lv2h_node_t *parent) {
    LL_DELETE2(node->host->parent_node_list, node, next_parent);
    node->parent = parent;
    LL_APPEND2(parent->child_list, node, next_child);
    return LV2H_OK;
}

int lv2h_node_unfollow(lv2h_node_t *node) {
    LL_DELETE2(node->parent->child_list, node, next_child);
    node->parent = NULL;
    LL_APPEND2(node->host->parent_node_list, node, next_parent);
    return LV2H_OK;
}

int lv2h_schedule_event(lv2h_t *host, long timestamp_ns, int audio_run_delay, lv2h_event_callback_fn callback, void *udata) {
    lv2h_event_t *ev;
    // TODO use preallocated event pool
    ev = calloc(1, sizeof(lv2h_event_t));
    ev->callback = callback;
    ev->udata = udata;
    ev->min_audio_iter = __sync_fetch_and_add(&host->audio_iter, 0) + audio_run_delay;
    ev->timestamp_ns = timestamp_ns;
    printf("lv2h_schedule_event ev->timestamp_ns=%ld\n", ev->timestamp_ns);
    LL_INSERT_INORDER(host->event_list, ev, lv2h_event_cmp);
    return LV2H_OK;
}

static int lv2h_event_cmp(lv2h_event_t *a, lv2h_event_t *b) {
    if (a->timestamp_ns < b->timestamp_ns) {
        return -1;
    } else if (a->timestamp_ns > b->timestamp_ns) {
        return 1;
    }
    return 0;
}

static int lv2h_process_tick(lv2h_t *host) {
    lv2h_event_t *ev, *ev_tmp;
    LL_FOREACH_SAFE(host->event_list, ev, ev_tmp) {
        if (host->ts_now_ns >= ev->timestamp_ns) {
            if (host->audio_iter >= ev->min_audio_iter) {
                LL_DELETE(host->event_list, ev);
                (ev->callback)(ev);
            }
        } else {
            // We can break because event_list is sorted (LL_INSERT_INORDER)
            break;
        }
    }
    return LV2H_OK;
}

static int lv2h_is_node_at_count_limit(lv2h_node_t *node) {
    if (node->parent) {
        return lv2h_is_node_at_count_limit(node->parent);
    }
    return node->count_limit > 0 && node->count >= node->count_limit ? 1 : 0;
}

static int lv2h_process_node(lv2h_event_t *ev) {
    lv2h_t *host;
    lv2h_node_t *node;
    long now_ns, next_ns, parent_interval_ns;

    node = (lv2h_node_t*)ev->udata;
    host = node->host;
    now_ns = host->ts_now_ns; // TODO or ev->timestamp_ns?

    // Invoke node callback
    (node->callback)(node, node->callback_udata, node->count);

    // Increment count and bail if limit has been reached
    node->count += 1;
    if (lv2h_is_node_at_count_limit(node)) {
        goto lv2h_process_node_done;
    }

    if (node->parent) {
        // Schedule based on parent's schedule and own divisor/multiplier. Note
        // we cannot use `parent->interval_ms` because our parent may be
        // following another node itself.
        parent_interval_ns = node->parent->ts_next_ns - node->parent->ts_last_ns;
        if (node->divisor > 1L) {
            next_ns = now_ns + (parent_interval_ns / node->divisor);
        } else if (node->multiplier > 1L) {
            next_ns = now_ns + (parent_interval_ns * node->multiplier);
        } else {
            next_ns = node->parent->ts_next_ns;
        }
        next_ns += node->offset_ns;
    } else {
        // Schedule at next interval
        if (node->interval_factor != 1.0) {
            node->interval_ns_double *= node->interval_factor;
            node->interval_ns = (long)node->interval_ns_double;
        }
        next_ns = now_ns + node->interval_ns;
    }

    // Schedule next event for this node
    node->ts_last_ns = now_ns;
    node->ts_next_ns = next_ns;
    lv2h_schedule_event(host, next_ns, 0, lv2h_process_node, node);

lv2h_process_node_done:
    free(ev); // TODO `ev->in_queue = 0` (use preallocated event pool)
    return LV2H_OK;
}
