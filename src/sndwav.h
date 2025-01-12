#ifndef SNDWAV_H
#define SNDWAV_H

#include <sys/cdefs.h>
__BEGIN_DECLS

#include <kos/fs.h>

typedef int wav_stream_hnd_t;

int wav_init(void);
void wav_shutdown(void);
void wav_destroy(void);

wav_stream_hnd_t wav_create(const char *filename, int loop);

void wav_play(void);
void wav_pause(void);
void wav_stop(void);
void wav_volume(int vol);
int wav_is_playing(void);

__END_DECLS

#endif
