
/* m_fixed.c -- fixed point implementation */

#include "i_main.h"
#include "doomdef.h"
#include "p_spec.h"
#include "r_local.h"

fixed_t D_abs(fixed_t x)
{
	fixed_t _s = x >> 31;
	return (x ^ _s) - _s;
}

/*
===============
=
= FixedDiv
=
===============
*/

fixed_t FixedDiv(fixed_t a, fixed_t b)
{
	fixed_t aa, bb;
	unsigned c;
	int sign;

	sign = a^b;

	if (a < 0)
		aa = -a;
	else
		aa = a;

	if (b < 0)
		bb = -b;
	else
		bb = b;

	if ((unsigned)(aa >> 14) >= bb) {
		if (sign < 0)
			c = MININT;
		else
			c = MAXINT;
	} else {
		c = (fixed_t) FixedDiv2(a, b);
	}

	return c;
}

/*
===============
=
= FixedDiv2
=
===============
*/

fixed_t FixedDiv2(register fixed_t a, register fixed_t b)
{
	s64 result = ((s64)a << 16) / (s64)b;

	return (fixed_t)result;
}

fixed_t FixedDivFloat(register fixed_t a, register fixed_t b)
{
	fixed_t aa, bb;
	unsigned c;
	int sign;

	sign = a^b;

	if (a < 0)
		aa = -a;
	else
		aa = a;

	if (b < 0)
		bb = -b;
	else
		bb = b;

	if ((unsigned)(aa >> 14) >= bb) {
		if (sign < 0)
			c = MININT;
		else
			c = MAXINT;
	} else {
		float af = (float)a;
		float bf = (float)b;
		float cf = af / bf;
		c = (fixed_t)(cf * 65536.0f);
	}

	return c;
}

/*
===============
=
= FixedMul
=
===============
*/

fixed_t FixedMul(fixed_t a, fixed_t b)
{
	s64 result = ((s64)a * (s64)b) >> 16;
	return (fixed_t)result;
}
