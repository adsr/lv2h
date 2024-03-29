#include "lv2h.h"

static void *lv2h_run_play(void *arg);
static int lv2h_node_callback0(lv2h_node_t *node, void *udata, int count);
static int lv2h_node_callback1(lv2h_node_t *node, void *udata, int count);

static pthread_t audio_thread;
static pthread_t play_thread;
static lv2h_plug_t *plug[4];
static lv2h_inst_t *inst[4];

int main(int argc, char **argv) {
    lv2h_t *host;
    lv2h_node_t *node[2];
    int rv;

    (void)argc;
    (void)argv;

    lv2h_new(44100, 128, 10, &host);

    // lv2h_plug_new(host, "http://drobilla.net/plugins/mda/JX10", &plug[0]);
    // lv2h_inst_new(plug[0], &inst[0]);
    // lv2h_inst_set_param(inst[0], "env_rel", 0.0);
    // lv2h_inst_set_param(inst[0], "env_dec", 0.0);
    // lv2h_inst_set_param(inst[0], "vibrato", 1.0);
    // lv2h_inst_set_param(inst[0], "glide", 0.0);

    // lv2h_plug_new(host, "http://calf.sourceforge.net/plugins/Monosynth", &plug[0]);
    // lv2h_inst_new(plug[0], &inst[0]);
    // lv2h_inst_load_preset(inst[0], "http://calf.sourceforge.net/factory_presets#monosynth_SquareWorm");
    // lv2h_inst_set_param(inst[0], "adsr_r", 0.0);
    // lv2h_inst_set_param(inst[0], "adsr_d", 0.0);

    lv2h_plug_new(host, "https://github.com/jpcima/ADLplug", &plug[0]);
    lv2h_inst_new(plug[0], &inst[0]);
    lv2h_inst_set_param(inst[0], "volume", 0.05);

    lv2h_plug_new(host, "http://calf.sourceforge.net/plugins/VintageDelay", &plug[1]);
    lv2h_inst_new(plug[1], &inst[1]);

    lv2h_plug_new(host, "http://calf.sourceforge.net/plugins/Monosynth", &plug[2]);
    lv2h_inst_new(plug[2], &inst[2]);
    lv2h_inst_set_param(inst[2], "adsr_r", 0.0);
    lv2h_inst_set_param(inst[2], "adsr_d", 0.0);
    lv2h_inst_set_param(inst[2], "o1_wave", 1.0);
    lv2h_inst_set_param(inst[2], "master", 5.0);

    //lv2h_inst_connect(inst[0], "left_out", inst[1], "in_l");
    //lv2h_inst_connect(inst[0], "right_out", inst[1], "in_l");
    //lv2h_inst_connect(inst[0], "out_l", inst[1], "in_l");
    //lv2h_inst_connect(inst[0], "out_r", inst[1], "in_l");
    lv2h_inst_connect(inst[0], "lv2_audio_out_1", inst[1], "in_l");
    lv2h_inst_connect(inst[0], "lv2_audio_out_2", inst[1], "in_r");

    lv2h_inst_connect_to_audio(inst[1], "out_l", 0);
    lv2h_inst_connect_to_audio(inst[1], "out_r", 1);

    lv2h_inst_connect_to_audio(inst[2], "out_l", 0);
    lv2h_inst_connect_to_audio(inst[2], "out_r", 1);

    lv2h_node_new(host, lv2h_node_callback0, NULL, &node[0]);
    lv2h_node_set_interval(node[0], 20);
    lv2h_node_set_interval_factor(node[0], 1.01);
    //lv2h_node_set_count_limit(node[0], 96);

    lv2h_node_new(host, lv2h_node_callback1, NULL, &node[1]);
    lv2h_node_set_divisor(node[1], 3);
    lv2h_node_follow(node[1], node[0]);

    // pthread_create(&play_thread, NULL, lv2h_run_play, host);
    pthread_create(&audio_thread, NULL, lv2h_run_audio, host);

    //sleep(1);

    lv2h_run(host);
    lv2h_free(host);

    pthread_join(audio_thread, NULL);
    pthread_join(play_thread, NULL);

    return 0;
}

static int lv2h_node_callback0(lv2h_node_t *node, void *udata, int count) {
    //printf("0count=%d\n", count);
    uint8_t note, reg, val;

    note = (count*7 + 0x30) % 0x7f;

    lv2h_inst_play(inst[0], "lv2_events_in", 0x00, note, -1, -1, -1, 0x70, 300);

    lv2h_inst_play(inst[0], "lv2_events_in", 0x09, note, -1, -1, -1, 0x70, 300);

    return 0;
}

static int lv2h_node_callback1(lv2h_node_t *node, void *udata, int count) {
    //printf("1count=%d\n", count);
    uint8_t note, reg, val;
    uint8_t sysex[8] = { 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf7 };

    note = (count*11 + 0x30) % 0x7f;

    reg = 0xA0;
    val = note % 0xFF;
    sysex[3] = reg >> 7;
    sysex[4] = reg & 0x7F;
    sysex[5] = val >> 7;
    sysex[6] = val & 0x7F;
    lv2h_inst_send_midi(inst[0], "lv2_events_in", sysex, 8);

    reg = 0xB0;
    val = 0x20 + (count % 0x1F);
    sysex[3] = reg >> 7;
    sysex[4] = reg & 0x7F;
    sysex[5] = val >> 7;
    sysex[6] = val & 0x7F;
    lv2h_inst_send_midi(inst[0], "lv2_events_in", sysex, 8);

    return 0;
}

static void *lv2h_run_play(void *arg) {
    lv2h_t *host;
    uint8_t msg[3];
    msg[0] = 0x90;
    msg[1] = 0x30;
    msg[2] = 0x7f;
    int i;

    host = (lv2h_t*)arg;

    for (i = 0; i < 5; i++) {
        msg[0] = 0x90;
        msg[1] += 1;
        msg[2] = 0x70;
        lv2h_inst_send_midi(inst[0], "midi_in", msg, 3);
        printf("sent note on\n");
        sleep(1);

        msg[0] = 0x80;
        msg[2] = 0x00;
        lv2h_inst_send_midi(inst[0], "midi_in", msg, 3);
        printf("sent note off\n");
        sleep(1);
    }

    host->done = 1;
    return NULL;
}
