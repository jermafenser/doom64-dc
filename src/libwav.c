#if 0
#include "libwav.h"

#include <string.h>

#include <stdio.h>

int wav_get_info_adpcm(file_t file, WavFileInfo *result) {
    result->format = WAVE_FORMAT_YAMAHA_ADPCM;
    result->channels = 2;
    result->sample_rate = 44100;
    result->sample_size = 4;
    result->data_length = fs_total(file);

    result->data_offset = 0;

    return 1;
}
#endif