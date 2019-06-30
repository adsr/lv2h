#include "lv2h.h"

static int lv2h_inst_xnnect(lv2h_inst_t *writer_inst, char *writer_port_name, lv2h_inst_t *reader_inst, char *reader_port_name, int disconnect);
static int lv2h_inst_xnnect_to_audio(lv2h_inst_t *writer_inst, char *writer_port_name, int audio_channel, int disconnect);
static int lv2h_inst_get_port(lv2h_inst_t *inst, char *port_name, int is_audio, int is_midi, int is_input, lv2h_port_t **out_port);
static int lv2h_inst_get_audio_output_port(lv2h_inst_t *inst, char *port_name, lv2h_port_t **out_port);
static int lv2h_inst_get_audio_input_port(lv2h_inst_t *inst, char *port_name, lv2h_port_t **out_port);
static int lv2h_inst_get_midi_input_port(lv2h_inst_t *inst, char *port_name, lv2h_port_t **out_port);
static LV2_URID lv2h_map_uri(LV2_URID_Map_Handle handle, const char *uri);
static const char *lv2h_unmap_uri(LV2_URID_Map_Handle handle, LV2_URID urid);
static void lv2h_inst_set_port_value(const char *port_name, void *user_data, const void *value, uint32_t size, uint32_t type);
static uint32_t lv2h_inst_port_index(lv2h_inst_t *inst, const char *port_name);
static int lv2h_process_tick(lv2h_t *host);
static int lv2h_process_node(lv2h_node_t *node, lv2h_event_t *ev);
static int lv2h_port_init(lv2h_port_t *port, uint32_t port_index, lv2h_inst_t *inst);
static int lv2h_port_deinit(lv2h_port_t *port);
static int lv2h_run_plugin_inst(lv2h_inst_t *inst, int frame_count, size_t iter, int depth);

int lv2h_new(uint32_t sample_rate, size_t block_size, long tick_ms, lv2h_t **out_lv2h) {
    lv2h_t *host;

    host = calloc(1, sizeof(lv2h_t));

    host->sample_rate = sample_rate;
    host->tick_ns = tick_ms * 1000000L;
    host->block_size = block_size;

    host->lilv_world = lilv_world_new();
    lilv_world_load_all(host->lilv_world);

    host->lilv_plugins = lilv_world_get_all_plugins(host->lilv_world);

    host->lv2_core_InputPort   = lilv_new_uri(host->lilv_world, LV2_CORE__InputPort);
    host->lv2_core_OutputPort  = lilv_new_uri(host->lilv_world, LV2_CORE__OutputPort);
    host->lv2_core_AudioPort   = lilv_new_uri(host->lilv_world, LV2_CORE__AudioPort);
    host->lv2_core_ControlPort = lilv_new_uri(host->lilv_world, LV2_CORE__ControlPort);
    host->lv2_core_CVPort      = lilv_new_uri(host->lilv_world, LV2_CORE__CVPort);
    host->lv2_atom_AtomPort    = lilv_new_uri(host->lilv_world, LV2_ATOM__AtomPort);
    host->lv2_atom_Sequence    = lilv_new_uri(host->lilv_world, LV2_ATOM__Sequence);
    host->lv2_urid_map         = lilv_new_uri(host->lilv_world, LV2_URID__map);

    host->urid_map.handle    = host;
    host->urid_map.map       = lv2h_map_uri;
    host->feature_map.URI    = LV2_URID_MAP_URI;
    host->feature_map.data   = &host->urid_map;

    host->urid_unmap.handle  = host;
    host->urid_unmap.unmap   = lv2h_unmap_uri;
    host->feature_unmap.URI  = LV2_URID_UNMAP_URI;
    host->feature_unmap.data = &host->urid_unmap;

    host->features[0] = &host->feature_map;
    host->features[1] = &host->feature_unmap;
    host->features[2] = NULL;

    host->audio_plug = calloc(1, sizeof(lv2h_plug_t));
    host->audio_plug->host = host;
    host->audio_plug->port_count = 2;

    host->audio_inst = calloc(1, sizeof(lv2h_inst_t));
    host->audio_inst->plug = host->audio_plug;
    host->audio_inst->port_array = calloc(2, sizeof(lv2h_port_t));
    host->audio_inst->port_array[0].reader_block_mixed = calloc(block_size, sizeof(float));
    host->audio_inst->port_array[1].reader_block_mixed = calloc(block_size, sizeof(float));

    *out_lv2h = host;
    return LV2H_OK;
}

int lv2h_free(lv2h_t *host) {
    // TODO ensure clean shutdown with valgrind

    lv2h_plug_t *plug, *plug_tmp;

    HASH_ITER(hh, host->plugin_map, plug, plug_tmp) {
        lv2h_plug_free(plug);
        HASH_DEL(host->plugin_map, plug);
    }

    free(host->audio_inst->port_array[0].reader_block_mixed);
    free(host->audio_inst->port_array[1].reader_block_mixed);
    free(host->audio_inst->port_array);
    free(host->audio_inst);
    free(host->audio_plug);

    lilv_node_free(host->lv2_core_InputPort);
    lilv_node_free(host->lv2_core_OutputPort);
    lilv_node_free(host->lv2_core_AudioPort);
    lilv_node_free(host->lv2_core_ControlPort);
    lilv_node_free(host->lv2_core_CVPort);
    lilv_node_free(host->lv2_atom_AtomPort);
    lilv_node_free(host->lv2_atom_Sequence);
    lilv_node_free(host->lv2_urid_map);

    lilv_world_free(host->lilv_world);

    if (host->lv2_uris) free(host->lv2_uris);

    free(host);

    return LV2H_OK;
}

int lv2h_run(lv2h_t *host) {
    struct timespec ts;
    long sleep_ns;
    // TODO astart audio thread
    // TODO start tui/keyboard/midi polling thread

    // TODO init script engine
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
        nanosleep(&ts, NULL);
    }
    return LV2H_OK;
}

int lv2h_plug_new(lv2h_t *host, char *uri_str, lv2h_plug_t **out_plug) {
    lv2h_plug_t *plug;

    plug = calloc(1, sizeof(lv2h_plug_t));
    plug->host = host;
    plug->lilv_uri = lilv_new_uri(host->lilv_world, uri_str);
    plug->uri_str = strdup(uri_str);
    if (!(plug->lilv_plugin = lilv_plugins_get_by_uri(host->lilv_plugins, plug->lilv_uri))) {
        lilv_node_free(plug->lilv_uri);
        free(plug);
        LV2H_RETURN_ERR(host, "lv2h_plug_new: plugin not found for uri %s\n", uri_str);
    }
    plug->port_count = lilv_plugin_get_num_ports(plug->lilv_plugin);
    plug->port_mins = calloc(plug->port_count, sizeof(float));
    plug->port_maxs = calloc(plug->port_count, sizeof(float));
    plug->port_defaults = calloc(plug->port_count, sizeof(float));

    lilv_plugin_get_port_ranges_float(plug->lilv_plugin, plug->port_mins, plug->port_maxs, plug->port_defaults);

    *out_plug = plug;

    return LV2H_OK;
}

int lv2h_plug_free(lv2h_plug_t *plugin) {
    lv2h_inst_t *inst, *inst_tmp;
    LL_FOREACH_SAFE(plugin->inst_list, inst, inst_tmp) {
        lv2h_inst_free(inst);
        LL_DELETE(plugin->inst_list, inst);
    }
    lilv_node_free(plugin->lilv_uri);
    free(plugin->uri_str);
    free(plugin);
    return LV2H_OK;
}


int lv2h_inst_new(lv2h_plug_t *plug, lv2h_inst_t **out_inst) {
    lv2h_inst_t *inst;
    lv2h_port_t *port;
    uint32_t i;

    inst = calloc(1, sizeof(lv2h_inst_t));
    inst->plug = plug;
    inst->lilv_inst = lilv_plugin_instantiate(plug->lilv_plugin, (float)plug->host->sample_rate, plug->host->features);
    inst->port_array = calloc(plug->port_count, sizeof(lv2h_port_t));

    for (i = 0; i < plug->port_count; ++i) {
        port = inst->port_array + i;
        lv2h_port_init(port, i, inst);
        HASH_ADD_STR(inst->port_map, port_name, port);
    }

    lilv_instance_activate(inst->lilv_inst);

    LL_APPEND(plug->inst_list, inst);

    *out_inst = inst;

    return LV2H_OK;
}


int lv2h_inst_free(lv2h_inst_t *inst) {
    uint32_t i;

    LL_DELETE(inst->plug->inst_list, inst);

    lilv_instance_deactivate(inst->lilv_inst);

    for (i = 0; i < inst->plug->port_count; ++i) {
        lv2h_port_deinit(&inst->port_array[i]);
        HASH_DEL(inst->port_map, inst->port_array + i);
    }

    free(inst->port_array);
    lilv_instance_free(inst->lilv_inst);

    free(inst);

    return LV2H_OK;
}

int lv2h_inst_connect(lv2h_inst_t *writer_inst, char *writer_port_name, lv2h_inst_t *reader_inst, char *reader_port_name) {
    return lv2h_inst_xnnect(writer_inst, writer_port_name, reader_inst, reader_port_name, 0);
}

int lv2h_inst_disconnect(lv2h_inst_t *writer_inst, char *writer_port_name, lv2h_inst_t *reader_inst, char *reader_port_name) {
    return lv2h_inst_xnnect(writer_inst, writer_port_name, reader_inst, reader_port_name, 1);
}

int lv2h_inst_connect_to_audio(lv2h_inst_t *writer_inst, char *writer_port_name, int audio_channel) {
    return lv2h_inst_xnnect_to_audio(writer_inst, writer_port_name, audio_channel, 0);
}

int lv2h_inst_disconnect_from_audio(lv2h_inst_t *writer_inst, char *writer_port_name, int audio_channel) {
    return lv2h_inst_xnnect_to_audio(writer_inst, writer_port_name, audio_channel, 1);
}

int lv2h_inst_send_midi(lv2h_inst_t *inst, char *port_name, uint8_t *bytes, int bytes_len) {
    lv2h_port_t *port;
    LV2_Evbuf_Iterator end;

    if (lv2h_inst_get_midi_input_port(inst, port_name, &port) != LV2H_OK) {
        return LV2H_ERR;
    }

    end = lv2_evbuf_end(port->atom_input);
    lv2_evbuf_write(&end, 0, 0, lv2h_map_uri(inst->plug->host, LV2_MIDI__MidiEvent), bytes_len, bytes);
    return LV2H_OK;
}

int lv2h_inst_set_param(lv2h_inst_t *inst, char *port_name, float val) {
    // TODO
    return 0;
}

int lv2h_inst_play(lv2h_inst_t *inst, char *port_name, uint8_t *notes, int notes_len, int vel, long len_ms) {
    // TODO
    return 0;
}

int lv2h_inst_load_preset(lv2h_inst_t *inst, char *preset_str) {
    LilvNode *preset;
    LilvState *state;
    lv2h_t *host;
    host = inst->plug->host;
    preset = lilv_new_uri(host->lilv_world, preset_str);
    state = lilv_state_new_from_world(host->lilv_world, &host->urid_map, preset);
    lilv_state_restore(state, inst->lilv_inst, lv2h_inst_set_port_value, inst, 0, NULL);
    return LV2H_OK;
}

int lv2h_run_plugin_insts(lv2h_t *host, int frame_count) {
    host->current_iter += 1;
    lv2h_run_plugin_inst(host->audio_inst, frame_count, host->current_iter, 0);
    return LV2H_OK;
}

static int lv2h_inst_xnnect(lv2h_inst_t *writer_inst, char *writer_port_name, lv2h_inst_t *reader_inst, char *reader_port_name, int disconnect) {
    lv2h_t *host;
    lv2h_port_t *writer_port, *reader_port;
    const LilvPort *lilv_writer_port, *lilv_reader_port;
    const LilvPlugin *lilv_writer_plugin, *lilv_reader_plugin;
    size_t i;

    host = writer_inst->plug->host;


    if (lv2h_inst_get_audio_output_port(writer_inst, writer_port_name, &writer_port) != LV2H_OK) {
        return LV2H_ERR;
    }
    if (lv2h_inst_get_audio_input_port(reader_inst, reader_port_name, &reader_port) != LV2H_OK) {
        return LV2H_ERR;
    }

    if (disconnect) {
        LL_DELETE(reader_port->writer_port_list, writer_port);
    } else {
        LL_APPEND(reader_port->writer_port_list, writer_port);
    }

    return LV2H_OK;
}

static int lv2h_inst_xnnect_to_audio(lv2h_inst_t *writer_inst, char *writer_port_name, int audio_channel, int disconnect) {
    lv2h_t *host;
    lv2h_port_t *writer_port;

    host = writer_inst->plug->host;
    if (audio_channel != 0 && audio_channel != 1) {
        LV2H_RETURN_ERR(host, "lv2h_inst_xnnect_to_audio: invalid audio_channel %d (must be 0 or 1)\n", audio_channel);
    }

    if (lv2h_inst_get_audio_output_port(writer_inst, writer_port_name, &writer_port) != LV2H_OK) {
        return LV2H_ERR;
    }

    if (disconnect) {
        LL_DELETE(host->audio_inst->port_array[audio_channel].writer_port_list, writer_port);
    } else {
        LL_APPEND(host->audio_inst->port_array[audio_channel].writer_port_list, writer_port);
    }

    return LV2H_OK;
}

static int lv2h_inst_get_port(lv2h_inst_t *inst, char *port_name, int is_audio, int is_midi, int is_input, lv2h_port_t **out_port) {
    lv2h_t *host;
    lv2h_port_t *port;

    host = inst->plug->host;
    HASH_FIND_STR(inst->port_map, port_name, port);
    if (!port) {
        LV2H_RETURN_ERR(host, "lv2h_inst_get_port: port %s not found\n", port_name);
    }

    if (is_audio) {
        if (is_input) {
            if (!port->reader_block_mixed) {
                LV2H_RETURN_ERR(host, "lv2h_inst_get_port: port %s is not an audio/CV input\n", port_name);
            }
        } else {
            if (!port->writer_block) {
                LV2H_RETURN_ERR(host, "lv2h_inst_get_port: port %s is not an audio/CV output\n", port_name);
            }
        }
    }

    if (is_midi) {
        if (is_input) {
            if (!port->atom_input) {
                LV2H_RETURN_ERR(host, "lv2h_inst_get_port: port %s is not a MIDI input\n", port_name);
            }
        } else {
            if (!port->atom_output) {
                LV2H_RETURN_ERR(host, "lv2h_inst_get_port: port %s is not a MIDI output\n", port_name);
            }
        }
    }

    *out_port = port;
    return LV2H_OK;
}

static int lv2h_inst_get_audio_output_port(lv2h_inst_t *inst, char *port_name, lv2h_port_t **out_port) {
    return lv2h_inst_get_port(inst, port_name, 1, 0, 0, out_port);
}

static int lv2h_inst_get_audio_input_port(lv2h_inst_t *inst, char *port_name, lv2h_port_t **out_port) {
    return lv2h_inst_get_port(inst, port_name, 1, 0, 1, out_port);
}

static int lv2h_inst_get_midi_input_port(lv2h_inst_t *inst, char *port_name, lv2h_port_t **out_port) {
    return lv2h_inst_get_port(inst, port_name, 0, 1, 1, out_port);
}


static LV2_URID lv2h_map_uri(LV2_URID_Map_Handle handle, const char *uri) {
    lv2h_t *host;
    size_t i;
    host = (lv2h_t*)handle;
    for (i = 0; i < host->lv2_uris_size; ++i) {
        if (!strcmp(host->lv2_uris[i], uri)) {
            return i + 1;
        }
    }
    host->lv2_uris = (char **)realloc(host->lv2_uris, (++host->lv2_uris_size) * sizeof(char *));
    host->lv2_uris[host->lv2_uris_size - 1] = strdup(uri);
    return host->lv2_uris_size;
}

static const char *lv2h_unmap_uri(LV2_URID_Map_Handle handle, LV2_URID urid) {
    lv2h_t *host;
    host = (lv2h_t*)handle;
    if (urid > 0 && urid <= host->lv2_uris_size) {
        return host->lv2_uris[urid - 1];
    }
    return NULL;
}

static void lv2h_inst_set_port_value(const char *port_name, void *user_data, const void *value, uint32_t size, uint32_t type) {
    lv2h_inst_t *inst;
    uint32_t port_index;

    (void)size;

    inst = (lv2h_inst_t*)user_data;

    if (type != 0 && type != lv2h_map_uri(inst->plug->host, LV2_ATOM__Float)) {
        return;
    }

    port_index = lv2h_inst_port_index(inst, port_name);
    if (port_index >= inst->plug->port_count) return;

    inst->port_array[port_index].control_val = *((float*)value);
}

static uint32_t lv2h_inst_port_index(lv2h_inst_t *inst, const char *port_name) {
    LilvNode *snode;
    const LilvPort *port;
    snode = lilv_new_string(inst->plug->host->lilv_world, port_name);
    port = lilv_plugin_get_port_by_symbol(inst->plug->lilv_plugin, snode);
    lilv_node_free(snode);
    if (!port) return 0;
    return lilv_port_get_index(inst->plug->lilv_plugin, port);
}


static int lv2h_process_tick(lv2h_t *host) {
    lv2h_event_t *ev, *ev_tmp;
    LL_FOREACH_SAFE(host->event_list, ev, ev_tmp) {
        if (ev->timestamp_ns >= host->ts_now_ns) {
            LL_DELETE(host->event_list, ev);
            lv2h_process_node(ev->node, ev);
        } else {
            break; // TODO nodes should be inserted with LL_INSERT_INORDER
        }
    }
    return LV2H_OK;
}

static int lv2h_process_node(lv2h_node_t *node, lv2h_event_t *ev) {
    // TODO
    // invoke callback
    // incr count
    // check count limit
    // bail if over limit
    // if child:
    //    schedule next event based on divisor/mult, count, and parent->ts_(last|next)_ns, delay_ns
    // else parent:
    //    apply accel if any
    //    schedule next event based on interval_ns
    (void)node;
    (void)ev;
    return LV2H_OK;
}

static int lv2h_port_init(lv2h_port_t *port, uint32_t port_index, lv2h_inst_t *inst) {
    lv2h_t *host;
    lv2h_plug_t *plug;
    const LilvPlugin *lilv_plug;
    const LilvPort *lilv_port;
    LilvInstance *lilv_inst;

    plug = inst->plug;
    host = plug->host;
    lilv_plug = plug->lilv_plugin;
    lilv_inst = inst->lilv_inst;
    lilv_port = lilv_plugin_get_port_by_index(lilv_plug, port_index);

    port->inst = inst;
    port->lilv_port = lilv_port;
    port->port_index = port_index;
    port->port_name = strdup(lilv_node_as_string(lilv_port_get_symbol(lilv_plug, lilv_port)));

    if (lilv_port_is_a(lilv_plug, lilv_port, host->lv2_core_ControlPort)) {
        port->control_val = plug->port_defaults[port_index];
        lilv_instance_connect_port(lilv_inst, port_index, &port->control_val);
    } else if (lilv_port_is_a(lilv_plug, lilv_port, host->lv2_core_AudioPort) || lilv_port_is_a(lilv_plug, lilv_port, host->lv2_core_CVPort)) {
        if (lilv_port_is_a(lilv_plug, lilv_port, host->lv2_core_InputPort)) {
            port->reader_block_mixed = calloc(host->block_size, sizeof(float));
            lilv_instance_connect_port(lilv_inst, port_index, port->reader_block_mixed);
        } else {
            port->writer_block = calloc(host->block_size, sizeof(float));
            lilv_instance_connect_port(lilv_inst, port_index, port->writer_block);
        }
    } else if (lilv_port_is_a(lilv_plug, lilv_port, host->lv2_atom_AtomPort)) {
        if (lilv_port_is_a(lilv_plug, lilv_port, host->lv2_core_InputPort)) {
            port->atom_input = lv2_evbuf_new(1024, LV2_EVBUF_ATOM, 0, lv2h_map_uri(host, LV2_ATOM__Sequence));
            lilv_instance_connect_port(lilv_inst, port_index, lv2_evbuf_get_buffer(port->atom_input));
        } else {
            port->atom_output = (LV2_Atom_Sequence*)calloc(1, sizeof(LV2_Atom_Sequence) + 1024);
            lilv_instance_connect_port(lilv_inst, port_index, port->atom_output);
        }
    }

    return LV2H_OK;
}

static int lv2h_port_deinit(lv2h_port_t *port) {
    free(port->port_name);
    if (port->writer_block) free(port->writer_block);
    if (port->reader_block_mixed) free(port->reader_block_mixed);
    if (port->atom_input) free(port->atom_input);
    if (port->atom_output) free(port->atom_output);
    return LV2H_OK;
}

static int lv2h_run_plugin_inst(lv2h_inst_t *inst, int frame_count, size_t iter, int depth) {
    lv2h_t *host;
    lv2h_port_t *reader_port, *writer_port;
    uint32_t p;
    size_t w;
    int f;

    // TODO check depth to protect againt cycles in graph

    host = inst->plug->host;

    for (p = 0; p < inst->plug->port_count; ++p) {
        reader_port = inst->port_array + p;
        LL_FOREACH(reader_port->writer_port_list, writer_port) {
            // TODO optimize looping over all ports like this
            if (writer_port->inst->current_iter != iter) {
                lv2h_run_plugin_inst(writer_port->inst, frame_count, iter, depth + 1);
            }
        }
        w = 0;
        LL_FOREACH(reader_port->writer_port_list, writer_port) {
            // TODO optimize case when there is 1 writer
            if (w == 0) {
                memcpy(reader_port->reader_block_mixed, writer_port->writer_block, sizeof(float) * inst->plug->host->block_size);
            } else {
                for (f = 0; f < host->block_size; ++f) {
                    reader_port->reader_block_mixed[f] += writer_port->writer_block[f];
                }
            }
            w += 1;
        }
    }
    if (inst->current_iter != iter && inst != host->audio_inst) {
        lilv_instance_run(inst->lilv_inst, frame_count);

        for (p = 0; p < inst->plug->port_count; ++p) {
            reader_port = inst->port_array + p;
            if (reader_port->atom_input) {
                lv2_evbuf_reset(reader_port->atom_input, 1);
            }
        }

        inst->current_iter = iter;
    }
    return LV2H_OK;
}
