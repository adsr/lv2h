#ifndef __LV2H_H
#define __LV2H_H

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <limits.h>
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
#define LV2H_RETURN_ERR(host, fmt, ...) do {                                \
    snprintf((host)->errstr, sizeof((host)->errstr), (fmt), __VA_ARGS__);   \
    return LV2H_ERR;                                                        \
} while (0)
#define LV2H_RETURN_ERR_ARG(host, retarg, fmt, ...) do {                    \
    snprintf((host)->errstr, sizeof((host)->errstr), (fmt), __VA_ARGS__);   \
    return (retarg);                                                        \
} while (0)
#define LV2H_RETURN_ERR_VOID(host, fmt, ...) do {                           \
    snprintf((host)->errstr, sizeof((host)->errstr), (fmt), __VA_ARGS__);   \
    return;                                                                 \
} while (0)

typedef struct _lv2h_t lv2h_t;
typedef struct _lv2h_plug_t lv2h_plug_t;
typedef struct _lv2h_inst_t lv2h_inst_t;
typedef struct _lv2h_port_t lv2h_port_t;
typedef struct _lv2h_node_t lv2h_node_t;
typedef struct _lv2h_event_t lv2h_event_t;
typedef int (*lv2h_node_callback_fn)(lv2h_node_t *node, void *udata, int count);
typedef int (*lv2h_event_callback_fn)(lv2h_event_t *event);

// TODO remove unused struct fields

struct _lv2h_t {
    lv2h_plug_t *plugin_map;
    lv2h_node_t *parent_node_list;
    lv2h_plug_t *audio_plug;
    lv2h_inst_t *audio_inst;
    lv2h_event_t *event_list;
    int sample_rate;
    long tick_ns;
    int block_size;
    long ts_now_ns;
    long ts_next_ns;
    float *audio_block_array;
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
    uintmax_t audio_iter;
    pthread_mutex_t mutex;
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
    uintmax_t audio_iter;
    LilvInstance *lilv_inst;
    lv2h_port_t *port_array;
    lv2h_port_t *port_map;
    lv2h_inst_t *next;
};

struct _lv2h_port_t {
    lv2h_inst_t *inst;
    const LilvPort *lilv_port;
    uint32_t port_index;
    char *port_name;
    float control_val;
    int is_control;
    float *writer_block;
    float *reader_block_mixed;
    LV2_Atom_Sequence *atom_output;
    LV2_Evbuf *atom_input; // TODO replace type
    LV2_Evbuf_Iterator atom_input_iter;
    lv2h_port_t *writer_port_list;
    lv2h_port_t *next;
    UT_hash_handle hh;
};

struct _lv2h_node_t {
    lv2h_t *host;
    lv2h_node_callback_fn callback;
    void *callback_udata;
    lv2h_node_t *parent;
    lv2h_node_t *child_list;
    long offset_ns;
    long interval_ns;
    double interval_ns_double;
    double interval_factor;
    int count_limit;
    int count;
    long divisor;
    long multiplier;
    long ts_last_ns;
    long ts_next_ns;
    lv2h_node_t *next_parent;
    lv2h_node_t *next_child;
};

struct _lv2h_event_t {
    lv2h_event_callback_fn callback;
    uintmax_t min_audio_iter;
    void *udata;
    long timestamp_ns;
    lv2h_event_t *next;
};

LV2H_API int lv2h_new(uint32_t sample_rate, size_t block_size, long tick_ms, lv2h_t **out_lv2h);
LV2H_API int lv2h_free(lv2h_t *host);
LV2H_API int lv2h_run(lv2h_t *host);

LV2H_API int lv2h_plug_new(lv2h_t *host, char *uri_str, lv2h_plug_t **out_plug);
LV2H_API int lv2h_plug_free(lv2h_plug_t *plugin);

LV2H_API int lv2h_inst_new(lv2h_plug_t *plug, lv2h_inst_t **out_inst);
LV2H_API int lv2h_inst_free(lv2h_inst_t *inst);
LV2H_API int lv2h_inst_connect(lv2h_inst_t *writer_inst, char *writer_port_name, lv2h_inst_t *reader_inst, char *reader_port_name);
LV2H_API int lv2h_inst_disconnect(lv2h_inst_t *writer_inst, char *writer_port_name, lv2h_inst_t *reader_inst, char *reader_port_name);
LV2H_API int lv2h_inst_connect_to_audio(lv2h_inst_t *writer_inst, char *writer_port_name, int audio_channel);
LV2H_API int lv2h_inst_disconnect_from_audio(lv2h_inst_t *writer_inst, char *writer_port_name, int audio_channel);
LV2H_API int lv2h_inst_send_midi(lv2h_inst_t *inst, char *port_name, uint8_t *bytes, int bytes_len);
LV2H_API int lv2h_inst_set_param(lv2h_inst_t *inst, char *port_name, float val);
LV2H_API int lv2h_inst_play(lv2h_inst_t *inst, char *port_name, int chan, int note1, int note2, int note3, int note4, int vel, int len_ms);
LV2H_API int lv2h_inst_load_preset(lv2h_inst_t *inst, char *preset_str);

LV2H_API int lv2h_node_new(lv2h_t *host, lv2h_node_callback_fn callback, void *udata, lv2h_node_t **out_node);
LV2H_API int lv2h_node_free(lv2h_node_t *node);
LV2H_API int lv2h_node_set_offset(lv2h_node_t *node, long offset_ms);
LV2H_API int lv2h_node_set_interval(lv2h_node_t *node, long interval_ms);
LV2H_API int lv2h_node_set_interval_factor(lv2h_node_t *node, double factor);
LV2H_API int lv2h_node_set_divisor(lv2h_node_t *node, long divisor);
LV2H_API int lv2h_node_set_multiplier(lv2h_node_t *node, long multiplier);
LV2H_API int lv2h_node_set_count(lv2h_node_t *node, int count);
LV2H_API int lv2h_node_set_count_limit(lv2h_node_t *node, int count_limit);
LV2H_API int lv2h_node_follow(lv2h_node_t *node, lv2h_node_t *parent);
LV2H_API int lv2h_node_unfollow(lv2h_node_t *node);

int lv2h_schedule_event(lv2h_t *host, long timestamp_ns, int audio_run_delay, lv2h_event_callback_fn callback, void *udata);
int lv2h_run_plugin_insts(lv2h_t *host, int frame_count);
void *lv2h_run_audio(void *arg);

#endif
