/* c_convert.c  */

/*-----------------------------------*/
/* Color Converter RGB2HSV & HSV2RGB */
/*-----------------------------------*/

#include "doomdef.h"

/*
===================
=
= LightGetHSV
= Set HSV values based on given RGB
=
===================
*/

int LightGetHSV(int r, int g, int b)
{
	int h_, s_, v_;
	int min;
	int max;
	float deltamin;
	float deltamax;
	float j;
	float x = 0;
	float xr;
	float xg;
	float xb;
	float sum = 0;

	max = MAXINT;

	if (r < max) {
		max = r;
	}
	if (g < max) {
		max = g;
	}
	if (b < max) {
		max = b;
	}

	min = MININT;

	if (r > min) {
		min = r;
	}
	if (g > min) {
		min = g;
	}
	if (b > min) {
		min = b;
	}

	deltamin = (float)min / 255.0f;
	deltamax = deltamin - ((float)max / 255.0f);

	if (deltamin == 0.0f) {
		j = 0.0f;
	} else {
		j = deltamax / deltamin;
	}

	if (j != 0.0f) {
		xr = (float)r / 255.0f;
		xg = (float)g / 255.0f;
		xb = (float)b / 255.0f;

		if (xr != deltamin) {
			if (xg != deltamin) {
				if (xb == deltamin) {
					sum = ((deltamin - xg) / deltamax +
					       4.0f) -
					      ((deltamin - xr) / deltamax);
				}
			} else {
				sum = ((deltamin - xr) / deltamax + 2.0f) -
				      ((deltamin - xb) / deltamax);
			}
		} else {
			sum = ((deltamin - xb) / deltamax) -
			      ((deltamin - xg) / deltamax);
		}

		x = (sum * 60.0f);

		if (x < 0.0f) {
			x += 360.0f;
		}

		while (x > 360.0f) {
			x -= 360.0f;
		}
	} else {
		j = 0.0f;
	}

	h_ = (int)((x / 360.0f) * 255.0f);

	s_ = (int)(j * 255.0f);

	v_ = (int)(deltamin * 255.0f);
	return (((h_ & 0xff) << 16) | ((s_ & 0xff) << 8) |
		(v_ & 0xff));
}

/*
===================
=
= LightGetRGB
= Set RGB values based on given HSV
=
===================
*/

int LightGetRGB(int h, int s, int v)
{
	int r, g, b;

	float x;
	float j;
	float i;
	float t;
	int table;
	float xr = 0;
	float xg = 0;
	float xb = 0;

	j = ((float)h / 255.0f) * 360.0f;

	if (360.0f <= j) {
		j = j - 360.0f;
	}

	x = (float)s / 255.0f;
	i = (float)v / 255.0f;

	if (x != 0.0f) {
		table = (int)(j / 60.0f);
		if (table < 6) {
			t = (float)j / 60.0f;
			switch (table) {
			case 0:
				xr = i;
				xg = ((1.0f - ((1.0f - (t - (float)table)) * x)) *
				      i);
				xb = ((1.0f - x) * i);
				break;
			case 1:
				xr = ((1.0f - (x * (t - (float)table))) * i);
				xg = i;
				xb = ((1.0f - x) * i);
				break;
			case 2:
				xr = ((1.0f - x) * i);
				xg = i;
				xb = ((1.0f - ((1.0f - (t - (float)table)) * x)) *
				      i);
				break;
			case 3:
				xr = ((1.0f - x) * i);
				xg = ((1.0f - (x * (t - (float)table))) * i);
				xb = i;
				break;
			case 4:
				xr = ((1.0f - ((1.0f - (t - (float)table)) * x)) *
				      i);
				xg = ((1.0f - x) * i);
				xb = i;
				break;
			case 5:
				xr = i;
				xg = ((1.0f - x) * i);
				xb = ((1.0f - (x * (t - (float)table))) * i);
				break;
			}
		}
	} else {
		xr = xg = xb = i;
	}

	r = (int)(xr * 255.0f);

	g = (int)(xg * 255.0f);

	b = (int)(xb * 255.0f);

	return (((r & 0xff) << 16) | ((g & 0xff) << 8) |
		(b & 0xff));
}