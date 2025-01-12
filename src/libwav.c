#include "libwav.h"

#include <string.h>

#include <stdio.h>

typedef struct __attribute__((__packed__)) {
    uint8_t riff[4];
    int32_t totalsize;
    uint8_t riff_format[4];
} wavmagic_t;

typedef struct __attribute__((__packed__)) {
    uint8_t id[4];
    size_t size;
} chunkhdr_t;

typedef struct __attribute__((__packed__)) {
    int16_t format;
    int16_t channels;
    int32_t sample_rate;
    int32_t byte_per_sec;
    int16_t blocksize;
    int16_t sample_size;
} fmthdr_t;

int wav_get_info_adpcm(file_t file, WavFileInfo *result) {
    result->format = WAVE_FORMAT_YAMAHA_ADPCM;
    result->channels = 2;
    result->sample_rate = 44100;
    result->sample_size = 4;
    result->data_length = fs_total(file);

    result->data_offset = 0;

    return 1;
}