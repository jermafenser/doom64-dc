#include <kos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include <kos/thread.h>
#include <dc/sound/stream.h>

#include "sndwav.h"
#include "libwav.h"

/* Keep track of things from the Driver side */
#define SNDDRV_STATUS_NULL         0x00
#define SNDDRV_STATUS_READY        0x01
#define SNDDRV_STATUS_DONE         0x02

/* Keep track of things from the Decoder side */
#define SNDDEC_STATUS_NULL         0x00
#define SNDDEC_STATUS_READY        0x01
#define SNDDEC_STATUS_STREAMING    0x02
#define SNDDEC_STATUS_PAUSING      0x03
#define SNDDEC_STATUS_STOPPING     0x04
#define SNDDEC_STATUS_RESUMING     0x05

typedef void *(*snddrv_cb)(snd_stream_hnd_t, int, int*);

typedef struct {
    /* The buffer on the AICA side */
    snd_stream_hnd_t shnd;

    /* We either read the wav data from a file or 
       we read from a buffer */
    file_t wave_file;
    const uint8_t *wave_buf;

    /* Contains the buffer that we are going to send
       to the AICA in the callback.  Should be 32-byte
       aligned */
    uint8_t *drv_buf;

    /* Status of the stream that can be started, stopped
       paused, ready. etc */
    volatile int status;

    snddrv_cb callback;

    uint32_t loop;
    uint32_t vol;         /* 0-255 */

    uint32_t format;      /* Wave format */
    uint32_t channels;    /* 1-Mono/2-Stereo */
    uint32_t sample_rate; /* 44100Hz */
    uint32_t sample_size; /* 4/8/16-Bit */

    /* Offset into the file or buffer where the audio 
       data starts */
    uint32_t data_offset;

    /* The length of the audio data */
    uint32_t data_length;

    /* Used only in reading wav data from a buffer 
       and not a file */
    uint32_t buf_offset;  
   
} snddrv_hnd;

static snddrv_hnd stream;//[SND_STREAM_MAX];
static volatile int sndwav_status = SNDDRV_STATUS_NULL;
static kthread_t *audio_thread;
static mutex_t stream_mutex = MUTEX_INITIALIZER;

static void *sndwav_thread(void *param);
static void *wav_file_callback(snd_stream_hnd_t hnd, int req, int *done);

int wav_init(void) {
    if(snd_stream_init() < 0)
        return 0;

    //for(i = 0; i < SND_STREAM_MAX; i++) {
        stream/* s[i] */.shnd = SND_STREAM_INVALID;
        stream/* s[i] */.vol = 255;
        stream/* s[i] */.status = SNDDEC_STATUS_NULL;
        stream/* s[i] */.callback = NULL;
    //}

    audio_thread = thd_create(0, sndwav_thread, NULL);
    if(audio_thread != NULL) {
        sndwav_status = SNDDRV_STATUS_READY;
        return 1;
	}
    else {
        return 0;
    }
}

void wav_shutdown(void) {
    int i;

    sndwav_status = SNDDRV_STATUS_DONE;

    thd_join(audio_thread, NULL);

    for(i = 0; i < SND_STREAM_MAX; i++) {
        wav_destroy(i);
    }
}

void wav_destroy(wav_stream_hnd_t hnd) {
    if(stream/* s[hnd] */.shnd == SND_STREAM_INVALID)
        return;

    mutex_lock(&stream_mutex);

    snd_stream_destroy(stream/* s[hnd] */.shnd);
    stream/* s[hnd] */.shnd = SND_STREAM_INVALID;
    stream/* s[hnd] */.status = SNDDEC_STATUS_NULL;
    stream/* s[hnd] */.vol = 255;
    stream/* s[hnd] */.callback = NULL;

    if(stream/* s[hnd] */.wave_file != FILEHND_INVALID)
        fs_close(stream/* s[hnd] */.wave_file);

    if(stream/* s[hnd] */.drv_buf) {
        free(stream/* s[hnd] */.drv_buf);
        stream/* s[hnd] */.drv_buf = NULL;
    }

    mutex_unlock(&stream_mutex);
}

wav_stream_hnd_t wav_create(const char *filename, int loop) {
    int fn_len;
    file_t file;
    WavFileInfo info;
    wav_stream_hnd_t index;

    if(filename == NULL) {
dbgio_printf("wav_create null filename\n");
        return SND_STREAM_INVALID;
	}


    file = fs_open(filename, O_RDONLY);

    if(file == FILEHND_INVALID) {
dbgio_printf("wav_create FILEHND_INVALID\n");
        return SND_STREAM_INVALID;
	}

    index = snd_stream_alloc(wav_file_callback, SND_STREAM_BUFFER_MAX);
    if(index == SND_STREAM_INVALID) {
dbgio_printf("wav_create sndstreamalloc INVALID\n");
        fs_close(file);
        snd_stream_destroy(index);
        return SND_STREAM_INVALID;
    }
snd_stream_volume(index,0);

    fn_len = strlen(filename);

    /* Check for ".raw" or ".pcm" extension */
    if (fn_len >= 4 && 
        ((strcmp(&filename[fn_len - 4], ".raw") == 0) ||
        (strcmp(&filename[fn_len - 4], ".pcm") == 0))) {
        wav_get_info_cdda(file, &info);
    }
    /* Check for ".adpcm" extension */
    else if (fn_len >= 6 && strcmp(&filename[fn_len - 6], ".adpcm") == 0) {
        wav_get_info_adpcm(file, &info);
    }
    /* Default case: handle other file types */
    else if(!wav_get_info_file(file, &info)) {
dbgio_printf("wav_create got to default info case\n");
        fs_close(file);
        snd_stream_destroy(index);
        return SND_STREAM_INVALID;
    }

    stream/* s[index] */.drv_buf = memalign(32, SND_STREAM_BUFFER_MAX);

    if(stream/* s[index] */.drv_buf == NULL) {
dbgio_printf("wav_create drv_buf NULL\n");
        fs_close(file);
        snd_stream_destroy(index);
        return SND_STREAM_INVALID;
    }

    stream/* s[index] */.shnd = index;
    stream/* s[index] */.wave_file = file;
    stream/* s[index] */.loop = loop;
    stream/* s[index] */.callback = wav_file_callback;

    stream/* s[index] */.format = info.format;
    stream/* s[index] */.channels = info.channels;
    stream/* s[index] */.sample_rate = info.sample_rate;
    stream/* s[index] */.sample_size = info.sample_size;
    stream/* s[index] */.data_length = info.data_length;
    stream/* s[index] */.data_offset = info.data_offset;
    
    fs_seek(stream/* s[index] */.wave_file, stream/* s[index] */.data_offset, SEEK_SET);
    stream/* s[index] */.status = SNDDEC_STATUS_READY;
    
    return index;
}
#if 0
wav_stream_hnd_t wav_create_fd(file_t file, int loop) {
    WavFileInfo info;
    wav_stream_hnd_t index;

    if(file == FILEHND_INVALID)
        return SND_STREAM_INVALID;

    index = snd_stream_alloc(wav_file_callback, SND_STREAM_BUFFER_MAX);

    if(index == SND_STREAM_INVALID) {
        fs_close(file);
        snd_stream_destroy(index);
        return SND_STREAM_INVALID;
    }
    
    if(!wav_get_info_file(file, &info)) {
        fs_close(file);
        snd_stream_destroy(index);
        return SND_STREAM_INVALID;
    }

    streams[index].drv_buf = memalign(32, SND_STREAM_BUFFER_MAX);

    if(streams[index].drv_buf == NULL) {
        fs_close(file);
        snd_stream_destroy(index);
        return SND_STREAM_INVALID;
    }

    streams[index].shnd = index;
    streams[index].wave_file = file;
    streams[index].loop = loop;
    streams[index].callback = wav_file_callback;

    streams[index].format = info.format;
    streams[index].channels = info.channels;
    streams[index].sample_rate = info.sample_rate;
    streams[index].sample_size = info.sample_size;
    streams[index].data_length = info.data_length;
    streams[index].data_offset = info.data_offset;
    
    fs_seek(streams[index].wave_file, streams[index].data_offset, SEEK_SET);
    streams[index].status = SNDDEC_STATUS_READY;
    
    return index;
}

wav_stream_hnd_t wav_create_buf(const uint8_t *buf, int loop) {
    WavFileInfo info;
    wav_stream_hnd_t index;

    if(buf == NULL)
        return SND_STREAM_INVALID;

    index = snd_stream_alloc(wav_file_callback, SND_STREAM_BUFFER_MAX);

    if(index == SND_STREAM_INVALID) {
        snd_stream_destroy(index);
        return SND_STREAM_INVALID;
    }
    
    if(!wav_get_info_buffer(buf, &info)) {
        snd_stream_destroy(index);
        return SND_STREAM_INVALID;
    }

    streams[index].drv_buf = memalign(32, SND_STREAM_BUFFER_MAX);

    if(streams[index].drv_buf == NULL) {
        snd_stream_destroy(index);
        return SND_STREAM_INVALID;
    }

    streams[index].shnd = index;
    streams[index].wave_buf = buf;
    streams[index].loop = loop;
    streams[index].callback = wav_buf_callback;

    streams[index].format = info.format;
    streams[index].channels = info.channels;
    streams[index].sample_rate = info.sample_rate;
    streams[index].sample_size = info.sample_size;
    streams[index].data_length = info.data_length;
    streams[index].data_offset = info.data_offset;
    
    streams[index].buf_offset = info.data_offset;
    streams[index].status = SNDDEC_STATUS_READY;

    return index;
}
#endif
void wav_play(wav_stream_hnd_t hnd) {
    if(stream/* s[hnd] */.status == SNDDEC_STATUS_STREAMING)
       return;

    stream/* s[hnd] */.status = SNDDEC_STATUS_RESUMING;
}

void wav_play_volume(wav_stream_hnd_t hnd) {
    if(stream/* s[hnd] */.status == SNDDEC_STATUS_STREAMING)
       return;

    stream/* s[hnd] */.status = SNDDEC_STATUS_RESUMING;
}

void wav_pause(wav_stream_hnd_t hnd) {
    if(stream/* s[hnd] */.status == SNDDEC_STATUS_READY ||
       stream/* s[hnd] */.status == SNDDEC_STATUS_PAUSING)
       return;
       
    stream/* s[hnd] */.status = SNDDEC_STATUS_PAUSING;
}

void wav_stop(wav_stream_hnd_t hnd) {
    if(stream/* s[hnd] */.status == SNDDEC_STATUS_READY ||
       stream/* s[hnd] */.status == SNDDEC_STATUS_STOPPING)
       return;
       
    stream/* s[hnd] */.status = SNDDEC_STATUS_STOPPING;
}

void wav_volume(wav_stream_hnd_t hnd, int vol) {
    if(stream/* s[hnd] */.shnd == SND_STREAM_INVALID)
        return;

    if(vol > 255)
        vol = 255;

    if(vol < 0)
        vol = 0;

    stream/* s[hnd] */.vol = vol;
    snd_stream_volume(stream/* s[hnd] */.shnd, stream/* s[hnd] */.vol);
}

int wav_is_playing(wav_stream_hnd_t hnd) {
    return stream/* s[hnd] */.status == SNDDEC_STATUS_STREAMING;
}

void wav_add_filter(wav_stream_hnd_t hnd, wav_filter filter, void *obj) {
    snd_stream_filter_add(stream/* s[hnd] */.shnd, filter, obj);
}

void wav_remove_filter(wav_stream_hnd_t hnd, wav_filter filter, void *obj) {
    snd_stream_filter_remove(stream/* s[hnd] */.shnd, filter, obj);
}

static void *sndwav_thread(void *param) {
    (void)param;

    while(sndwav_status != SNDDRV_STATUS_DONE) {

        mutex_lock(&stream_mutex);

 //       for(i = 0; i < SND_STREAM_MAX; i++) {
            switch(stream/* s[i] */.status) {
                case SNDDEC_STATUS_RESUMING:
                    snd_stream_start_adpcm(stream/* s[i] */.shnd, stream/* s[i] */.sample_rate, stream/* s[i] */.channels - 1);
                    stream/* s[i] */.status = SNDDEC_STATUS_STREAMING;
                    break;
                case SNDDEC_STATUS_PAUSING:
                    snd_stream_stop(stream/* s[i] */.shnd);
                    stream/* s[i] */.status = SNDDEC_STATUS_READY;
                    break;
                case SNDDEC_STATUS_STOPPING:
                    snd_stream_stop(stream/* s[i] */.shnd);
                    if(stream/* s[i] */.wave_file != FILEHND_INVALID)
                        fs_seek(stream/* s[i] */.wave_file, stream/* s[i] */.data_offset, SEEK_SET);
                    else
                        stream/* s[i] */.buf_offset = stream/* s[i] */.data_offset;
                    
                    stream/* s[i] */.status = SNDDEC_STATUS_READY;
                    break;
                case SNDDEC_STATUS_STREAMING:
                    snd_stream_poll(stream/* s[i] */.shnd);
                    break;
                case SNDDEC_STATUS_READY:
                default:
                    break;
 //           }
        }

        mutex_unlock(&stream_mutex);

        thd_sleep(50);
    }

    return NULL;
}

static void *wav_file_callback(snd_stream_hnd_t hnd, int req, int* done) {
    int read = fs_read(stream/* s[hnd] */.wave_file, stream/* s[hnd] */.drv_buf, req);

    if(read != req) {
        fs_seek(stream/* s[hnd] */.wave_file, stream/* s[hnd] */.data_offset, SEEK_SET);
        if(stream/* s[hnd] */.loop) {
            fs_read(stream/* s[hnd] */.wave_file, stream/* s[hnd] */.drv_buf, req);
        }
        else {
            snd_stream_stop(stream/* s[hnd] */.shnd);
            stream/* s[hnd] */.status = SNDDEC_STATUS_READY;
            return NULL;
        }
    }

    *done = req;

    return stream/* s[hnd] */.drv_buf;
}
#if 0
static void *wav_buf_callback(snd_stream_hnd_t hnd, int req, int* done) {
    if((streams[hnd].data_length-(streams[hnd].buf_offset - streams[hnd].data_offset)) >= req)
        memcpy(streams[hnd].drv_buf, streams[hnd].wave_buf+streams[hnd].buf_offset, req);
    else {
        streams[hnd].buf_offset = streams[hnd].data_offset;
        if(streams[hnd].loop) {
            memcpy(streams[hnd].drv_buf, streams[hnd].wave_buf+streams[hnd].buf_offset, req);
        }
        else {
            snd_stream_stop(streams[hnd].shnd);
            streams[hnd].status = SNDDEC_STATUS_READY;
            return NULL;
        }
    }

    streams[hnd].buf_offset += *done = req;

    return streams[hnd].drv_buf;
}
#endif
