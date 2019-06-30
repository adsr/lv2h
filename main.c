#include "lv2h.h"

static void *lv2h_run_play(void *arg);
static int lv2h_node_callback(lv2h_node_t *node, void *udata, int count);

static pthread_t audio_thread;
static pthread_t play_thread;
static lv2h_plug_t *plug[4];
static lv2h_inst_t *inst[4];

int main(int argc, char **argv) {
    lv2h_t *host;
    lv2h_node_t *node;
    int rv;

    (void)argc;
    (void)argv;

    lv2h_new(44100, 128, 1, &host);

    lv2h_plug_new(host, "http://drobilla.net/plugins/mda/JX10", &plug[0]);
    lv2h_inst_new(plug[0], &inst[0]);

    lv2h_plug_new(host, "http://calf.sourceforge.net/plugins/VintageDelay", &plug[1]);
    lv2h_inst_new(plug[1], &inst[1]);

    rv = lv2h_inst_load_preset(inst[0], "http://drobilla.net/plugins/mda/presets#JX10-303-saw-bass");
    printf("lv2h_inst_load_preset rv=%d\n", rv);

    // lv2h_inst_connect(inst[0], "out_l", inst[1], "in_l");
    // lv2h_inst_connect(inst[0], "out_r", inst[1], "in_l");

    lv2h_inst_connect(inst[0], "left_out", inst[1], "in_l");
    lv2h_inst_connect(inst[0], "right_out", inst[1], "in_l");

    lv2h_inst_connect_to_audio(inst[1], "out_l", 0);
    lv2h_inst_connect_to_audio(inst[1], "out_r", 1);

    lv2h_node_new(host, lv2h_node_callback, NULL, &node);
    lv2h_node_set_interval(node, 50);
    // lv2h_node_set_count_limit(node, 5);

    // pthread_create(&play_thread, NULL, lv2h_run_play, host);
    pthread_create(&audio_thread, NULL, lv2h_run_audio, host);

    sleep(1);

    lv2h_run(host);
    lv2h_free(host);

    pthread_join(audio_thread, NULL);
    pthread_join(play_thread, NULL);

    return 0;
}

static int lv2h_node_callback(lv2h_node_t *node, void *udata, int count) {
    printf("count=%d\n", count);
    uint8_t note;
    note = (count + 0x30) % 0x7f;
    // lv2h_inst_play(inst[0], "midi_in", 0, 0x30 + count, 0x30 + count + 3,  0x30 + count + 3 + 4, -1, 0x70, 500);
    // lv2h_inst_play(inst[0], "midi_in", 0, note, -1, -1, -1, 0x70, 40);
    lv2h_inst_play(inst[0], "event_in", 0, note, -1, -1, -1, 0x70, 40);
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
