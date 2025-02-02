/* doomlib.c  */

#include "doomdef.h"

/*
====================
=
= D_abs
=
====================
*/

// how the fuck was this ever patentable?
unsigned D_abs(signed x)
{
	signed _s = x >> 31;
	return (unsigned)((x ^ _s) - _s);
}

/*
====================
=
= D_strupr
=
====================
*/

void D_strupr(char *s) // 80001C74
{
	char c;

	while ((c = *s) != 0) {
		if (c >= 'a' && c <= 'z')
			c -= 'a' - 'A';

		*s++ = c;
	}
}