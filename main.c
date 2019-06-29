#include "lv2h.h"

static pthread_t audio_thread;

int main(int argc, char **argv) {
    lv2h_t *host;
    lv2h_plug_t *plug;
    lv2h_inst_t *inst;

    (void)argc;
    (void)argv;

    lv2h_new(44100, 512, 10, &host);

    pthread_create(&audio_thread, NULL, lv2h_run_audio, host);

    lv2h_plug_new(host, "http://calf.sourceforge.net/plugins/Monosynth", &plug);
    lv2h_inst_new(plug, &inst);

    lv2h_run(host);
    lv2h_free(host);

    pthread_join(audio_thread, NULL);

    return 0;
}
