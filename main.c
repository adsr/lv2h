#include "lv2h.h"

static pthread_t audio_thread;

lv2h_inst_t *mono;

int main(int argc, char **argv) {
    lv2h_t *host;
    lv2h_plug_t *plug;
    lv2h_inst_t *inst;

    (void)argc;
    (void)argv;

    lv2h_new(44100, 512, 10, &host);

    lv2h_plug_new(host, "http://calf.sourceforge.net/plugins/Monosynth", &plug);
    lv2h_inst_new(plug, &inst);
    mono = inst;
    lv2h_inst_load_preset(inst, "http://calf.sourceforge.net/factory_presets#monosynth_FatCats");

    lv2h_inst_connect_to_audio(inst, "out_l", 0);
    lv2h_inst_connect_to_audio(inst, "out_r", 1);

    pthread_create(&audio_thread, NULL, lv2h_run_audio, host);

    lv2h_run(host);
    lv2h_free(host);

    pthread_join(audio_thread, NULL);

    return 0;
}
