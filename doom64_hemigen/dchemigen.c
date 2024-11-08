#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>

#define HEMIDEBUG 0

/* TODO:
 * With this utility included, there are now three utilities
 * (the other two being "vqenc" and "kmgenc") using common texture
 * operations such as twiddling and loading of png and jpeg images.
 * Maybe it is time to break that stuff out into its own library
 * or create a megatool consisting of all the tree tools in one
 * executable.
 */
#include "get_image.h"


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <assert.h>

void split_pairs(const uint8_t* bytes, size_t length, uint8_t** pitch_bytes, uint8_t** yaw_bytes);
void gen_palette_and_indexes(int start_x, int start_y, int stride, const uint8_t* bytes, uint8_t* palette, uint8_t* indexes);
void block_encode(int start_x, int start_y, int stride, const uint8_t* r_bytes, const uint8_t* g_bytes, 
                  uint8_t* r_palette, uint8_t* r_indexes, uint8_t* g_palette, uint8_t* g_indexes);
void pack_block(const uint8_t* rpal, const uint8_t* ridxs, const uint8_t* gpal, const uint8_t* gidxs, uint8_t* result);
uint8_t* compress_file(const uint8_t* norm_bytes, int width, int height);

void encode_hemi_data(uint8_t *raw_xyz_data, int w, int h, uint8_t *raw_hemi_data, short *twidbuffer);

/* twiddling stuff copied from kmgenc.c */
#define TWIDTAB(x) ( (x&1)|((x&2)<<1)|((x&4)<<2)|((x&8)<<3)|((x&16)<<4)| \
	((x&32)<<5)|((x&64)<<6)|((x&128)<<7)|((x&256)<<8)|((x&512)<<9) )
#define TWIDOUT(x, y) ( TWIDTAB((y)) | (TWIDTAB((x)) << 1) )
#define MIN(a, b) ( (a)<(b)? (a):(b) )

#define ERROR(...) { fprintf(stderr, __VA_ARGS__); goto cleanup; }

static void printUsage(void)
{
	printf("dchemigen - Dreamcast hemisphere normal map generator v1.0\n");
	printf("Copyright (c) 2024 Jason Martin\n");
	printf("normal usage:\n\tdchemigen <infile.png/.jpg>\n");
	printf("Outputs:\n\tpolar-encoded normal map to <infile.raw>.\n");
	printf("extended usage:\n");
	printf("\tdchemigen <infile.png/.jpg> <outfile.raw> <polar.pnm> <vector.pnm> <compressed.comp>\n");
	printf("Outputs:\n\tpolar-encoded normal map to <outfile.raw>.\n");
	printf("\tpolar-encoded preview PNM to <polar.pnm>.\n");
	printf("\tpolar-vector roundtripped preview PNM to <vector.pnm>.\n");
	printf("\tBC5 compressed to <compressed.comp>.\n");

}

static int isPowerOfTwo(unsigned x)
{
	return x && !(x & (x-1));
}


int main(int argc, char **argv)
{
	uint8_t *raw_hemi_data = NULL;
	short *twidbuffer = NULL;

	FILE *fp = NULL;
	image_t img = {0};

	if (argc < 2) {
		printUsage();
		return 0;
	}

	if (get_image(argv[1], &img) < 0) {
		ERROR("Cannot open %s\n", argv[1]);
	}

	char *fn2;
	char *pfn;
	char *vfn;
	char *cfn;
	if (argc == 2) {
		int fn_len = strlen(argv[1]);
		fn2 = malloc(fn_len + 1);
		pfn = NULL;
		vfn = NULL;
		cfn = NULL;

		memset(fn2, 0, fn_len + 1);

		strcpy(fn2, argv[1]);

		int in_extension = 0;
		for(int i=0;i<fn_len;i++) {
			if (!in_extension) {
				if(fn2[i] == '.') {
					in_extension = 1;
				}
			} else {
				if(in_extension == 1) {
					fn2[i] = 'r';
					in_extension++;
				} else if (in_extension == 2) {
					fn2[i] = 'a';
					in_extension++;
				} else if (in_extension == 3) {
					fn2[i] = 'w';
					break;
				}
			}
		}

	} else {
		fn2 = argv[2];
		pfn = argv[3];
		vfn = argv[4];
		cfn = argv[5];
	}

	fp = fopen(fn2, "wb");
	if (NULL == fp) {
		ERROR("Cannot open file %s!\n", argv[4]);
	}

	raw_hemi_data = malloc(2 * img.w * img.h);
	if (NULL == raw_hemi_data) {
		ERROR("Cannot allocate memory for image data!\n");
	}

	twidbuffer = malloc(2 * img.w * img.h);
		if (NULL == twidbuffer) {
		ERROR("Cannot allocate memory for twiddle buffer!\n");
	}

	if (!isPowerOfTwo(img.w) || !isPowerOfTwo(img.h)) {
		ERROR("Image dimensions %ux%u are not a power of two!\n", img.w, img.h);
	}

	encode_hemi_data(img.data, img.w, img.h, raw_hemi_data, twidbuffer);

	if (fwrite(twidbuffer, 2 * img.w * img.h, 1, fp) != 1) {
		ERROR("Cannot write twiddle buffer!\n");
	}

	if (argc > 2) {
	uint8_t *compd_img = compress_file(raw_hemi_data, img.w, img.h);

	FILE *cfp = fopen(cfn, "wb");
	if (NULL == cfp) {
		ERROR("Cannot open file for compressed normal map.\n");
	}
	fwrite(compd_img, img.w*img.h, 1, cfp);
	fclose(cfp);

	FILE *pfp = fopen(pfn, "wb");
	if (NULL == pfp) {
		ERROR("Cannot open file for polar preview.\n");
	}
	fprintf(pfp, "P6\n%d %d\n255\n", img.w, img.h);
	uint8_t u8zero = 0;
	for (int i=0;i<img.w*img.h*2;i+=2) {
		fwrite(&raw_hemi_data[i], 1, 1, pfp);
		uint8_t u8el = raw_hemi_data[i+1];
		fwrite(/*&raw_hemi_data[i+1]*/&u8el, 1, 1, pfp);
		fwrite(&u8zero, 1, 1, pfp);
	}

	fclose(pfp);

	FILE *vfp = fopen(vfn, "wb");
	if (NULL == pfp) {
		ERROR("Cannot open file for vector preview.\n");
	}
	fprintf(vfp, "P6\n%d %d\n255\n", img.w, img.h);
	for (int i=0;i<img.w*img.h*2;i+=2) {
		double az = (((double)raw_hemi_data[i+0]) / 255.0) * (M_PI * 2.0);
		double el = (1.0 - ((double)raw_hemi_data[i+1] / 255.0)) * (M_PI / 2.0);

		double x = sin(el) * cos(az);
		double y = sin(el) * sin(az);
		double z = cos(el);

		x = (x + 1.0) / 2.0;
		y = -((y + 1.0) / 2.0);
		z = (z + 1.0) / 2.0;

		uint8_t ix = x * 255;
		uint8_t iy = y * 255;
		uint8_t iz = z * 255;
		fwrite(&ix, 1, 1, vfp);
		fwrite(&iy, 1, 1, vfp);
		fwrite(&iz, 1, 1, vfp);
	}

	fclose(vfp);
	}
cleanup:
	if (fp) fclose(fp);
	if (raw_hemi_data) free(raw_hemi_data);
	if (twidbuffer) free(twidbuffer);
	if (img.data) free(img.data);

	return 0;
}

#define rescale(x) ((((double)(x) / 255.0) * 2.0) - 1.0)
#define zrescale(x) ((double)(x) / 255.0)

void encode_hemi_data(uint8_t *raw_xyz_data, int w, int h, uint8_t *raw_hemi_data, short *twidbuffer)
{
	double x, y, z;
	double a, e;
	int outa, oute;
	double lenxy2;

	int dest = 0;
	int source = 1;
	int ih;
	int iw;
	for (ih = 0; ih < h; ih++) {
		for (iw = 0; iw < w; iw++, source += 4) {
			x = rescale(raw_xyz_data[source    ]);
			y = rescale(raw_xyz_data[source + 1]);
			z = rescale(raw_xyz_data[source + 2]);

			// flip y
			y = -y;

			lenxy2 = sqrt((x*x) + (y*y));

			// compute the azimuth angle
			a = atan2( y , x );

			// compute the elevation angle
			e = atan2( lenxy2 , z );

#if HEMIDEBUG
			printf("%f, %f, %f - %f, %f\n", x, y, z, a, e);
#endif
			// a 0 - 2PI
			// e 0 - PI/2

			while (a < 0.0) {
				a += (M_PI * 2.0);
			}

			if (e < 0.0) e = 0.0;
			if (e > (M_PI / 2.0)) e = M_PI / 2.0;

#if HEMIDEBUG
			printf("\tclamped: %f, %f, %f - %f, %f\n", x, y, z, a, e);
#endif

			outa = (int)(255 * (a / (M_PI * 2.0)));
			oute = (int)(255 * (1.0 - (e / (M_PI / 2.0))));

			raw_hemi_data[dest] = outa; dest += 1;
			raw_hemi_data[dest] = oute; dest += 1;
		}
	}

	short *sbuffer = (short*) raw_hemi_data;

	/* twiddle code based on code from kmgenc.c */
	int min = MIN(w, h);
	int mask = min-1;
	for (int y=0; y<h; y++) {
		int yout = y;
		for (int x=0; x<w; x++) {
			twidbuffer[TWIDOUT(x&mask, yout&mask) +
				(x/min + yout/min)*min*min] = sbuffer[y*w+x];
		}
	}
}


void split_pairs(const uint8_t* bytes, size_t length, uint8_t** pitch_bytes, uint8_t** yaw_bytes) {
    *pitch_bytes = (uint8_t*)malloc(length / 2);
    *yaw_bytes = (uint8_t*)malloc(length / 2);

    for (size_t i = 0, j = 0; i < length; i += 2, j++) {
        (*pitch_bytes)[j] = bytes[i];
        (*yaw_bytes)[j] = bytes[i + 1];
    }
}

void gen_palette_and_indexes(int start_x, int start_y, int stride, const uint8_t* bytes, uint8_t* palette, uint8_t* indexes) {
    uint8_t min_byte = 255, max_byte = 0;
    int idx = 0;
    float one_over_7 = 1.0f / 7.0f;

    // Find min and max bytes
    for (int y = start_y; y < start_y + 4; y++) {
        for (int x = start_x; x < start_x + 4; x++) {
            uint8_t byte = bytes[y * stride + x];
            if (byte < min_byte) min_byte = byte;
            if (byte > max_byte) max_byte = byte;
        }
    }

    palette[0] = min_byte;
    palette[1] = max_byte;

    // Generate indexes
    for (int y = start_y; y < start_y + 4; y++) {
        for (int x = start_x; x < start_x + 4; x++) {
            uint8_t tgt_color = bytes[y * stride + x];
            float best_dist = INFINITY;
            int best_idx = 0;

            for (int pidx = 0; pidx < 8; pidx++) {
                float a = (7 - pidx) * one_over_7;
                float b = pidx * one_over_7;
                float pal_color = palette[0] * a + palette[1] * b;
                float cur_dist = fabsf(pal_color - tgt_color);
                if (cur_dist < best_dist) {
                    best_dist = cur_dist;
                    best_idx = pidx;
                }
            }

            indexes[idx++] = best_idx;
        }
    }
}

void block_encode(int start_x, int start_y, int stride, const uint8_t* r_bytes, const uint8_t* g_bytes, 
                  uint8_t* r_palette, uint8_t* r_indexes, uint8_t* g_palette, uint8_t* g_indexes) {
    gen_palette_and_indexes(start_x, start_y, stride, r_bytes, r_palette, r_indexes);
    gen_palette_and_indexes(start_x, start_y, stride, g_bytes, g_palette, g_indexes);
}

void pack_block(const uint8_t* rpal, const uint8_t* ridxs, const uint8_t* gpal, const uint8_t* gidxs, uint8_t* result) {
    result[0] = rpal[0];
    result[1] = rpal[1];

    uint32_t r0s = ridxs[0] | (ridxs[1] << 3) | (ridxs[2] << 6) | (ridxs[3] << 9) |
                   (ridxs[4] << 12) | (ridxs[5] << 15) | (ridxs[6] << 18) | (ridxs[7] << 21) |
                   (ridxs[8] << 24) | (ridxs[9] << 27) | ((ridxs[10] & 0x3) << 30);

    uint16_t r1s = ((ridxs[10] & 0x4) >> 2) | (ridxs[11] << 1) | (ridxs[12] << 4) | (ridxs[13] << 7) |
                   (ridxs[14] << 10) | (ridxs[15] << 13);

    result[2] = r0s & 0xFF;
    result[3] = (r0s >> 8) & 0xFF;
    result[4] = (r0s >> 16) & 0xFF;
    result[5] = (r0s >> 24) & 0xFF;

    result[6] = r1s & 0xFF;
    result[7] = (r1s >> 8) & 0xFF;

    result[8] = gpal[0];
    result[9] = gpal[1];

    uint32_t g0s = gidxs[0] | (gidxs[1] << 3) | (gidxs[2] << 6) | (gidxs[3] << 9) |
                   (gidxs[4] << 12) | (gidxs[5] << 15) | (gidxs[6] << 18) | (gidxs[7] << 21) |
                   (gidxs[8] << 24) | (gidxs[9] << 27) | ((gidxs[10] & 0x3) << 30);

    uint16_t g1s = ((gidxs[10] & 0x4) >> 2) | (gidxs[11] << 1) | (gidxs[12] << 4) | (gidxs[13] << 7) |
                   (gidxs[14] << 10) | (gidxs[15] << 13);

    result[10] = g0s & 0xFF;
    result[11] = (g0s >> 8) & 0xFF;
    result[12] = (g0s >> 16) & 0xFF;
    result[13] = (g0s >> 24) & 0xFF;

    result[14] = g1s & 0xFF;
    result[15] = (g1s >> 8) & 0xFF;
}

uint8_t* compress_file(const uint8_t* norm_bytes, int width, int height) {
    size_t compressed_size;
    uint8_t *pitch_bytes, *yaw_bytes;
    split_pairs(norm_bytes, width * height * 2, &pitch_bytes, &yaw_bytes);

    int width_in_blocks = width / 4;
    int height_in_blocks = height / 4;

    compressed_size = width_in_blocks * height_in_blocks * 16;
    uint8_t* all_block_bytes = (uint8_t*)malloc(compressed_size);

    for (int y = 0; y < height_in_blocks; y++) {
        for (int x = 0; x < width_in_blocks; x++) {
            uint8_t r_palette[2], g_palette[2], r_indexes[16], g_indexes[16];
            block_encode(x * 4, y * 4, width, pitch_bytes, yaw_bytes, r_palette, r_indexes, g_palette, g_indexes);
            pack_block(r_palette, r_indexes, g_palette, g_indexes, all_block_bytes + (y * width_in_blocks + x) * 16);
        }
    }

    free(pitch_bytes);
    free(yaw_bytes);

    return all_block_bytes;
}
