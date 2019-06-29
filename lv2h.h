#ifndef __LV2H_H
#define __LV2H_H

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <soundio/soundio.h>
#include <lilv-0/lilv/lilv.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/buf-size/buf-size.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <uthash.h>
#include <utlist.h>
#include "lv2_evbuf.h"

#define LV2H_OK  0
#define LV2H_ERR 1
#define LV2H_API
#define LV2H_RETURN_ERR(lv2h, fmt, ...) do {                                \
    snprintf((lv2h)->errstr, sizeof((lv2h)->errstr), (fmt), __VA_ARGS__);   \
    return LV2H_ERR;                                                        \
} while (0)
#define LV2H_RETURN_ERR_ARG(lv2h, retarg, fmt, ...) do {                    \
    snprintf((lv2h)->errstr, sizeof((lv2h)->errstr), (fmt), __VA_ARGS__);   \
    return (retarg);                                                        \
} while (0)
#define LV2H_RETURN_ERR_VOID(lv2h, fmt, ...) do {                           \
    snprintf((lv2h)->errstr, sizeof((lv2h)->errstr), (fmt), __VA_ARGS__);   \
    return;                                                                 \
} while (0)

#define try(__rv, __call) do { if (((__rv) = (__call)) != 0) return (__rv); } while(0)

typedef struct _lv2h_t lv2h_t;
typedef struct _lv2h_plug_t lv2h_plug_t;
typedef struct _lv2h_inst_t lv2h_inst_t;
typedef struct _lv2h_port_t lv2h_port_t;
typedef struct _lv2h_conn_t lv2h_conn_t;
typedef struct _lv2h_node_t lv2h_node_t;
typedef struct _lv2h_event_t lv2h_event_t;
typedef int (*lv2h_node_callback_fn)(lv2h_node_t *node, void *udata, int count);

struct _lv2h_t {
    lv2h_plug_t *plugin_map;
    lv2h_node_t *parent_node_list;
    lv2h_plug_t *audio_plugin;
    lv2h_inst_t *audio_inst;
    lv2h_conn_t *conn_list;
    lv2h_event_t *event_list;
    int sample_rate;
    long tick_ns;
    int block_size;
    long ts_now_ns;
    long ts_next_ns;
    float *audio_out[2];
    LilvWorld *lilv_world;
    const LilvPlugins *lilv_plugins;
    LilvNode *lv2_core_InputPort;
    LilvNode *lv2_core_OutputPort;
    LilvNode *lv2_core_AudioPort;
    LilvNode *lv2_core_ControlPort;
    LilvNode *lv2_core_CVPort;
    LilvNode *lv2_atom_AtomPort;
    LilvNode *lv2_atom_Sequence;
    LilvNode *lv2_urid_map;
    LV2_Feature feature_map;
    LV2_Feature feature_unmap;
    const LV2_Feature *features[3];
    LV2_URID_Map urid_map;
    LV2_URID_Unmap urid_unmap;
    char **lv2_uris;
    size_t lv2_uris_size;
    int done;
    char errstr[1024];
};

struct _lv2h_plug_t {
    lv2h_t *host;
    char *uri_str;
    LilvNode *lilv_uri;
    const LilvPlugin *lilv_plugin;
    uint32_t port_count;
    float *port_mins;
    float *port_maxs;
    float *port_defaults;
    lv2h_inst_t *inst_list;
    UT_hash_handle hh;
};

struct _lv2h_inst_t {
    lv2h_plug_t *plug;
    LilvInstance *lilv_inst;
    lv2h_port_t *port_array;
    // lv2h_port_t *port_map;
    lv2h_inst_t *next;
};

struct _lv2h_port_t {
    lv2h_inst_t *inst;
    const LilvPort *lilv_port;
    uint32_t port_index;
    char *port_name;
    float control_val;
    float *audio_null;
    LV2_Atom_Sequence *atom_output;
    LV2_Evbuf *atom_input; // TODO replace type
    lv2h_conn_t **conn_ptr_array;
    size_t conn_count;
    size_t conn_size;
    // UT_hash_handle hh;
};

struct _lv2h_conn_t {
    lv2h_port_t *writer;
    lv2h_port_t **reader_ptr_array;
    size_t reader_count;
    size_t reader_size;
    float *data;
    size_t data_size;
    lv2h_conn_t *next;
};

struct _lv2h_node_t {
    lv2h_t *lv2h;
    struct _lv2h_node_t *parent;
    struct _lv2h_node_t *child_list;
    long delay_ms;
    long interval_ms;
    int count_limit;
    int count;
    int divisor;
    int multiplier;
    lv2h_node_t *next_parent;
    lv2h_node_t *next_child;
};

struct _lv2h_event_t {
    lv2h_node_t *node;
    long timestamp_ns;
    lv2h_event_t *next;
};

LV2H_API int lv2h_new(uint32_t sample_rate, size_t block_size, long tick_ms, lv2h_t **out_lv2h);
LV2H_API int lv2h_free(lv2h_t *lv2h);
LV2H_API int lv2h_run(lv2h_t *lv2h);

LV2H_API int lv2h_plug_new(lv2h_t *lv2h, char *uri, lv2h_plug_t **out_plug);
LV2H_API int lv2h_plug_free(lv2h_plug_t *plug);

LV2H_API int lv2h_inst_new(lv2h_plug_t *plugin, lv2h_inst_t **out_inst);
LV2H_API int lv2h_inst_free(lv2h_inst_t *inst);
LV2H_API int lv2h_inst_load_preset(lv2h_inst_t *inst, char *preset);
LV2H_API int lv2h_inst_connect(lv2h_inst_t *writer_inst, char *writer_port_name, lv2h_inst_t *reader_inst, char *reader_port_name, lv2h_conn_t **out_conn);
LV2H_API int lv2h_inst_disconnect(lv2h_inst_t *writer_inst, char *writer_port_name, lv2h_inst_t *reader_inst, char *reader_port_name);
LV2H_API int lv2h_inst_play(lv2h_inst_t *inst, int *notes, int notes_len, int vel, long len_ms);
LV2H_API int lv2h_inst_send_midi(lv2h_inst_t *inst, int *bytes, int bytes_len);
LV2H_API int lv2h_inst_set_param(lv2h_inst_t *inst, char *port_name, float val);

LV2H_API int lv2h_conn_free(lv2h_conn_t *conn);

LV2H_API int lv2h_node_new(lv2h_t *lv2h, lv2h_node_callback_fn *callback, void *udata, lv2h_node_t **out_node);
LV2H_API int lv2h_node_free(lv2h_node_t *node);
LV2H_API int lv2h_node_set_delay(lv2h_node_t *node, long delay_ms);
LV2H_API int lv2h_node_set_interval(lv2h_node_t *node, long interval_ms);
LV2H_API int lv2h_node_set_accel(lv2h_node_t *node, float delta);
LV2H_API int lv2h_node_set_divisor(lv2h_node_t *node, int divisor);
LV2H_API int lv2h_node_set_multiplier(lv2h_node_t *node, int multiplier);
LV2H_API int lv2h_node_set_count(lv2h_node_t *node, int count);
LV2H_API int lv2h_node_set_count_limit(lv2h_node_t *node, int count_limit);
LV2H_API int lv2h_node_run(lv2h_node_t *node);
LV2H_API int lv2h_node_follow(lv2h_node_t *node, lv2h_node_t *parent);
LV2H_API int lv2h_node_unfollow(lv2h_node_t *node);

int lv2h_run_plugin_insts(lv2h_t *lv2h, int frame_count);
void *lv2h_run_audio(void *arg);

#endif
