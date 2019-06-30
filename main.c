#include "lv2h.h"

static void *lv2h_run_play(void *arg);


static pthread_t audio_thread;
static pthread_t play_thread;
static lv2h_plug_t *plug[4];
static lv2h_inst_t *inst[4];

int main(int argc, char **argv) {
    lv2h_t *host;

    (void)argc;
    (void)argv;

    lv2h_new(44100, 128, 10, &host);

    lv2h_plug_new(host, "http://calf.sourceforge.net/plugins/Monosynth", &plug[0]);
    lv2h_inst_new(plug[0], &inst[0]);
    lv2h_inst_load_preset(inst[0], "http://calf.sourceforge.net/factory_presets#monosynth_FatCats");

    lv2h_plug_new(host, "http://calf.sourceforge.net/plugins/VintageDelay", &plug[1]);
    lv2h_inst_new(plug[1], &inst[1]);

    lv2h_inst_connect(inst[0], "out_l", inst[1], "in_l");
    lv2h_inst_connect(inst[0], "out_r", inst[1], "in_l");

    lv2h_inst_connect_to_audio(inst[1], "out_l", 0);
    lv2h_inst_connect_to_audio(inst[1], "out_r", 1);


    pthread_create(&play_thread, NULL, lv2h_run_play, host);
    pthread_create(&audio_thread, NULL, lv2h_run_audio, host);

    lv2h_run(host);
    lv2h_free(host);

    pthread_join(audio_thread, NULL);
    pthread_join(play_thread, NULL);

    return 0;
}

static void *lv2h_run_play(void *arg) {
    uint8_t msg[3];
    msg[0] = 0x90;
    msg[1] = 0x30;
    msg[2] = 0x7f;

    while (1) {
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
}