#include "lv2h.h"

static void lv2h_audio_callback(struct SoundIoOutStream *outstream, int frame_count_min, int frame_count_max);

extern  lv2h_inst_t *mono;

static void lv2h_audio_callback(struct SoundIoOutStream *outstream, int frame_count_min, int frame_count_max) {
    lv2h_t *host;
    struct SoundIoChannelArea *areas;
    struct SoundIoChannelLayout *layout;
    float *sample_ptr;
    int channel;
    int err;
    int frame;
    int frame_count;
    int frames_left;

    (void)frame_count_min;

    host = (lv2h_t*)outstream->userdata;
    layout = &outstream->layout;
    frames_left = frame_count_max;

    while (frames_left > 0) {
        frame_count = frames_left < host->block_size ? frames_left : host->block_size;

        if ((err = soundio_outstream_begin_write(outstream, &areas, &frame_count))) {
            LV2H_RETURN_ERR_VOID(host, "audio: soundio_outstream_begin_write error: %s\n", soundio_strerror(err));
        }
        if (frame_count < 1) {
            break;
        }

        // TODO mutex for data shared by threads
        lv2h_run_plugin_insts(host, frame_count);

        for (frame = 0; frame < frame_count; frame += 1) {
            for (channel = 0; channel < layout->channel_count; channel += 1) {
                sample_ptr = (float*)(areas[channel].ptr + areas[channel].step * frame);
                *sample_ptr = host->audio_inst->port_array[channel].reader_block_mixed[frame];
            }
        }

        if ((err = soundio_outstream_end_write(outstream))) {
            LV2H_RETURN_ERR_VOID(host, "audio: soundio_outstream_end_write error: %s\n", soundio_strerror(err));
        }

        frames_left -= frame_count;
    }
}


void *lv2h_run_audio(void *arg) {
    lv2h_t *host;
    struct SoundIoDevice *device;
    struct SoundIoOutStream *outstream;
    struct SoundIo *soundio;
    int default_out_device_index;
    int err;

    host = (lv2h_t*)arg;

    if (!(soundio = soundio_create())) {
        LV2H_RETURN_ERR_ARG(host, NULL, "audio: soundio_create\n%s", "");
    }
    if ((err = soundio_connect(soundio))) {
        LV2H_RETURN_ERR_ARG(host, NULL, "audio: soundio_connect: %s\n", soundio_strerror(err));
    }

    soundio_flush_events(soundio);

    if ((default_out_device_index = soundio_default_output_device_index(soundio)) < 0) {
        LV2H_RETURN_ERR_ARG(host, NULL, "audio: soundio_default_output_device_index: no output device found\n%s", "");
    }
    if (!(device = soundio_get_output_device(soundio, default_out_device_index))) {
        LV2H_RETURN_ERR_ARG(host, NULL, "audio: soundio_get_output_device\n%s", "");
    }

    //fprintf(stderr, "Output device: %s\n", device->name);
    //fprintf(stderr, "nearest sample rate to 44.1k is %d\n", soundio_device_nearest_sample_rate(device, 44100));

    outstream = soundio_outstream_create(device);
    outstream->format = SoundIoFormatFloat32NE;
    outstream->write_callback = lv2h_audio_callback;
    outstream->userdata = host;

    if ((err = soundio_outstream_open(outstream))) {
        LV2H_RETURN_ERR_ARG(host, NULL, "audio: soundio_outstream_open: %s\n", soundio_strerror(err));
    }
    if (outstream->layout_error) {
        LV2H_RETURN_ERR_ARG(host, NULL, "audio: layout_error: %s\n", soundio_strerror(outstream->layout_error));
    }
    if ((err = soundio_outstream_start(outstream))) {
        LV2H_RETURN_ERR_ARG(host, NULL, "audio: soundio_outstream_start: %s\n", soundio_strerror(err));
    }

    while (!host->done) {
        soundio_wait_events(soundio);
    }

    soundio_outstream_destroy(outstream);
    soundio_device_unref(device);
    soundio_destroy(soundio);
    return NULL;
}
