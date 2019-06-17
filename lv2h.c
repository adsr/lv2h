#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <soundio/soundio.h>
#include <lilv-0/lilv/lilv.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/buf-size/buf-size.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>

#include "lv2_evbuf.h"

static LV2_URID uri_map(LV2_URID_Map_Handle handle, const char *uri);
static const char *uri_unmap(LV2_URID_Map_Handle handle, LV2_URID urid);
static void write_callback(struct SoundIoOutStream *outstream, int frame_count_min, int frame_count_max);
static int run_audio();
static float crandom(float min, float max);
static void set_port_value(const char* port_symbol, void* user_data, const void* value, uint32_t /*size*/, uint32_t type);
static uint32_t port_index(const char* port_symbol);


static uint32_t block_size = 512;

static char **uris = NULL;
static size_t uris_size = 0;

static LilvWorld    *world;
static LilvInstance *instance;
static LilvPlugin   *plugin;
static float *out[2];
static float *mins;
static float *maxs;
static float *controls;
static LV2_Evbuf *evbuf;


int main(int argc, char **argv) {
    LilvNode     *uri;
    const LilvPlugins  *plugins;

    LV2_URID_Map       map           = { uris, uri_map };
    LV2_Feature        map_feature   = { LV2_URID_MAP_URI, &map };
    LV2_URID_Unmap     unmap         = { uris, uri_unmap };
    LV2_Feature        unmap_feature = { LV2_URID_UNMAP_URI, &unmap };
    const LV2_Feature* features[]    = { &map_feature, &unmap_feature, NULL };

    world = lilv_world_new();
    lilv_world_load_all(world);

    LilvNode *lv2_InputPort   = lilv_new_uri(world, LV2_CORE__InputPort);
    LilvNode *lv2_OutputPort  = lilv_new_uri(world, LV2_CORE__OutputPort);
    LilvNode *lv2_AudioPort   = lilv_new_uri(world, LV2_CORE__AudioPort);
    LilvNode *lv2_ControlPort = lilv_new_uri(world, LV2_CORE__ControlPort);
    LilvNode *lv2_CVPort      = lilv_new_uri(world, LV2_CORE__CVPort);
    LilvNode *atom_AtomPort   = lilv_new_uri(world, LV2_ATOM__AtomPort);
    LilvNode *atom_Sequence   = lilv_new_uri(world, LV2_ATOM__Sequence);
    LilvNode *urid_map        = lilv_new_uri(world, LV2_URID__map);


    //LilvNode *midi_MidiEvent  = lilv_new_uri(world, LV2_MIDI__MidiEvent);

     /*
    LV2_Atom_Sequence midi_in;
    midi_in.atom.size = sizeof(LV2_Atom_Sequence_Body) + ;
    midi_in.atom.type = atom_Sequence;
    midi_in.body.unit = 0;
    midi_in.body.pad = 0;


     = {
        { sizeof(LV2_Atom_Sequence_Body),
          uri_table_map(&uri_table, LV2_ATOM__Sequence) },
        { 0, 0 } };
    */

    const size_t atom_capacity = 1024;
    LV2_Atom_Sequence* seq_out = (LV2_Atom_Sequence*)malloc(sizeof(LV2_Atom_Sequence) + atom_capacity);


    plugins = lilv_world_get_all_plugins(world);

    uri = lilv_new_uri(world, argc >= 2 ? argv[1] : "http://calf.sourceforge.net/plugins/Monosynth");

    plugin = lilv_plugins_get_by_uri(plugins, uri);

    instance = lilv_plugin_instantiate(plugin, 44100.0, features);

    uint32_t n_ports = lilv_plugin_get_num_ports(plugin);
    controls = (float*)calloc(n_ports, sizeof(float));
    mins = (float*)calloc(n_ports, sizeof(float));
    maxs = (float*)calloc(n_ports, sizeof(float));
    lilv_plugin_get_port_ranges_float(plugin, mins, maxs, controls);


    float in = 0;
    int out_n = 0;

    // https://github.com/Ardour/ardour/blob/48b960fdefef2b3c02f47baca328638e9ecef9a4/libs/ardour/lv2_plugin.cc#L3548-L3570
    for (uint32_t i = 0; i < n_ports; ++i) {
        const LilvPort* port = lilv_plugin_get_port_by_index(plugin, i);
        if (lilv_port_is_a(plugin, port, lv2_ControlPort)) {
            //controls[i] = (maxs[i] + mins[i]) / 2.f;
            lilv_instance_connect_port(instance, i, &controls[i]);
            fprintf(stderr, "%s=%3.2f\n",   lilv_node_as_string (lilv_port_get_symbol(plugin, port)), controls[i]);
        } else if (lilv_port_is_a(plugin, port, lv2_AudioPort) || lilv_port_is_a(plugin, port, lv2_CVPort)) {
            if (lilv_port_is_a(plugin, port, lv2_InputPort)) {
                lilv_instance_connect_port(instance, i, &in);
                fprintf(stderr, "port %d is an intput\n", i);
            } else if (lilv_port_is_a(plugin, port, lv2_OutputPort)) {
                out[out_n] = calloc(block_size, sizeof(float));
                lilv_instance_connect_port(instance, i, out[out_n]);
                fprintf(stderr, "port %d is an output (%d)\n", i, out_n);
                out_n += 1;
            } else {
                fprintf(stderr, "port %d neither input nor output, skipping\n", i);
            }
        } else if (lilv_port_is_a(plugin, port, atom_AtomPort)) {
            if (lilv_port_is_a(plugin, port, lv2_InputPort)) {
                fprintf(stderr, "port %d is an atom input\n", i);
                evbuf = lv2_evbuf_new(1024, LV2_EVBUF_ATOM, 0, uri_map(uris, LV2_ATOM__Sequence));
                lilv_instance_connect_port(instance, i, lv2_evbuf_get_buffer(evbuf));
            } else {
                fprintf(stderr, "port %d is an atom output\n", i);
                lilv_instance_connect_port(instance, i, seq_out);
            }
        } else {
            fprintf(stderr, "port %d has unknown type, skipping\n", i);
        }
    }

    lilv_instance_activate(instance);

    // load preset
    LilvNode *preset = lilv_new_uri(world, "http://calf.sourceforge.net/factory_presets#monosynth_FatCats");
    LilvState *state = lilv_state_new_from_world(world, &map, preset);
    lilv_state_restore(state, instance, set_port_value, NULL, 0, NULL);

    // send note
    //uint8_t msg[3] = {0x90, 0x40, 0x7f};
    //LV2_Evbuf_Iterator end = lv2_evbuf_begin(evbuf);
    //lv2_evbuf_write(&end, 0, 0, uri_map(uris, LV2_MIDI__MidiEvent), 3, msg);

    // play
    run_audio();

    lilv_instance_deactivate(instance);
    lilv_instance_free(instance);

    lilv_node_free(lv2_InputPort);
    lilv_node_free(lv2_OutputPort);
    lilv_node_free(lv2_AudioPort);
    lilv_node_free(lv2_ControlPort);
    lilv_node_free(lv2_CVPort);
    lilv_node_free(atom_AtomPort);
    lilv_node_free(atom_Sequence);
    lilv_node_free(urid_map);

    lilv_world_free(world);

    if (uris) free(uris);
}

static LV2_URID uri_map(LV2_URID_Map_Handle handle, const char *uri) {
    size_t i;
    (void)handle;
    for (i = 0; i < uris_size; ++i) {
        if (!strcmp(uris[i], uri)) {
            return i + 1;
        }
    }
    uris = (char **)realloc(uris, ++uris_size * sizeof(char *));
    uris[uris_size - 1] = strdup(uri);
    return uris_size;
}

static const char *uri_unmap(LV2_URID_Map_Handle handle, LV2_URID urid) {
    (void)handle;
    if (urid > 0 && urid <= uris_size) {
        return uris[urid - 1];
    }
    return NULL;
}

static void write_callback(struct SoundIoOutStream *outstream,
        int frame_count_min, int frame_count_max)
{
    const struct SoundIoChannelLayout *layout = &outstream->layout;
    struct SoundIoChannelArea *areas;
    int frames_left = frame_count_max;
    int err;


    static int ii = 0;
    if (ii % 10 == 0) {
        int note = (ii/10)+0x40 % 128;
        uint8_t msg[3] = {0x90, 0, 0}; // JESUS 
        LV2_Evbuf_Iterator end;

        // send prev note_off
        //msg[1] = note-1;
        //msg[2] = 0;
        //end = lv2_evbuf_end(evbuf);
        //lv2_evbuf_write(&end, 0, 0, uri_map(uris, LV2_MIDI__MidiEvent), 3, msg);

        // send note_on
        msg[1] = note;
        msg[2] = 0x7f;
        end = lv2_evbuf_end(evbuf);
        lv2_evbuf_write(&end, 0, 0, uri_map(uris, LV2_MIDI__MidiEvent), 3, msg);
    }
    ++ii;

    while (frames_left > 0) {
        int frame_count = frames_left < block_size ? frames_left : block_size;

        if ((err = soundio_outstream_begin_write(outstream, &areas, &frame_count))) {
            fprintf(stderr, "%s\n", soundio_strerror(err));
            exit(1);
        }

        if (!frame_count)
            break;

        lilv_instance_run(instance, frame_count);
        lv2_evbuf_reset(evbuf, 1);

        for (int frame = 0; frame < frame_count; frame += 1) {
            for (int channel = 0; channel < layout->channel_count; channel += 1) {
                float sample = out[channel][frame];
                //float sample = crandom(-1.f, 1.f);
                float *ptr = (float*)(areas[channel].ptr + areas[channel].step * frame);
                *ptr = sample;
                // if (sample != 0.f) printf("%3.2f ", sample);
            }
        }

        if ((err = soundio_outstream_end_write(outstream))) {
            fprintf(stderr, "%s\n", soundio_strerror(err));
            exit(1);
        }

        frames_left -= frame_count;
        printf("\nframe_count_min=%d frame_count_max=%d frame_count=%d frames_left=%d\n", frame_count_min, frame_count_max, frame_count, frames_left);
    }
}


static int run_audio() {
    int err;
    struct SoundIo *soundio = soundio_create();

    if (!soundio) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    if ((err = soundio_connect(soundio))) {
        fprintf(stderr, "error connecting: %s", soundio_strerror(err));
        return 1;
    }

    soundio_flush_events(soundio);

    int default_out_device_index = soundio_default_output_device_index(soundio);
    if (default_out_device_index < 0) {
        fprintf(stderr, "no output device found");
        return 1;
    }

    struct SoundIoDevice *device = soundio_get_output_device(soundio, default_out_device_index);
    if (!device) {
        fprintf(stderr, "out of memory");
        return 1;
    }

    fprintf(stderr, "Output device: %s\n", device->name);

    fprintf(stderr, "nearest sample rate to 44.1k is %d\n", soundio_device_nearest_sample_rate(device, 44100));

    struct SoundIoOutStream *outstream = soundio_outstream_create(device);
    outstream->format = SoundIoFormatFloat32NE;
    outstream->write_callback = write_callback;

    if ((err = soundio_outstream_open(outstream))) {
        fprintf(stderr, "unable to open device: %s", soundio_strerror(err));
        return 1;
    }

    if (outstream->layout_error)
        fprintf(stderr, "unable to set channel layout: %s\n", soundio_strerror(outstream->layout_error));

    if ((err = soundio_outstream_start(outstream))) {
        fprintf(stderr, "unable to start device: %s", soundio_strerror(err));
        return 1;
    }

    for (;;)
        soundio_wait_events(soundio);

    soundio_outstream_destroy(outstream);
    soundio_device_unref(device);
    soundio_destroy(soundio);
    return 0;
}

static float crandom(float min, float max) {
    return (((float)rand() / (float)RAND_MAX) * (max - min)) + min;
}

static void set_port_value(const char* port_symbol, void* user_data, const void* value, uint32_t size, uint32_t type) {
    if (type != 0 && type != uri_map(uris, LV2_ATOM__Float)) {
        fprintf(stderr, "non float type: %d\n", type);
        return;
    }
    uint32_t pi = port_index(port_symbol);
    float fv = *(const float*)value;
    fprintf(stderr, "%s=%3.2f\n", port_symbol, fv);
    controls[pi] = fv;
}

static uint32_t port_index(const char* port_symbol) {
    LilvNode *snode = lilv_new_string(world, port_symbol);
    LilvPort *port = lilv_plugin_get_port_by_symbol(plugin, snode);
    lilv_node_free(snode);
    if (!port) {
        fprintf(stderr, "cannot find port index for symbol=%s\n", port_symbol);
        return 0;
    }
    return lilv_port_get_index(plugin, port);
}
