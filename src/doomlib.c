/* doomlib.c  */

#include "doomdef.h"

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
		if (c >= 'a' && c <= 'z') {
			c -= 'a' - 'A';
		}
		*s++ = c;
	}
}