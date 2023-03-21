/**************************************************************************/
/*  bentley_ottmann.cpp                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "bentley_ottmann.h"
#include "core/math/rect2.h"

static void bigint28_from_r128(R128_U64 out[5], R128 in) {
	out[0] = in.lo & 0xFFFFFFF;
	out[1] = (in.lo >> 28) & 0xFFFFFFF;
	out[2] = (in.lo >> 56) | ((in.hi << 8) & 0xFFFFFFF);
	out[3] = (in.hi >> 20) & 0xFFFFFFF;
	out[4] = in.hi >> 48;
}

static void bigint28_to_r128(R128 &out, R128_U64 in[5]) {
	out = R128(in[0] | (in[1] << 28) | (in[2] << 56), (in[2] >> 8) | (in[3] << 20) | (in[4] << 48));
}

static void bigint28_mul_10(R128_U64 out[10], R128_U64 in1[10], R128_U64 in2[5]) {
	out[0] = in1[0] * in2[0];
	out[1] = in1[1] * in2[0] + in1[0] * in2[1] + (out[0] >> 28);
	out[2] = in1[2] * in2[0] + in1[1] * in2[1] + in1[0] * in2[2] + (out[1] >> 28);
	out[3] = in1[3] * in2[0] + in1[2] * in2[1] + in1[1] * in2[2] + in1[0] * in2[3] + (out[2] >> 28);
	out[4] = in1[4] * in2[0] + in1[3] * in2[1] + in1[2] * in2[2] + in1[1] * in2[3] + in1[0] * in2[4] + (out[3] >> 28);
	out[5] = in1[5] * in2[0] + in1[4] * in2[1] + in1[3] * in2[2] + in1[2] * in2[3] + in1[1] * in2[4] + (out[4] >> 28);
	out[6] = in1[6] * in2[0] + in1[5] * in2[1] + in1[4] * in2[2] + in1[3] * in2[3] + in1[2] * in2[4] + (out[5] >> 28);
	out[7] = in1[7] * in2[0] + in1[6] * in2[1] + in1[5] * in2[2] + in1[4] * in2[3] + in1[3] * in2[4] + (out[6] >> 28);
	out[8] = in1[8] * in2[0] + in1[7] * in2[1] + in1[6] * in2[2] + in1[5] * in2[3] + in1[4] * in2[4] + (out[7] >> 28);
	out[9] = in1[9] * in2[0] + (out[8] >> 28);
	out[0] &= 0xFFFFFFF;
	out[1] &= 0xFFFFFFF;
	out[2] &= 0xFFFFFFF;
	out[3] &= 0xFFFFFFF;
	out[4] &= 0xFFFFFFF;
	out[5] &= 0xFFFFFFF;
	out[6] &= 0xFFFFFFF;
	out[7] &= 0xFFFFFFF;
	out[8] &= 0xFFFFFFF;
}

static void bigint28_mul(R128_U64 out[10], R128_U64 in1[5], R128_U64 in2[5]) {
	out[0] = in1[0] * in2[0];
	out[1] = in1[1] * in2[0] + in1[0] * in2[1] + (out[0] >> 28);
	out[2] = in1[2] * in2[0] + in1[1] * in2[1] + in1[0] * in2[2] + (out[1] >> 28);
	out[3] = in1[3] * in2[0] + in1[2] * in2[1] + in1[1] * in2[2] + in1[0] * in2[3] + (out[2] >> 28);
	out[4] = in1[4] * in2[0] + in1[3] * in2[1] + in1[2] * in2[2] + in1[1] * in2[3] + in1[0] * in2[4] + (out[3] >> 28);
	out[5] = in1[4] * in2[1] + in1[3] * in2[2] + in1[2] * in2[3] + in1[1] * in2[4] + (out[4] >> 28);
	out[6] = in1[4] * in2[2] + in1[3] * in2[3] + in1[2] * in2[4] + (out[5] >> 28);
	out[7] = in1[4] * in2[3] + in1[3] * in2[4] + (out[6] >> 28);
	out[8] = in1[4] * in2[4] + (out[7] >> 28);
	out[9] = (out[8] >> 28);
	out[0] &= 0xFFFFFFF;
	out[1] &= 0xFFFFFFF;
	out[2] &= 0xFFFFFFF;
	out[3] &= 0xFFFFFFF;
	out[4] &= 0xFFFFFFF;
	out[5] &= 0xFFFFFFF;
	out[6] &= 0xFFFFFFF;
	out[7] &= 0xFFFFFFF;
	out[8] &= 0xFFFFFFF;
}

static void bigint28_add_10(R128_U64 out[10], R128_U64 in[10]) {
	out[0] += in[0];
	out[1] += in[1] + (out[0] >> 28);
	out[2] += in[2] + (out[1] >> 28);
	out[3] += in[3] + (out[2] >> 28);
	out[4] += in[4] + (out[3] >> 28);
	out[5] += in[5] + (out[4] >> 28);
	out[6] += in[6] + (out[5] >> 28);
	out[7] += in[7] + (out[6] >> 28);
	out[8] += in[8] + (out[7] >> 28);
	out[9] += in[9] + (out[8] >> 28);
	out[0] &= 0xFFFFFFF;
	out[1] &= 0xFFFFFFF;
	out[2] &= 0xFFFFFFF;
	out[3] &= 0xFFFFFFF;
	out[4] &= 0xFFFFFFF;
	out[5] &= 0xFFFFFFF;
	out[6] &= 0xFFFFFFF;
	out[7] &= 0xFFFFFFF;
	out[8] &= 0xFFFFFFF;
}

static void bigint28_sub_10(R128_U64 out[10], R128_U64 in[10]) {
	out[0] -= in[0];
	out[1] -= in[1] + ((out[0] >> 28) & 1);
	out[2] -= in[2] + ((out[1] >> 28) & 1);
	out[3] -= in[3] + ((out[2] >> 28) & 1);
	out[4] -= in[4] + ((out[3] >> 28) & 1);
	out[5] -= in[5] + ((out[4] >> 28) & 1);
	out[6] -= in[6] + ((out[5] >> 28) & 1);
	out[7] -= in[7] + ((out[6] >> 28) & 1);
	out[8] -= in[8] + ((out[7] >> 28) & 1);
	out[9] -= in[9] + ((out[8] >> 28) & 1);
	out[0] &= 0xFFFFFFF;
	out[1] &= 0xFFFFFFF;
	out[2] &= 0xFFFFFFF;
	out[3] &= 0xFFFFFFF;
	out[4] &= 0xFFFFFFF;
	out[5] &= 0xFFFFFFF;
	out[6] &= 0xFFFFFFF;
	out[7] &= 0xFFFFFFF;
	out[8] &= 0xFFFFFFF;
}

static void bigint28_sub(R128_U64 out[10], R128_U64 in[5]) {
	out[0] -= in[0];
	out[1] -= in[1] + ((out[0] >> 28) & 1);
	out[2] -= in[2] + ((out[1] >> 28) & 1);
	out[3] -= in[3] + ((out[2] >> 28) & 1);
	out[4] -= in[4] + ((out[3] >> 28) & 1);
	out[5] -= ((out[4] >> 28) & 1);
	out[6] -= ((out[5] >> 28) & 1);
	out[7] -= ((out[6] >> 28) & 1);
	out[8] -= ((out[7] >> 28) & 1);
	out[9] -= ((out[8] >> 28) & 1);
	out[0] &= 0xFFFFFFF;
	out[1] &= 0xFFFFFFF;
	out[2] &= 0xFFFFFFF;
	out[3] &= 0xFFFFFFF;
	out[4] &= 0xFFFFFFF;
	out[5] &= 0xFFFFFFF;
	out[6] &= 0xFFFFFFF;
	out[7] &= 0xFFFFFFF;
	out[8] &= 0xFFFFFFF;
}

static void bigint28_shl1(R128_U64 out[5], R128_U64 in[5]) {
	out[0] = (in[0] << 1) & 0xFFFFFFF;
	out[1] = ((in[1] << 1) | (in[0] >> 27)) & 0xFFFFFFF;
	out[2] = ((in[2] << 1) | (in[1] >> 27)) & 0xFFFFFFF;
	out[3] = ((in[3] << 1) | (in[2] >> 27)) & 0xFFFFFFF;
	out[4] = ((in[4] << 1) | (in[3] >> 27)) & 0xFFFFFFF;
}

static void bigint28_shr(R128_U64 out[10], R128_U64 in[10], int amount) {
	if (amount >= 140) {
		if (amount >= 196) {
			if (amount >= 252) {
				out[0] = in[9] >> (amount - 252);
				out[1] = out[2] = out[3] = out[4] = out[5] = out[6] = out[7] = out[8] = out[9] = 0;
			} else if (amount >= 224) {
				out[0] = ((in[9] << (252 - amount)) & 0xFFFFFFF) | (in[8] >> (amount - 224));
				out[1] = in[9] >> (amount - 224);
				out[2] = out[3] = out[4] = out[5] = out[6] = out[7] = out[8] = out[9] = 0;
			} else {
				out[0] = ((in[8] << (224 - amount)) & 0xFFFFFFF) | (in[7] >> (amount - 196));
				out[1] = ((in[9] << (224 - amount)) & 0xFFFFFFF) | (in[8] >> (amount - 196));
				out[2] = in[9] >> (amount - 196);
				out[3] = out[4] = out[5] = out[6] = out[7] = out[8] = out[9] = 0;
			}
		} else if (amount >= 168) {
			out[0] = ((in[7] << (196 - amount)) & 0xFFFFFFF) | (in[6] >> (amount - 168));
			out[1] = ((in[8] << (196 - amount)) & 0xFFFFFFF) | (in[7] >> (amount - 168));
			out[2] = ((in[9] << (196 - amount)) & 0xFFFFFFF) | (in[8] >> (amount - 168));
			out[3] = in[9] >> (amount - 168);
			out[4] = out[5] = out[6] = out[7] = out[8] = out[9] = 0;
		} else {
			out[0] = ((in[6] << (168 - amount)) & 0xFFFFFFF) | (in[5] >> (amount - 140));
			out[1] = ((in[7] << (168 - amount)) & 0xFFFFFFF) | (in[6] >> (amount - 140));
			out[2] = ((in[8] << (168 - amount)) & 0xFFFFFFF) | (in[7] >> (amount - 140));
			out[3] = ((in[9] << (168 - amount)) & 0xFFFFFFF) | (in[8] >> (amount - 140));
			out[4] = in[9] >> (amount - 140);
			out[5] = out[6] = out[7] = out[8] = out[9] = 0;
		}
	} else if (amount >= 56) {
		if (amount >= 112) {
			out[0] = ((in[5] << (140 - amount)) & 0xFFFFFFF) | (in[4] >> (amount - 112));
			out[1] = ((in[6] << (140 - amount)) & 0xFFFFFFF) | (in[5] >> (amount - 112));
			out[2] = ((in[7] << (140 - amount)) & 0xFFFFFFF) | (in[6] >> (amount - 112));
			out[3] = ((in[8] << (140 - amount)) & 0xFFFFFFF) | (in[7] >> (amount - 112));
			out[4] = ((in[9] << (140 - amount)) & 0xFFFFFFF) | (in[8] >> (amount - 112));
			out[5] = in[9] >> (amount - 112);
			out[6] = out[7] = out[8] = out[9] = 0;
		} else if (amount >= 84) {
			out[0] = ((in[4] << (112 - amount)) & 0xFFFFFFF) | (in[3] >> (amount - 84));
			out[1] = ((in[5] << (112 - amount)) & 0xFFFFFFF) | (in[4] >> (amount - 84));
			out[2] = ((in[6] << (112 - amount)) & 0xFFFFFFF) | (in[5] >> (amount - 84));
			out[3] = ((in[7] << (112 - amount)) & 0xFFFFFFF) | (in[6] >> (amount - 84));
			out[4] = ((in[8] << (112 - amount)) & 0xFFFFFFF) | (in[7] >> (amount - 84));
			out[5] = ((in[9] << (112 - amount)) & 0xFFFFFFF) | (in[8] >> (amount - 84));
			out[6] = in[9] >> (amount - 84);
			out[7] = out[8] = out[9] = 0;
		} else {
			out[0] = ((in[3] << (84 - amount)) & 0xFFFFFFF) | (in[2] >> (amount - 56));
			out[1] = ((in[4] << (84 - amount)) & 0xFFFFFFF) | (in[3] >> (amount - 56));
			out[2] = ((in[5] << (84 - amount)) & 0xFFFFFFF) | (in[4] >> (amount - 56));
			out[3] = ((in[6] << (84 - amount)) & 0xFFFFFFF) | (in[5] >> (amount - 56));
			out[4] = ((in[7] << (84 - amount)) & 0xFFFFFFF) | (in[6] >> (amount - 56));
			out[5] = ((in[8] << (84 - amount)) & 0xFFFFFFF) | (in[7] >> (amount - 56));
			out[6] = ((in[9] << (84 - amount)) & 0xFFFFFFF) | (in[8] >> (amount - 56));
			out[7] = in[9] >> (amount - 56);
			out[8] = out[9] = 0;
		}
	} else if (amount >= 28) {
		out[0] = ((in[2] << (56 - amount)) & 0xFFFFFFF) | (in[1] >> (amount - 28));
		out[1] = ((in[3] << (56 - amount)) & 0xFFFFFFF) | (in[2] >> (amount - 28));
		out[2] = ((in[4] << (56 - amount)) & 0xFFFFFFF) | (in[3] >> (amount - 28));
		out[3] = ((in[5] << (56 - amount)) & 0xFFFFFFF) | (in[4] >> (amount - 28));
		out[4] = ((in[6] << (56 - amount)) & 0xFFFFFFF) | (in[5] >> (amount - 28));
		out[5] = ((in[7] << (56 - amount)) & 0xFFFFFFF) | (in[6] >> (amount - 28));
		out[6] = ((in[8] << (56 - amount)) & 0xFFFFFFF) | (in[7] >> (amount - 28));
		out[7] = ((in[9] << (56 - amount)) & 0xFFFFFFF) | (in[8] >> (amount - 28));
		out[8] = in[9] >> (amount - 28);
		out[9] = 0;
	} else {
		out[0] = ((in[1] << (28 - amount)) & 0xFFFFFFF) | (in[0] >> amount);
		out[1] = ((in[2] << (28 - amount)) & 0xFFFFFFF) | (in[1] >> amount);
		out[2] = ((in[3] << (28 - amount)) & 0xFFFFFFF) | (in[2] >> amount);
		out[3] = ((in[4] << (28 - amount)) & 0xFFFFFFF) | (in[3] >> amount);
		out[4] = ((in[5] << (28 - amount)) & 0xFFFFFFF) | (in[4] >> amount);
		out[5] = ((in[6] << (28 - amount)) & 0xFFFFFFF) | (in[5] >> amount);
		out[6] = ((in[7] << (28 - amount)) & 0xFFFFFFF) | (in[6] >> amount);
		out[7] = ((in[8] << (28 - amount)) & 0xFFFFFFF) | (in[7] >> amount);
		out[8] = ((in[9] << (28 - amount)) & 0xFFFFFFF) | (in[8] >> amount);
		out[9] = in[9] >> amount;
	}
}

static void bigint28_div_1(R128_U64 out_mul[10], R128_U64 &out_mod, R128_U64 in1[10], R128_U64 in2) {
	R128_U64 v = in1[9];
	out_mul[9] = v / in2;
	v = ((v % in2) << 28) | in1[8];
	out_mul[8] = v / in2;
	v = ((v % in2) << 28) | in1[7];
	out_mul[7] = v / in2;
	v = ((v % in2) << 28) | in1[6];
	out_mul[6] = v / in2;
	v = ((v % in2) << 28) | in1[5];
	out_mul[5] = v / in2;
	v = ((v % in2) << 28) | in1[4];
	out_mul[4] = v / in2;
	v = ((v % in2) << 28) | in1[3];
	out_mul[3] = v / in2;
	v = ((v % in2) << 28) | in1[2];
	out_mul[2] = v / in2;
	v = ((v % in2) << 28) | in1[1];
	out_mul[1] = v / in2;
	v = ((v % in2) << 28) | in1[0];
	out_mul[0] = v / in2;
	out_mod = (v % in2);
}

static int bigint28_clz(R128_U64 in) {
	static const int de_bruijn[64] = {
		63, 62, 61, 56, 60, 50, 55, 44,
		59, 38, 49, 35, 54, 29, 43, 23,
		58, 46, 37, 25, 48, 17, 34, 15,
		53, 32, 28, 9, 42, 13, 22, 6,
		0, 57, 51, 45, 39, 36, 30, 24,
		47, 26, 18, 16, 33, 10, 14, 7,
		1, 52, 40, 31, 27, 19, 11, 8,
		2, 41, 20, 12, 3, 21, 4, 5
	};
	in |= (in >> 1);
	in |= (in >> 2);
	in |= (in >> 4);
	in |= (in >> 8);
	in |= (in >> 16);
	in |= (in >> 32);
	in -= (in >> 1);
	// De Bruijn 64-bit sequence derived using Burrows-Wheeler transform
	return de_bruijn[(in * 0x0218A392CD3D5DBFULL) >> 58];
}

static bool bigint28_ge(R128_U64 in1[10], R128_U64 in2[5]) {
	if (in1[5] || in1[6] || in1[7] || in1[8] || in1[9]) {
		return true;
	}
	if (in1[4] > in2[4]) {
		return true;
	}
	if (in1[4] < in2[4]) {
		return false;
	}
	if (in1[3] > in2[3]) {
		return true;
	}
	if (in1[3] < in2[3]) {
		return false;
	}
	if (in1[2] > in2[2]) {
		return true;
	}
	if (in1[2] < in2[2]) {
		return false;
	}
	if (in1[1] > in2[1]) {
		return true;
	}
	if (in1[1] < in2[1]) {
		return false;
	}
	if (in1[0] >= in2[0]) {
		return true;
	}
	return false;
}

static void bigint28_div(R128_U64 out_mul[10], R128_U64 out_mod[5], R128_U64 in1[10], R128_U64 in2[5]) {
	R128_U64 div_msb = 0;
	int div_shift = 0;
	if (in2[4]) {
		int bit = bigint28_clz(in2[4]);
		div_msb = (in2[4] << (bit - 36)) | (in2[3] >> (64 - bit));
		div_shift = 148 - bit;
	} else if (in2[3]) {
		int bit = bigint28_clz(in2[3]);
		div_msb = (in2[3] << (bit - 36)) | (in2[2] >> (64 - bit));
		div_shift = 120 - bit;
	} else if (in2[2]) {
		int bit = bigint28_clz(in2[2]);
		div_msb = (in2[2] << (bit - 36)) | (in2[1] >> (64 - bit));
		div_shift = 92 - bit;
	} else if (in2[1]) {
		int bit = bigint28_clz(in2[1]);
		div_msb = (in2[1] << (bit - 36)) | (in2[0] >> (64 - bit));
		div_shift = 64 - bit;
	} else {
		bigint28_div_1(out_mul, out_mod[0], in1, in2[0]);
		out_mod[1] = out_mod[2] = out_mod[3] = out_mod[4];
		return;
	}
	div_msb++;
	R128_U64 mod[10];
	mod[0] = in1[0];
	mod[1] = in1[1];
	mod[2] = in1[2];
	mod[3] = in1[3];
	mod[4] = in1[4];
	mod[5] = in1[5];
	mod[6] = in1[6];
	mod[7] = in1[7];
	mod[8] = in1[8];
	mod[9] = in1[9];
	out_mul[0] = out_mul[1] = out_mul[2] = out_mul[3] = out_mul[4] = out_mul[5] = out_mul[6] = out_mul[7] = out_mul[8] = out_mul[9] = 0;
	R128_U64 in2_shl1[5];
	bigint28_shl1(in2_shl1, in2);
	while (bigint28_ge(mod, in2_shl1)) {
		R128_U64 tmp_mod;
		R128_U64 tmp_mul[10];
		bigint28_div_1(tmp_mul, tmp_mod, mod, div_msb);
		R128_U64 tmp_mul_shr[10];
		bigint28_shr(tmp_mul_shr, tmp_mul, div_shift);
		bigint28_add_10(out_mul, tmp_mul_shr);
		bigint28_mul_10(tmp_mul, tmp_mul_shr, in2);
		bigint28_sub_10(mod, tmp_mul);
	}
	if (bigint28_ge(mod, in2)) {
		bigint28_sub(mod, in2);
		R128_U64 tmp_add1[10] = { 1 };
		bigint28_add_10(out_mul, tmp_add1);
	}
	out_mod[0] = mod[0];
	out_mod[1] = mod[1];
	out_mod[2] = mod[2];
	out_mod[3] = mod[3];
	out_mod[4] = mod[4];
}

/**
 * Implements mul1 * mul2 / div in a way that does not introduce
 * rounding errors. Returns the result in the form ret + mod / div
 * where 0 <= mod < div
 **/
static void quotient_positive(R128 mul1, R128 mul2, R128 div, R128 &ret, R128 &mod) {
	DEV_ASSERT(div > R128_zero);
	if (mul1 == R128_zero || mul2 == R128_zero) {
		ret = R128_zero;
		mod = R128_zero;
		return;
	}
	if (mul1 == div) {
		ret = mul2;
		mod = R128_zero;
		return;
	}
	if (mul2 == div) {
		ret = mul1;
		mod = R128_zero;
		return;
	}
	R128_U64 a[5];
	R128_U64 b[5];
	R128_U64 c[10];
	R128_U64 d[5];
	bigint28_from_r128(a, mul1);
	bigint28_from_r128(b, mul2);
	bigint28_from_r128(d, div);
	bigint28_mul(c, a, b);
	R128_U64 r[10];
	R128_U64 m[5];
	bigint28_div(r, m, c, d);
	if (r[9] || r[8] || r[7] || r[6] || r[5] || r[4] > 0xFFFF) {
		// Result too big to be represented in 128 bits
		ret = R128_max;
		mod = R128_zero;
		return;
	}
	bigint28_to_r128(ret, r);
	bigint28_to_r128(mod, m);
}

static void quotient_positive_mul2(R128 mul1, R128 mul2, R128 div, R128 &ret, R128 &mod) {
	if (r128IsNeg(&mul1)) {
		quotient_positive(-mul1, mul2, div, ret, mod);
		ret = -ret;
		if (mod > R128_zero) {
			ret -= R128_smallest;
			mod = div - mod;
		}
		return;
	}
	quotient_positive(mul1, mul2, div, ret, mod);
}

static void quotient(R128 mul1, R128 mul2, R128 div, R128 &ret, R128 &mod) {
	if (r128IsNeg(&mul2)) {
		quotient_positive_mul2(-mul1, -mul2, div, ret, mod);
		return;
	}
	quotient_positive_mul2(mul1, mul2, div, ret, mod);
}

/**
 * Implements ((mul1 + mod1 / div) + (mul2 + mod2 / div) - (mul3 + mod3 / div)) / 2 where
 * 0 <= mod1 < div and 0 <= mod2 < div and 0 <= mod3 < div. The return value is in the
 * form ret + mod / div where 0 <= mod < div
 **/
static void quotient_mid(R128 mul1, R128 mod1, R128 mul2, R128 mod2, R128 mul3, R128 mod3, R128 div, R128 &ret, R128 &mod) {
	DEV_ASSERT(div > R128_zero);
	mul1 += mul2;
	mul1 -= mul3;
	if (mod1 >= div - mod2) {
		mod1 -= div - mod2;
		mul1 += R128_smallest;
	} else {
		mod1 += mod2;
	}
	if (mod1 < mod3) {
		mod1 += div - mod3;
		mul1 -= R128_smallest;
	} else {
		mod1 -= mod3;
	}
	if (mul1 & R128_smallest) {
		mul1 -= R128_smallest;
		mod1 += div;
	}
	r128Sar(&ret, &mul1, 1);
	r128Shr(&mod, &mod1, 1);
}

/**
 * Implements (mul1 + mod1 / div) + (mul2 + mod2 / div) where 0 <= mod1 < div and
 * 0 <= mod2 < div. The return value is in the form ret + mod / div where 0 <= mod < div
 **/
static void quotient_add(R128 mul1, R128 mod1, R128 mul2, R128 mod2, R128 div, R128 &ret, R128 &mod) {
	DEV_ASSERT(div > R128_zero);
	mul1 += mul2;
	if (mod1 >= div - mod2) {
		mod1 -= div - mod2;
		mul1 += R128_smallest;
	} else {
		mod1 += mod2;
	}
	ret = mul1;
	mod = mod1;
}

BentleyOttmann::BentleyOttmann(Vector<Vector2> p_edges, Vector<int> p_winding, bool p_winding_even_odd) {
	// The cost of an explicit nil node is lower than having a special nil value.
	// This also ensures that tree_nodes[0].element is 0 instead of a null pointer exception.
	TreeNode nil_node;
	tree_nodes.push_back(nil_node);
	edges_tree = tree_create();
	slices_tree = tree_create();

	ERR_FAIL_COND(p_edges.size() & 1);
	ERR_FAIL_COND((p_edges.size() >> 1) != p_winding.size());
	if (p_edges.size() < 1) {
		return;
	}
	Rect2 rect;
	rect.position = p_edges[0];
	for (int i = 1; i < p_edges.size(); i++) {
		rect.expand_to(p_edges[i]);
	}
	rect.grow_by(CMP_EPSILON2);
	for (int i = 0, j = 0; i < p_winding.size(); i++, j += 2) {
		if (!p_winding[i]) {
			// Zero-winding edges are used internally for concave shapes and holes.
			// Therefore, don't allow them as input.
			continue;
		}
		R128 start_x = (p_edges[j].x - rect.position.x) / rect.size.x;
		R128 start_y = (p_edges[j].y - rect.position.y) / rect.size.y;
		R128 end_x = (p_edges[j + 1].x - rect.position.x) / rect.size.x;
		R128 end_y = (p_edges[j + 1].y - rect.position.y) / rect.size.y;
		if (start_x < end_x) {
			add_edge(add_point(add_slice(start_x), start_y), add_point(add_slice(end_x), end_y), p_winding[i]);
		} else if (start_x > end_x) {
			add_edge(add_point(add_slice(end_x), end_y), add_point(add_slice(start_x), start_y), -p_winding[i]);
		} else if (start_y < end_y) {
			add_vertical_edge(add_slice(start_x), start_y, end_y);
		} else if (start_y > end_y) {
			add_vertical_edge(add_slice(start_x), end_y, start_y);
		}
	}

	LocalVector<uint32_t> triangles;
	uint32_t incoming_list = list_create();
	uint32_t outgoing_list = list_create();

	uint32_t slice_iter = tree_nodes[slices_tree].current.next;
	while (slice_iter != slices_tree) {
		uint32_t slice = tree_nodes[slice_iter].element;

		{
			// Remove edges ending at this slice
			uint32_t check_iter = list_nodes[slices[slice].check_list].next;
			while (check_iter != slices[slice].check_list) {
				DEV_ASSERT(edges[list_nodes[check_iter].element].x_next_check == slices[slice].x);
				uint32_t check_iter_next = list_nodes[check_iter].next;
				if (points[edges[list_nodes[check_iter].element].point_end].x == slices[slice].x) {
					uint32_t treenode_edge_prev = tree_nodes[edges[list_nodes[check_iter].element].treenode_edges].current.prev;
					if (treenode_edge_prev != edges_tree) {
						edges[tree_nodes[treenode_edge_prev].element].x_next_check = slices[slice].x;
						list_insert(edges[tree_nodes[treenode_edge_prev].element].listnode_check, slices[slice].check_list);
					}
					list_insert(edges[list_nodes[check_iter].element].listnode_incoming, incoming_list);
					tree_remove(edges[list_nodes[check_iter].element].treenode_edges, slices[slice].x);
					list_remove(check_iter);
				}
				check_iter = check_iter_next;
			}
		}

		{
			// Mark intersection of passthrough edges with vertical edges
			uint32_t vertical_iter = tree_nodes[slices[slice].vertical_tree].current.next;
			while (vertical_iter != slices[slice].vertical_tree) {
				DEV_ASSERT(verticals[tree_nodes[vertical_iter].element].is_start);
				uint32_t treenode_edge = get_edge_before(slices[slice].x, verticals[tree_nodes[vertical_iter].element].y);
				vertical_iter = tree_nodes[vertical_iter].current.next;
				DEV_ASSERT(vertical_iter != slices[slice].vertical_tree);
				DEV_ASSERT(!verticals[tree_nodes[vertical_iter].element].is_start);
				while (tree_nodes[treenode_edge].current.next != edges_tree) {
					treenode_edge = tree_nodes[treenode_edge].current.next;
					edge_calculate_y(tree_nodes[treenode_edge].element, slices[slice].x);
					if (edges[tree_nodes[treenode_edge].element].y > verticals[tree_nodes[vertical_iter].element].y) {
						break;
					}
					add_point(slice, edges[tree_nodes[treenode_edge].element].y);
					list_insert(edges[tree_nodes[treenode_edge].element].listnode_incoming, incoming_list);
					list_insert(edges[tree_nodes[treenode_edge].element].listnode_outgoing, outgoing_list);
				}
				vertical_iter = tree_nodes[vertical_iter].current.next;
			}
		}

		{
			// Add edges starting at this slice
			uint32_t check_iter = list_nodes[slices[slice].check_list].next;
			while (check_iter != slices[slice].check_list) {
				DEV_ASSERT(edges[list_nodes[check_iter].element].x_next_check == slices[slice].x);
				if (points[edges[list_nodes[check_iter].element].point_start].x == slices[slice].x) {
					uint32_t treenode_edge = get_edge_before_end(slices[slice].x, points[edges[list_nodes[check_iter].element].point_start].y, points[edges[list_nodes[check_iter].element].point_end].x, points[edges[list_nodes[check_iter].element].point_end].y);
					list_insert(edges[list_nodes[check_iter].element].listnode_outgoing, outgoing_list);
					tree_insert(edges[list_nodes[check_iter].element].treenode_edges, treenode_edge, slices[slice].x);
					if (treenode_edge != edges_tree) {
						edges[tree_nodes[treenode_edge].element].x_next_check = slices[slice].x;
						list_insert(edges[tree_nodes[treenode_edge].element].listnode_check, slices[slice].check_list);
					}
				}
				check_iter = list_nodes[check_iter].next;
			}
		}

		{
			// Check order changes of edges, and mark as intersections
			while (list_nodes[slices[slice].check_list].next != slices[slice].check_list) {
				uint32_t edge = list_nodes[list_nodes[slices[slice].check_list].next].element;
				DEV_ASSERT(edges[edge].x_next_check == slices[slice].x);
				DEV_ASSERT(points[edges[edge].point_end].x > slices[slice].x);
				// Reset the next check of the checked edge to its end point.
				// This will be reduced to the nearest intersection if one is found.
				edges[edge].x_next_check = points[edges[edge].point_end].x;
				list_insert(edges[edge].listnode_check, slices[points[edges[edge].point_end].slice].check_list);
				uint32_t treenode_edge_next = tree_nodes[edges[edge].treenode_edges].current.next;
				if (treenode_edge_next == edges_tree) {
					continue;
				}
				uint32_t edge_next = tree_nodes[treenode_edge_next].element;
				edge_calculate_y(edge, slices[slice].x);
				edge_calculate_y(edge_next, slices[slice].x);
				DEV_ASSERT(edges[edge].y <= edges[edge_next].y);
				if (edges[edge].y_next <= edges[edge_next].y_next) {
					continue;
				}
				if (edges[edge].y >= edges[edge].y_next) {
					add_point(slice, edges[edge].y);
				} else if (edges[edge_next].y <= edges[edge_next].y_next) {
					add_point(slice, edges[edge_next].y);
				} else {
					R128 min_y = (edges[edge].y > edges[edge_next].y_next) ? edges[edge].y : edges[edge_next].y_next;
					R128 max_y = (edges[edge].y_next < edges[edge_next].y) ? edges[edge].y_next : edges[edge_next].y;
					add_point(slice, (min_y + max_y) >> 1);
				}
				if (tree_nodes[edges[edge].treenode_edges].self_value == 0) {
					tree_remove(edges[edge].treenode_edges, slices[slice].x);
					list_insert(edges[edge].listnode_incoming, incoming_list);
					list_insert(edges[edge_next].listnode_incoming, incoming_list);
					list_insert(edges[edge_next].listnode_outgoing, outgoing_list);
					list_remove(edges[edge].listnode_check);
					uint32_t treenode_edge_prev = tree_nodes[treenode_edge_next].current.prev;
					if (treenode_edge_prev != edges_tree) {
						edges[tree_nodes[treenode_edge_prev].element].x_next_check = slices[slice].x;
						list_insert(edges[tree_nodes[treenode_edge_prev].element].listnode_check, slices[slice].check_list);
					}
				} else if (tree_nodes[treenode_edge_next].self_value == 0) {
					tree_remove(treenode_edge_next, slices[slice].x);
					list_insert(edges[edge].listnode_incoming, incoming_list);
					list_insert(edges[edge_next].listnode_incoming, incoming_list);
					list_insert(edges[edge].listnode_outgoing, outgoing_list);
					list_remove(edges[edge_next].listnode_check);
					edges[edge].x_next_check = slices[slice].x;
					list_insert(edges[edge].listnode_check, slices[slice].check_list);
				} else {
					tree_swap(edges[edge].treenode_edges, treenode_edge_next, slices[slice].x);
					list_insert(edges[edge].listnode_incoming, incoming_list);
					list_insert(edges[edge_next].listnode_incoming, incoming_list);
					list_insert(edges[edge].listnode_outgoing, outgoing_list);
					list_insert(edges[edge_next].listnode_outgoing, outgoing_list);
					edges[edge].x_next_check = slices[slice].x;
					list_insert(edges[edge].listnode_check, slices[slice].check_list);
					uint32_t treenode_edge_prev = tree_nodes[treenode_edge_next].current.prev;
					if (treenode_edge_prev != edges_tree) {
						edges[tree_nodes[treenode_edge_prev].element].x_next_check = slices[slice].x;
						list_insert(edges[tree_nodes[treenode_edge_prev].element].listnode_check, slices[slice].check_list);
					}
				}
			}
		}

		{
			// Force edges going through a point to treat it as intersection
			uint32_t point_iter = tree_nodes[slices[slice].points_tree].current.next;
			while (point_iter != slices[slice].points_tree) {
				uint32_t point = tree_nodes[point_iter].element;
				// Edges are currently sorted by y_next. To get their sorting by y, we need to
				// use the previous tree
				uint32_t treenode_edge = get_edge_before_previous(slices[slice].x, points[point].y);
				if (tree_nodes[treenode_edge].version == slices[slice].x) {
					treenode_edge = tree_nodes[treenode_edge].previous.next;
				} else {
					treenode_edge = tree_nodes[treenode_edge].current.next;
				}
				while (treenode_edge != edges_tree) {
					edge_calculate_y(tree_nodes[treenode_edge].element, slices[slice].x);
					DEV_ASSERT(edges[tree_nodes[treenode_edge].element].y >= points[point].y);
					if (edges[tree_nodes[treenode_edge].element].y > points[point].y) {
						break;
					}
					list_insert(edges[tree_nodes[treenode_edge].element].listnode_incoming, incoming_list);
					if (tree_nodes[treenode_edge].current.parent != 0) {
						// If the edge wasn't removed this slice, add outgoing too
						list_insert(edges[tree_nodes[treenode_edge].element].listnode_outgoing, outgoing_list);
					}
					if (tree_nodes[treenode_edge].version == slices[slice].x) {
						treenode_edge = tree_nodes[treenode_edge].previous.next;
					} else {
						treenode_edge = tree_nodes[treenode_edge].current.next;
					}
				}
				point_iter = tree_nodes[point_iter].current.next;
			}
		}

		{
			// Add incoming edges to points
			while (list_nodes[incoming_list].next != incoming_list) {
				uint32_t edge = list_nodes[list_nodes[incoming_list].next].element;
				list_remove(list_nodes[incoming_list].next);
				tree_index_previous(edges[edge].treenode_edges, slices[slice].x);
				edge_calculate_y(edge, slices[slice].x);
				uint32_t treenode_point = get_point_or_before(slice, edges[edge].y);
				DEV_ASSERT(treenode_point != slices[slice].points_tree || edges[edge].y < edges[edge].y_next);
				if (treenode_point == slices[slice].points_tree || (edges[edge].y < edges[edge].y_next && points[tree_nodes[treenode_point].element].y < edges[edge].y)) {
					treenode_point = tree_nodes[treenode_point].current.next;
				}
				DEV_ASSERT(treenode_point != slices[slice].points_tree);
				DEV_ASSERT((edges[edge].y <= points[tree_nodes[treenode_point].element].y || points[tree_nodes[treenode_point].element].y <= edges[edge].y_next) || (edges[edge].y_next <= points[tree_nodes[treenode_point].element].y || points[tree_nodes[treenode_point].element].y <= edges[edge].y));
				tree_insert(edges[edge].treenode_incoming, point_get_incoming_before(tree_nodes[treenode_point].element, tree_nodes[edges[edge].treenode_edges].index));
			}
		}

		{
			// Produce triangles
			int winding = 0;
			uint32_t treenode_edge_previous = edges_tree;
			uint32_t point_previous = 0;
			uint32_t point_iter = tree_nodes[slices[slice].points_tree].current.next;
			while (point_iter != slices[slice].points_tree) {
				uint32_t point = tree_nodes[point_iter].element;
				uint32_t treenode_edge_before;
				if (tree_nodes[points[point].incoming_tree].current.next != points[point].incoming_tree) {
					uint32_t treenode_edge_first = edges[tree_nodes[tree_nodes[points[point].incoming_tree].current.next].element].treenode_edges;
					if (tree_nodes[treenode_edge_first].version == slices[slice].x) {
						treenode_edge_before = tree_nodes[treenode_edge_first].previous.prev;
					} else {
						treenode_edge_before = tree_nodes[treenode_edge_first].current.prev;
					}
				} else {
					treenode_edge_before = get_edge_before_previous(slices[slice].x, points[point].y);
				}
				if (treenode_edge_before == treenode_edge_previous) {
					if (winding != 0 && (!p_winding_even_odd || (winding & 1))) {
						DEV_ASSERT(treenode_edge_previous != edges_tree);
						triangles.push_back(point_previous);
						triangles.push_back(point);
						if (tree_nodes[treenode_edge_previous].version == slices[slice].x) {
							DEV_ASSERT(tree_nodes[treenode_edge_previous].previous.next != edges_tree);
							triangles.push_back(edges[tree_nodes[tree_nodes[treenode_edge_previous].previous.next].element].point_outgoing);
						} else {
							DEV_ASSERT(tree_nodes[treenode_edge_previous].current.next != edges_tree);
							triangles.push_back(edges[tree_nodes[tree_nodes[treenode_edge_previous].current.next].element].point_outgoing);
						}
					}
				} else {
					treenode_edge_previous = treenode_edge_before;
					winding = edge_get_winding_previous(treenode_edge_previous, slices[slice].x);
					if (winding != 0 && (!p_winding_even_odd || (winding & 1))) {
						DEV_ASSERT(treenode_edge_previous != edges_tree);
						triangles.push_back(edges[tree_nodes[treenode_edge_previous].element].point_outgoing);
						triangles.push_back(point);
						if (tree_nodes[treenode_edge_previous].version == slices[slice].x) {
							DEV_ASSERT(tree_nodes[treenode_edge_previous].previous.next != edges_tree);
							triangles.push_back(edges[tree_nodes[tree_nodes[treenode_edge_previous].previous.next].element].point_outgoing);
						} else {
							DEV_ASSERT(tree_nodes[treenode_edge_previous].current.next != edges_tree);
							triangles.push_back(edges[tree_nodes[tree_nodes[treenode_edge_previous].current.next].element].point_outgoing);
						}
					}
				}
				uint32_t edge_incoming_iter = tree_nodes[points[point].incoming_tree].current.next;
				while (edge_incoming_iter != points[point].incoming_tree) {
					DEV_ASSERT(edges[tree_nodes[edge_incoming_iter].element].treenode_edges == (tree_nodes[treenode_edge_previous].version == slices[slice].x ? tree_nodes[treenode_edge_previous].previous.next : tree_nodes[treenode_edge_previous].current.next));
					treenode_edge_previous = edges[tree_nodes[edge_incoming_iter].element].treenode_edges;
					winding += tree_nodes[treenode_edge_previous].self_value;
					if (winding != 0 && (!p_winding_even_odd || (winding & 1))) {
						DEV_ASSERT(treenode_edge_previous != edges_tree);
						triangles.push_back(edges[tree_nodes[treenode_edge_previous].element].point_outgoing);
						triangles.push_back(point);
						if (tree_nodes[treenode_edge_previous].version == slices[slice].x) {
							DEV_ASSERT(tree_nodes[treenode_edge_previous].previous.next != edges_tree);
							triangles.push_back(edges[tree_nodes[tree_nodes[treenode_edge_previous].previous.next].element].point_outgoing);
						} else {
							DEV_ASSERT(tree_nodes[treenode_edge_previous].current.next != edges_tree);
							triangles.push_back(edges[tree_nodes[tree_nodes[treenode_edge_previous].current.next].element].point_outgoing);
						}
					}
					edge_incoming_iter = tree_nodes[edge_incoming_iter].current.next;
				}
				point_previous = point;
				point_iter = tree_nodes[point_iter].current.next;
			}
		}

		{
			// Add outgoing edges to points
			while (list_nodes[outgoing_list].next != outgoing_list) {
				uint32_t edge = list_nodes[list_nodes[outgoing_list].next].element;
				list_remove(list_nodes[outgoing_list].next);
				tree_index(edges[edge].treenode_edges);
				edge_calculate_y(edge, slices[slice].x);
				uint32_t treenode_point = get_point_or_before(slice, edges[edge].y_next);
				DEV_ASSERT(treenode_point != slices[slice].points_tree || edges[edge].y > edges[edge].y_next);
				if (treenode_point == slices[slice].points_tree || (edges[edge].y > edges[edge].y_next && points[tree_nodes[treenode_point].element].y < edges[edge].y_next)) {
					treenode_point = tree_nodes[treenode_point].current.next;
				}
				DEV_ASSERT(treenode_point != slices[slice].points_tree);
				DEV_ASSERT((edges[edge].y <= points[tree_nodes[treenode_point].element].y || points[tree_nodes[treenode_point].element].y <= edges[edge].y_next) || (edges[edge].y_next <= points[tree_nodes[treenode_point].element].y || points[tree_nodes[treenode_point].element].y <= edges[edge].y));
				tree_insert(edges[edge].treenode_outgoing, point_get_outgoing_before(tree_nodes[treenode_point].element, tree_nodes[edges[edge].treenode_edges].index));
				edges[edge].point_outgoing = tree_nodes[treenode_point].element;
			}
		}

		{
			// Add helper edges
			uint32_t point_iter = tree_nodes[slices[slice].points_tree].current.next;
			while (point_iter != slices[slice].points_tree) {
				uint32_t point = tree_nodes[point_iter].element;
				// Concave point or hole in the x direction
				// Has two connected points with equal or lower x. Add an edge
				// ensuring those points are not connected to each other.
				if (tree_nodes[points[point].outgoing_tree].current.next == points[point].outgoing_tree) {
					uint32_t treenode_edge_before = get_edge_before(slices[slice].x, points[point].y);
					if (treenode_edge_before != edges_tree && tree_nodes[treenode_edge_before].current.next != edges_tree) {
						DEV_ASSERT(list_nodes[slices[slice].check_list].next == slices[slice].check_list);
						if (points[edges[tree_nodes[treenode_edge_before].element].point_end].x < points[edges[tree_nodes[tree_nodes[treenode_edge_before].current.next].element].point_end].x) {
							add_edge(point, edges[tree_nodes[treenode_edge_before].element].point_end, 0);
						} else {
							add_edge(point, edges[tree_nodes[tree_nodes[treenode_edge_before].current.next].element].point_end, 0);
						}
						// Adding the edge at the current slice will cause it to be added to the check list.
						// Remove it, and add it to the point's outgoing edges.
						DEV_ASSERT(list_nodes[slices[slice].check_list].next != slices[slice].check_list);
						uint32_t edge = list_nodes[list_nodes[slices[slice].check_list].next].element;
						tree_insert(edges[edge].treenode_edges, treenode_edge_before, slices[slice].x);
						tree_insert(edges[edge].treenode_outgoing, points[point].outgoing_tree);
						edges[edge].x_next_check = points[edges[edge].point_end].x;
						list_insert(edges[edge].listnode_check, slices[points[edges[edge].point_end].slice].check_list);
						DEV_ASSERT(list_nodes[slices[slice].check_list].next == slices[slice].check_list);
					}
				}
				// Concave points in the y direction
				// A quad formed by the edges connected to this point and the next edges
				// above or below is concave. Add an edge to split it into triangles.
				if (tree_nodes[points[point].outgoing_tree].current.next != points[point].outgoing_tree) {
					{
						uint32_t edge_first = tree_nodes[tree_nodes[points[point].outgoing_tree].current.next].element;
						uint32_t treenode_edge_other = tree_nodes[edges[edge_first].treenode_edges].current.prev;
						if (treenode_edge_other != edges_tree) {
							uint32_t point_edge_end = edges[edge_first].point_end;
							uint32_t point_other_outgoing = edges[tree_nodes[treenode_edge_other].element].point_outgoing;
							R128 a_x = points[point].x - points[point_other_outgoing].x;
							R128 a_y = points[point].y - points[point_other_outgoing].y;
							R128 b_x = points[point_edge_end].x - points[point_other_outgoing].x;
							R128 b_y = points[point_edge_end].y - points[point_other_outgoing].y;
							if (a_x * b_y > a_y * b_x) {
								DEV_ASSERT(list_nodes[slices[slice].check_list].next == slices[slice].check_list);
								add_edge(point, edges[tree_nodes[treenode_edge_other].element].point_end, 0);
								DEV_ASSERT(list_nodes[slices[slice].check_list].next != slices[slice].check_list);
								uint32_t edge = list_nodes[list_nodes[slices[slice].check_list].next].element;
								tree_insert(edges[edge].treenode_edges, treenode_edge_other, slices[slice].x);
								tree_insert(edges[edge].treenode_outgoing, points[point].outgoing_tree);
								edges[edge].x_next_check = points[edges[edge].point_end].x;
								list_insert(edges[edge].listnode_check, slices[points[edges[edge].point_end].slice].check_list);
								DEV_ASSERT(list_nodes[slices[slice].check_list].next == slices[slice].check_list);
							}
						}
					}
					{
						uint32_t edge_last = tree_nodes[tree_nodes[points[point].outgoing_tree].current.prev].element;
						uint32_t treenode_edge_other = tree_nodes[edges[edge_last].treenode_edges].current.next;
						if (treenode_edge_other != edges_tree) {
							uint32_t point_edge_end = edges[edge_last].point_end;
							uint32_t point_other_outgoing = edges[tree_nodes[treenode_edge_other].element].point_outgoing;
							R128 a_x = points[point].x - points[point_other_outgoing].x;
							R128 a_y = points[point].y - points[point_other_outgoing].y;
							R128 b_x = points[point_edge_end].x - points[point_other_outgoing].x;
							R128 b_y = points[point_edge_end].y - points[point_other_outgoing].y;
							if (a_x * b_y < a_y * b_x) {
								DEV_ASSERT(list_nodes[slices[slice].check_list].next == slices[slice].check_list);
								add_edge(point, edges[tree_nodes[treenode_edge_other].element].point_end, 0);
								DEV_ASSERT(list_nodes[slices[slice].check_list].next != slices[slice].check_list);
								uint32_t edge = list_nodes[list_nodes[slices[slice].check_list].next].element;
								tree_insert(edges[edge].treenode_edges, edges[edge_last].treenode_edges, slices[slice].x);
								tree_insert(edges[edge].treenode_outgoing, edges[edge_last].treenode_outgoing);
								edges[edge].x_next_check = points[edges[edge].point_end].x;
								list_insert(edges[edge].listnode_check, slices[points[edges[edge].point_end].slice].check_list);
								DEV_ASSERT(list_nodes[slices[slice].check_list].next == slices[slice].check_list);
							}
						}
					}
				}
				point_iter = tree_nodes[point_iter].current.next;
			}
		}

		{
			// Check for possible next intersections
			uint32_t point_iter = tree_nodes[slices[slice].points_tree].current.next;
			while (point_iter != slices[slice].points_tree) {
				uint32_t point = tree_nodes[point_iter].element;
				if (tree_nodes[points[point].outgoing_tree].current.next != points[point].outgoing_tree) {
					{
						uint32_t treenode_edge = tree_nodes[edges[tree_nodes[tree_nodes[points[point].outgoing_tree].current.next].element].treenode_edges].current.prev;
						if (treenode_edge != edges_tree) {
							check_intersection(treenode_edge, slices[slice].x + R128_smallest);
						}
					}
					{
						uint32_t treenode_edge = edges[tree_nodes[tree_nodes[points[point].outgoing_tree].current.prev].element].treenode_edges;
						if (tree_nodes[treenode_edge].current.next != edges_tree) {
							check_intersection(treenode_edge, slices[slice].x + R128_smallest);
						}
					}
				}
				point_iter = tree_nodes[point_iter].current.next;
			}
		}

		{
			// Cleanup
			while (tree_nodes[slices[slice].points_tree].current.next != slices[slice].points_tree) {
				uint32_t point = tree_nodes[tree_nodes[slices[slice].points_tree].current.next].element;
				tree_remove(tree_nodes[slices[slice].points_tree].current.next);
				while (tree_nodes[points[point].incoming_tree].current.next != points[point].incoming_tree) {
					tree_remove(tree_nodes[points[point].incoming_tree].current.next);
				}
				while (tree_nodes[points[point].outgoing_tree].current.next != points[point].outgoing_tree) {
					tree_remove(tree_nodes[points[point].outgoing_tree].current.next);
				}
			}
		}

		DEV_ASSERT(list_nodes[incoming_list].next == incoming_list);
		DEV_ASSERT(list_nodes[outgoing_list].next == outgoing_list);

		slice_iter = tree_nodes[slice_iter].current.next;
	}

	DEV_ASSERT(tree_nodes[edges_tree].current.right == 0);

	// Optimize points and flush to final buffers
	for (uint32_t i = 0; i < points.size(); i++) {
		points[i].used = 0;
	}
	DEV_ASSERT((triangles.size() % 3) == 0);
	for (uint32_t i = 0; i < triangles.size();) {
		if (triangles[i] == triangles[i + 1] || triangles[i] == triangles[i + 2] || triangles[i + 1] == triangles[i + 2]) {
			i += 3;
			continue;
		}
		for (uint32_t j = 0; j < 3; i++, j++) {
			if (!points[triangles[i]].used) {
				out_points.push_back(Vector2(static_cast<double>(points[triangles[i]].x) * rect.size.x + rect.position.x, static_cast<double>(points[triangles[i]].y) * rect.size.y + rect.position.y));
				points[triangles[i]].used = out_points.size();
			}
			out_triangles.push_back(points[triangles[i]].used - 1);
		}
	}
}

uint32_t BentleyOttmann::add_slice(R128 p_x) {
	uint32_t insert_after = slices_tree;
	uint32_t current = tree_nodes[slices_tree].current.right;
	if (current) {
		while (true) {
			R128 x = slices[tree_nodes[current].element].x;
			if (p_x < x) {
				if (tree_nodes[current].current.left) {
					current = tree_nodes[current].current.left;
					continue;
				}
				insert_after = tree_nodes[current].current.prev;
				break;
			}
			if (p_x > x) {
				if (tree_nodes[current].current.right) {
					current = tree_nodes[current].current.right;
					continue;
				}
				insert_after = current;
				break;
			}
			return tree_nodes[current].element;
		}
	}
	Slice slice;
	slice.x = p_x;
	slice.points_tree = tree_create();
	slice.vertical_tree = tree_create();
	slice.check_list = list_create();
	tree_insert(tree_create(slices.size()), insert_after);
	slices.push_back(slice);
	return slices.size() - 1;
}

uint32_t BentleyOttmann::add_point(uint32_t p_slice, R128 p_y) {
	uint32_t insert_after = slices[p_slice].points_tree;
	uint32_t current = tree_nodes[slices[p_slice].points_tree].current.right;
	if (current) {
		while (true) {
			R128 y = points[tree_nodes[current].element].y;
			if (p_y < y) {
				if (tree_nodes[current].current.left) {
					current = tree_nodes[current].current.left;
					continue;
				}
				insert_after = tree_nodes[current].current.prev;
				break;
			}
			if (p_y > y) {
				if (tree_nodes[current].current.right) {
					current = tree_nodes[current].current.right;
					continue;
				}
				insert_after = current;
				break;
			}
			return tree_nodes[current].element;
		}
	}
	Point point;
	point.slice = p_slice;
	point.x = slices[p_slice].x;
	point.y = p_y;
	point.incoming_tree = tree_create();
	point.outgoing_tree = tree_create();
	tree_insert(tree_create(points.size()), insert_after);
	points.push_back(point);
	return points.size() - 1;
}

uint32_t BentleyOttmann::get_point_or_before(uint32_t p_slice, R128 p_y) {
	uint32_t current = tree_nodes[slices[p_slice].points_tree].current.right;
	if (!current) {
		return slices[p_slice].points_tree;
	}
	while (true) {
		R128 y = points[tree_nodes[current].element].y;
		if (p_y < y) {
			if (tree_nodes[current].current.left) {
				current = tree_nodes[current].current.left;
				continue;
			}
			return tree_nodes[current].current.prev;
		}
		if (p_y > y && tree_nodes[current].current.right) {
			current = tree_nodes[current].current.right;
			continue;
		}
		return current;
	}
}

uint32_t BentleyOttmann::point_get_incoming_before(uint32_t p_point, uint32_t p_index) {
	uint32_t current = tree_nodes[points[p_point].incoming_tree].current.right;
	if (!current) {
		return points[p_point].incoming_tree;
	}
	while (true) {
		uint32_t index = tree_nodes[edges[tree_nodes[current].element].treenode_edges].index;
		if (p_index > index) {
			if (tree_nodes[current].current.right) {
				current = tree_nodes[current].current.right;
				continue;
			}
			return current;
		}
		if (p_index < index && tree_nodes[current].current.left) {
			current = tree_nodes[current].current.left;
			continue;
		}
		return tree_nodes[current].current.prev;
	}
}

uint32_t BentleyOttmann::point_get_outgoing_before(uint32_t p_point, uint32_t p_index) {
	uint32_t current = tree_nodes[points[p_point].outgoing_tree].current.right;
	if (!current) {
		return points[p_point].outgoing_tree;
	}
	while (true) {
		uint32_t index = tree_nodes[edges[tree_nodes[current].element].treenode_edges].index;
		if (p_index > index) {
			if (tree_nodes[current].current.right) {
				current = tree_nodes[current].current.right;
				continue;
			}
			return current;
		}
		if (p_index < index && tree_nodes[current].current.left) {
			current = tree_nodes[current].current.left;
			continue;
		}
		return tree_nodes[current].current.prev;
	}
}

void BentleyOttmann::add_edge(uint32_t p_point_start, uint32_t p_point_end, int p_winding) {
	DEV_ASSERT(points[p_point_start].x < points[p_point_end].x);
	Edge edge;
	edge.point_start = edge.point_outgoing = p_point_start;
	edge.point_end = p_point_end;
	edge.treenode_edges = tree_create(edges.size(), p_winding);
	edge.treenode_incoming = tree_create(edges.size());
	edge.treenode_outgoing = tree_create(edges.size());
	edge.listnode_incoming = list_create(edges.size());
	edge.listnode_outgoing = list_create(edges.size());
	edge.listnode_check = list_create(edges.size());
	R128 start_x = points[p_point_start].x;
	R128 start_y = points[p_point_start].y;
	edge.dir_x = points[p_point_end].x - start_x;
	edge.dir_y = points[p_point_end].y - start_y;
	edge.x_next_check = start_x;
	edge.x_last_calculate = start_x;
	edge.y = start_y;
	quotient(R128_smallest, edge.dir_y, edge.dir_x, edge.step_y, edge.step_mod);
	quotient_add(start_y, R128_zero, edge.step_y, edge.step_mod, edge.dir_x, edge.y_next, R128());
	edges.push_back(edge);
	list_insert(edge.listnode_check, slices[points[p_point_start].slice].check_list);
}

void BentleyOttmann::add_vertical_edge(uint32_t p_slice, R128 p_y_start, R128 p_y_end) {
	uint32_t start;
	uint32_t current = tree_nodes[slices[p_slice].vertical_tree].current.right;
	if (!current) {
		Vertical vertical;
		vertical.y = p_y_start;
		vertical.is_start = true;
		start = tree_create(verticals.size());
		verticals.push_back(vertical);
		tree_insert(start, slices[p_slice].vertical_tree);
	} else {
		while (true) {
			R128 y = verticals[tree_nodes[current].element].y;
			if (p_y_start < y) {
				if (tree_nodes[current].current.left) {
					current = tree_nodes[current].current.left;
					continue;
				}
				if (verticals[tree_nodes[current].element].is_start) {
					Vertical vertical;
					vertical.y = p_y_start;
					vertical.is_start = true;
					start = tree_create(verticals.size());
					verticals.push_back(vertical);
					tree_insert(start, tree_nodes[current].current.prev);
				} else {
					start = tree_nodes[current].current.prev;
				}
				break;
			}
			if (p_y_start > y) {
				if (tree_nodes[current].current.right) {
					current = tree_nodes[current].current.right;
					continue;
				}
				if (!verticals[tree_nodes[current].element].is_start) {
					Vertical vertical;
					vertical.y = p_y_start;
					vertical.is_start = true;
					start = tree_create(verticals.size());
					verticals.push_back(vertical);
					tree_insert(start, current);
				} else {
					start = current;
				}
				break;
			}
			if (verticals[tree_nodes[current].element].is_start) {
				start = current;
			} else {
				start = tree_nodes[current].current.prev;
			}
			break;
		}
	}
	while (tree_nodes[start].current.next != slices[p_slice].vertical_tree && (verticals[tree_nodes[tree_nodes[start].current.next].element].y > p_y_end || (verticals[tree_nodes[tree_nodes[start].current.next].element].y == p_y_end && verticals[tree_nodes[tree_nodes[start].current.next].element].is_start))) {
		tree_remove(tree_nodes[start].current.next);
	}
	if (tree_nodes[start].current.next == slices[p_slice].vertical_tree || verticals[tree_nodes[tree_nodes[start].current.next].element].is_start) {
		Vertical vertical;
		vertical.y = p_y_end;
		vertical.is_start = false;
		tree_insert(tree_create(verticals.size()), start);
		verticals.push_back(vertical);
	}
}

void BentleyOttmann::edge_calculate_y(uint32_t p_edge, R128 p_x) {
	if (edges[p_edge].x_last_calculate == p_x) {
		return;
	}
	edges[p_edge].x_last_calculate = p_x;
	R128 start_x = points[edges[p_edge].point_start].x;
	R128 start_y = points[edges[p_edge].point_start].y;
	R128 mod;
	quotient(p_x - start_x, edges[p_edge].dir_y, edges[p_edge].dir_x, edges[p_edge].y, mod);
	edges[p_edge].y += start_y;
	quotient_add(edges[p_edge].y, mod, edges[p_edge].step_y, edges[p_edge].step_mod, edges[p_edge].dir_x, edges[p_edge].y_next, R128());
}

uint32_t BentleyOttmann::get_edge_before(R128 p_x, R128 p_y) {
	uint32_t current = tree_nodes[edges_tree].current.right;
	if (!current) {
		return edges_tree;
	}
	while (true) {
		edge_calculate_y(tree_nodes[current].element, p_x);
		R128 y = edges[tree_nodes[current].element].y;
		if (p_y > y) {
			if (tree_nodes[current].current.right) {
				current = tree_nodes[current].current.right;
				continue;
			}
			return current;
		}
		if (p_y < y && tree_nodes[current].current.left) {
			current = tree_nodes[current].current.left;
			continue;
		}
		return tree_nodes[current].current.prev;
	}
}

uint32_t BentleyOttmann::get_edge_before_end(R128 p_x, R128 p_y, R128 p_end_x, R128 p_end_y) {
	R128 a_x = p_end_x - p_x;
	R128 a_y = p_end_y - p_y;
	uint32_t current = tree_nodes[edges_tree].current.right;
	if (!current) {
		return edges_tree;
	}
	while (true) {
		edge_calculate_y(tree_nodes[current].element, p_x);
		R128 y = edges[tree_nodes[current].element].y;
		if (p_y > y) {
			if (tree_nodes[current].current.right) {
				current = tree_nodes[current].current.right;
				continue;
			}
			return current;
		}
		if (p_y < y) {
			if (tree_nodes[current].current.left) {
				current = tree_nodes[current].current.left;
				continue;
			}
			return tree_nodes[current].current.prev;
		}
		// This is a best-effort attempt, since edges are not guaranteed
		// to be sorted by end.
		R128 b_x = points[edges[tree_nodes[current].element].point_end].x - p_x;
		R128 b_y = points[edges[tree_nodes[current].element].point_end].y - p_y;
		if (b_x * a_y > b_y * a_x) {
			if (tree_nodes[current].current.right) {
				current = tree_nodes[current].current.right;
				continue;
			}
			return current;
		}
		if (b_x * a_y < b_y * a_x && tree_nodes[current].current.left) {
			current = tree_nodes[current].current.left;
			continue;
		}
		return tree_nodes[current].current.prev;
	}
}

uint32_t BentleyOttmann::get_edge_before_previous(R128 p_x, R128 p_y) {
	uint32_t current;
	if (tree_nodes[edges_tree].version == p_x) {
		current = tree_nodes[edges_tree].previous.right;
	} else {
		current = tree_nodes[edges_tree].current.right;
	}
	if (!current) {
		return edges_tree;
	}
	while (true) {
		edge_calculate_y(tree_nodes[current].element, p_x);
		R128 y = edges[tree_nodes[current].element].y;
		if (p_y > y) {
			if (tree_nodes[current].version == p_x) {
				if (tree_nodes[current].previous.right) {
					current = tree_nodes[current].previous.right;
					continue;
				}
			} else {
				if (tree_nodes[current].current.right) {
					current = tree_nodes[current].current.right;
					continue;
				}
			}
			return current;
		}
		if (tree_nodes[current].version == p_x) {
			if (p_y < y && tree_nodes[current].previous.left) {
				current = tree_nodes[current].previous.left;
				continue;
			}
			return tree_nodes[current].previous.prev;
		} else {
			if (p_y < y && tree_nodes[current].current.left) {
				current = tree_nodes[current].current.left;
				continue;
			}
			return tree_nodes[current].current.prev;
		}
	}
}

int BentleyOttmann::edge_get_winding_previous(uint32_t p_treenode_edge, R128 p_x) {
	int winding = tree_nodes[p_treenode_edge].self_value;
	uint32_t current = p_treenode_edge;
	uint32_t parent;
	if (tree_nodes[p_treenode_edge].version == p_x) {
		parent = tree_nodes[p_treenode_edge].previous.parent;
		if (tree_nodes[tree_nodes[p_treenode_edge].previous.left].version == p_x) {
			winding += tree_nodes[tree_nodes[p_treenode_edge].previous.left].previous.sum_value;
		} else {
			winding += tree_nodes[tree_nodes[p_treenode_edge].previous.left].current.sum_value;
		}
	} else {
		parent = tree_nodes[p_treenode_edge].current.parent;
		if (tree_nodes[tree_nodes[p_treenode_edge].current.left].version == p_x) {
			winding += tree_nodes[tree_nodes[p_treenode_edge].current.left].previous.sum_value;
		} else {
			winding += tree_nodes[tree_nodes[p_treenode_edge].current.left].current.sum_value;
		}
	}
	while (parent) {
		if (tree_nodes[parent].version == p_x) {
			if (tree_nodes[parent].previous.right == current) {
				if (tree_nodes[tree_nodes[parent].previous.left].version == p_x) {
					winding += tree_nodes[tree_nodes[parent].previous.left].previous.sum_value + tree_nodes[parent].self_value;
				} else {
					winding += tree_nodes[tree_nodes[parent].previous.left].current.sum_value + tree_nodes[parent].self_value;
				}
			}
			current = parent;
			parent = tree_nodes[current].previous.parent;
		} else {
			if (tree_nodes[parent].current.right == current) {
				if (tree_nodes[tree_nodes[parent].current.left].version == p_x) {
					winding += tree_nodes[tree_nodes[parent].current.left].previous.sum_value + tree_nodes[parent].self_value;
				} else {
					winding += tree_nodes[tree_nodes[parent].current.left].current.sum_value + tree_nodes[parent].self_value;
				}
			}
			current = parent;
			parent = tree_nodes[current].current.parent;
		}
	}
	return winding;
}

void BentleyOttmann::check_intersection(uint32_t p_treenode_edge, R128 p_x_min) {
	DEV_ASSERT(p_treenode_edge != edges_tree && tree_nodes[p_treenode_edge].current.next != edges_tree);
	Edge &edge1 = edges[tree_nodes[p_treenode_edge].element];
	Edge &edge2 = edges[tree_nodes[tree_nodes[p_treenode_edge].current.next].element];
	R128 start1_x = points[edge1.point_start].x;
	R128 start1_y = points[edge1.point_start].y;
	R128 dir1_x = edge1.dir_x;
	R128 dir1_y = edge1.dir_y;
	R128 start2_x = points[edge2.point_start].x;
	R128 start2_y = points[edge2.point_start].y;
	R128 dir2_x = edge2.dir_x;
	R128 dir2_y = edge2.dir_y;
	R128 max = (edge1.x_next_check < edge2.x_next_check) ? edge1.x_next_check : edge2.x_next_check;
	if (max <= p_x_min) {
		return;
	}
	R128 y1min, m1min, y2min, m2min;
	R128 y1mid, m1mid, y2mid, m2mid;
	R128 y1max, m1max, y2max, m2max;
	quotient(max - start1_x, dir1_y, dir1_x, y1max, m1max);
	quotient(max - start2_x, dir2_y, dir2_x, y2max, m2max);
	y1max += start1_y;
	y2max += start2_y;
	if (y1max <= y2max) {
		return;
	}
	quotient(p_x_min - start1_x, dir1_y, dir1_x, y1min, m1min);
	quotient(p_x_min - start2_x, dir2_y, dir2_x, y2min, m2min);
	y1min += start1_y;
	y2min += start2_y;
	R128 y1step = edge1.step_y;
	R128 m1step = edge1.step_mod;
	R128 y2step = edge2.step_y;
	R128 m2step = edge2.step_mod;
	DEV_ASSERT(y1min <= y2min);
	while (p_x_min + R128_smallest < max) {
		R128 mid = (p_x_min + max) >> 1;
		if ((p_x_min + max) & R128_smallest) {
			quotient_mid(y1min, m1min, y1max, m1max, y1step, m1step, dir1_x, y1mid, m1mid);
			quotient_mid(y2min, m2min, y2max, m2max, y2step, m2step, dir2_x, y2mid, m2mid);
		} else {
			quotient_mid(y1min, m1min, y1max, m1max, R128_zero, R128_zero, dir1_x, y1mid, m1mid);
			quotient_mid(y2min, m2min, y2max, m2max, R128_zero, R128_zero, dir2_x, y2mid, m2mid);
		}
		if (y1mid > y2mid) {
			max = mid;
			y1max = y1mid;
			m1max = m1mid;
			y2max = y2mid;
			m2max = m2mid;
		} else {
			p_x_min = mid;
			y1min = y1mid;
			m1min = m1mid;
			y2min = y2mid;
			m2min = m2mid;
		}
	}
	DEV_ASSERT(p_x_min + R128_smallest == max);
	edge1.x_next_check = p_x_min;
	list_insert(edge1.listnode_check, slices[add_slice(p_x_min)].check_list);
}

uint32_t BentleyOttmann::tree_create(uint32_t p_element, int p_value) {
	TreeNode node;
	node.previous.prev = node.previous.next = node.current.prev = node.current.next = tree_nodes.size();
	node.element = p_element;
	node.self_value = p_value;
	tree_nodes.push_back(node);
	return node.current.next;
}

void BentleyOttmann::tree_insert(uint32_t p_insert_item, uint32_t p_insert_after, const R128 &p_version) {
	DEV_ASSERT(p_insert_item != 0 && p_insert_after != 0);
	tree_version(p_insert_item, p_version);
	tree_version(p_insert_after, p_version);
	tree_version(tree_nodes[p_insert_after].current.next, p_version);
	if (tree_nodes[p_insert_after].current.right == 0) {
		tree_nodes[p_insert_after].current.right = p_insert_item;
		tree_nodes[p_insert_item].current.parent = p_insert_after;
	} else {
		DEV_ASSERT(tree_nodes[tree_nodes[p_insert_after].current.next].current.left == 0);
		tree_nodes[tree_nodes[p_insert_after].current.next].current.left = p_insert_item;
		tree_nodes[p_insert_item].current.parent = tree_nodes[p_insert_after].current.next;
	}
	tree_nodes[p_insert_item].current.prev = p_insert_after;
	tree_nodes[p_insert_item].current.next = tree_nodes[p_insert_after].current.next;
	tree_nodes[tree_nodes[p_insert_after].current.next].current.prev = p_insert_item;
	tree_nodes[p_insert_after].current.next = p_insert_item;
	DEV_ASSERT(tree_nodes[p_insert_item].current.sum_value == 0);
	uint32_t item = p_insert_item;
	while (item) {
		tree_version(item, p_version);
		tree_nodes[item].current.sum_value += tree_nodes[p_insert_item].self_value;
		tree_nodes[item].current.size++;
		item = tree_nodes[item].current.parent;
	}
	item = p_insert_item;
	uint32_t parent = tree_nodes[item].current.parent;
	while (tree_nodes[parent].current.parent) {
		uint32_t sibling = tree_nodes[parent].current.left;
		if (sibling == item) {
			sibling = tree_nodes[parent].current.right;
		}
		if (tree_nodes[sibling].current.is_heavy) {
			tree_version(sibling, p_version);
			tree_nodes[sibling].current.is_heavy = false;
			return;
		}
		if (!tree_nodes[item].current.is_heavy) {
			tree_version(item, p_version);
			tree_nodes[item].current.is_heavy = true;
			item = parent;
			parent = tree_nodes[item].current.parent;
			continue;
		}
		uint32_t move;
		uint32_t unmove;
		uint32_t move_move;
		uint32_t move_unmove;
		if (item == tree_nodes[parent].current.left) {
			move = tree_nodes[item].current.right;
			unmove = tree_nodes[item].current.left;
			move_move = tree_nodes[move].current.left;
			move_unmove = tree_nodes[move].current.right;
		} else {
			move = tree_nodes[item].current.left;
			unmove = tree_nodes[item].current.right;
			move_move = tree_nodes[move].current.right;
			move_unmove = tree_nodes[move].current.left;
		}
		if (!tree_nodes[move].current.is_heavy) {
			tree_version(parent, p_version);
			tree_rotate(item, p_version);
			tree_nodes[item].current.is_heavy = tree_nodes[parent].current.is_heavy;
			tree_nodes[parent].current.is_heavy = !tree_nodes[unmove].current.is_heavy;
			if (tree_nodes[unmove].current.is_heavy) {
				tree_version(unmove, p_version);
				tree_nodes[unmove].current.is_heavy = false;
				return;
			}
			DEV_ASSERT(move != 0);
			tree_version(move, p_version);
			tree_nodes[move].current.is_heavy = true;
			parent = tree_nodes[item].current.parent;
			continue;
		}
		tree_rotate(move, p_version);
		tree_rotate(move, p_version);
		tree_nodes[move].current.is_heavy = tree_nodes[parent].current.is_heavy;
		if (unmove != 0) {
			tree_version(unmove, p_version);
			tree_nodes[unmove].current.is_heavy = tree_nodes[move_unmove].current.is_heavy;
		}
		if (sibling != 0) {
			tree_version(sibling, p_version);
			tree_nodes[sibling].current.is_heavy = tree_nodes[move_move].current.is_heavy;
		}
		tree_nodes[item].current.is_heavy = false;
		tree_nodes[parent].current.is_heavy = false;
		tree_nodes[move_move].current.is_heavy = false;
		if (move_unmove != 0) {
			tree_version(move_unmove, p_version);
			tree_nodes[move_unmove].current.is_heavy = false;
		}
		return;
	}
}

void BentleyOttmann::tree_remove(uint32_t p_remove_item, const R128 &p_version) {
	DEV_ASSERT(tree_nodes[p_remove_item].current.parent != 0);
	if (tree_nodes[p_remove_item].current.left != 0 && tree_nodes[p_remove_item].current.right != 0) {
		uint32_t prev = tree_nodes[p_remove_item].current.prev;
		DEV_ASSERT(tree_nodes[prev].current.parent != 0 && tree_nodes[prev].current.right == 0);
		tree_swap(p_remove_item, prev, p_version);
	}
	DEV_ASSERT(tree_nodes[p_remove_item].current.left == 0 || tree_nodes[p_remove_item].current.right == 0);
	uint32_t prev = tree_nodes[p_remove_item].current.prev;
	uint32_t next = tree_nodes[p_remove_item].current.next;
	tree_version(prev, p_version);
	tree_version(next, p_version);
	tree_nodes[prev].current.next = next;
	tree_nodes[next].current.prev = prev;
	uint32_t parent = tree_nodes[p_remove_item].current.parent;
	uint32_t replacement = tree_nodes[p_remove_item].current.left;
	if (replacement == 0) {
		replacement = tree_nodes[p_remove_item].current.right;
	}
	if (replacement != 0) {
		tree_version(replacement, p_version);
		tree_nodes[replacement].current.parent = parent;
		tree_nodes[replacement].current.is_heavy = tree_nodes[p_remove_item].current.is_heavy;
	}
	tree_version(parent, p_version);
	if (tree_nodes[parent].current.left == p_remove_item) {
		tree_nodes[parent].current.left = replacement;
	} else {
		tree_nodes[parent].current.right = replacement;
	}
	tree_version(p_remove_item, p_version);
	tree_nodes[p_remove_item].current.left = tree_nodes[p_remove_item].current.right = tree_nodes[p_remove_item].current.parent = 0;
	tree_nodes[p_remove_item].current.prev = tree_nodes[p_remove_item].current.next = p_remove_item;
	tree_nodes[p_remove_item].current.is_heavy = false;
	tree_nodes[p_remove_item].current.sum_value = 0;
	tree_nodes[p_remove_item].current.size = 0;
	uint32_t item = parent;
	while (item) {
		tree_version(item, p_version);
		tree_nodes[item].current.sum_value -= tree_nodes[p_remove_item].self_value;
		tree_nodes[item].current.size--;
		item = tree_nodes[item].current.parent;
	}
	item = replacement;
	if (tree_nodes[parent].current.left == 0 && tree_nodes[parent].current.right == 0) {
		item = parent;
		parent = tree_nodes[item].current.parent;
	}
	while (tree_nodes[parent].current.parent != 0) {
		uint32_t sibling = tree_nodes[parent].current.left;
		if (sibling == item) {
			sibling = tree_nodes[parent].current.right;
		}
		DEV_ASSERT(sibling != 0);
		if (tree_nodes[item].current.is_heavy) {
			tree_version(item, p_version);
			tree_nodes[item].current.is_heavy = false;
			item = parent;
			parent = tree_nodes[item].current.parent;
			continue;
		}
		if (!tree_nodes[sibling].current.is_heavy) {
			tree_version(sibling, p_version);
			tree_nodes[sibling].current.is_heavy = true;
			return;
		}
		uint32_t move;
		uint32_t unmove;
		uint32_t move_move;
		uint32_t move_unmove;
		if (sibling == tree_nodes[parent].current.left) {
			move = tree_nodes[sibling].current.right;
			unmove = tree_nodes[sibling].current.left;
			move_move = tree_nodes[move].current.left;
			move_unmove = tree_nodes[move].current.right;
		} else {
			move = tree_nodes[sibling].current.left;
			unmove = tree_nodes[sibling].current.right;
			move_move = tree_nodes[move].current.right;
			move_unmove = tree_nodes[move].current.left;
		}
		if (!tree_nodes[move].current.is_heavy) {
			tree_version(parent, p_version);
			tree_rotate(sibling, p_version);
			tree_nodes[sibling].current.is_heavy = tree_nodes[parent].current.is_heavy;
			tree_nodes[parent].current.is_heavy = !tree_nodes[unmove].current.is_heavy;
			if (tree_nodes[unmove].current.is_heavy) {
				tree_version(unmove, p_version);
				tree_nodes[unmove].current.is_heavy = false;
				item = sibling;
				parent = tree_nodes[item].current.parent;
				continue;
			}
			DEV_ASSERT(move != 0);
			tree_version(move, p_version);
			tree_nodes[move].current.is_heavy = true;
			return;
		}
		tree_rotate(move, p_version);
		tree_rotate(move, p_version);
		tree_nodes[move].current.is_heavy = tree_nodes[parent].current.is_heavy;
		if (unmove != 0) {
			tree_version(unmove, p_version);
			tree_nodes[unmove].current.is_heavy = tree_nodes[move_unmove].current.is_heavy;
		}
		if (item != 0) {
			tree_version(item, p_version);
			tree_nodes[item].current.is_heavy = tree_nodes[move_move].current.is_heavy;
		}
		tree_nodes[sibling].current.is_heavy = false;
		tree_nodes[parent].current.is_heavy = false;
		tree_nodes[move_move].current.is_heavy = false;
		if (move_unmove != 0) {
			tree_version(move_unmove, p_version);
			tree_nodes[move_unmove].current.is_heavy = false;
		}
		item = move;
		parent = tree_nodes[item].current.parent;
		continue;
	}
}

void BentleyOttmann::tree_rotate(uint32_t p_item, const R128 &p_version) {
	DEV_ASSERT(tree_nodes[tree_nodes[p_item].current.parent].current.parent != 0);
	uint32_t parent = tree_nodes[p_item].current.parent;
	tree_version(p_item, p_version);
	tree_version(parent, p_version);
	if (tree_nodes[parent].current.left == p_item) {
		uint32_t move = tree_nodes[p_item].current.right;
		tree_nodes[parent].current.left = move;
		tree_nodes[p_item].current.right = parent;
		if (move) {
			tree_version(move, p_version);
			tree_nodes[move].current.parent = parent;
		}
	} else {
		uint32_t move = tree_nodes[p_item].current.left;
		tree_nodes[parent].current.right = move;
		tree_nodes[p_item].current.left = parent;
		if (move) {
			tree_version(move, p_version);
			tree_nodes[move].current.parent = parent;
		}
	}
	uint32_t grandparent = tree_nodes[parent].current.parent;
	tree_version(grandparent, p_version);
	tree_nodes[p_item].current.parent = grandparent;
	if (tree_nodes[grandparent].current.left == parent) {
		tree_nodes[grandparent].current.left = p_item;
	} else {
		tree_nodes[grandparent].current.right = p_item;
	}
	tree_nodes[parent].current.parent = p_item;
	tree_nodes[parent].current.sum_value = tree_nodes[parent].self_value + tree_nodes[tree_nodes[parent].current.left].current.sum_value + tree_nodes[tree_nodes[parent].current.right].current.sum_value;
	tree_nodes[p_item].current.sum_value = tree_nodes[p_item].self_value + tree_nodes[tree_nodes[p_item].current.left].current.sum_value + tree_nodes[tree_nodes[p_item].current.right].current.sum_value;
	tree_nodes[parent].current.size = tree_nodes[tree_nodes[parent].current.left].current.size + tree_nodes[tree_nodes[parent].current.right].current.size + 1;
	tree_nodes[p_item].current.size = tree_nodes[tree_nodes[p_item].current.left].current.size + tree_nodes[tree_nodes[p_item].current.right].current.size + 1;
}

void BentleyOttmann::tree_swap(uint32_t p_item1, uint32_t p_item2, const R128 &p_version) {
	DEV_ASSERT(tree_nodes[p_item1].current.parent != 0 && tree_nodes[p_item2].current.parent != 0);
	tree_version(p_item1, p_version);
	tree_version(p_item2, p_version);
	uint32_t parent1 = tree_nodes[p_item1].current.parent;
	uint32_t left1 = tree_nodes[p_item1].current.left;
	uint32_t right1 = tree_nodes[p_item1].current.right;
	uint32_t prev1 = tree_nodes[p_item1].current.prev;
	uint32_t next1 = tree_nodes[p_item1].current.next;
	uint32_t parent2 = tree_nodes[p_item2].current.parent;
	uint32_t left2 = tree_nodes[p_item2].current.left;
	uint32_t right2 = tree_nodes[p_item2].current.right;
	uint32_t prev2 = tree_nodes[p_item2].current.prev;
	uint32_t next2 = tree_nodes[p_item2].current.next;
	tree_version(parent1, p_version);
	tree_version(prev1, p_version);
	tree_version(next1, p_version);
	tree_version(parent2, p_version);
	tree_version(prev2, p_version);
	tree_version(next2, p_version);
	if (tree_nodes[parent1].current.left == p_item1) {
		tree_nodes[parent1].current.left = p_item2;
	} else {
		tree_nodes[parent1].current.right = p_item2;
	}
	if (tree_nodes[parent2].current.left == p_item2) {
		tree_nodes[parent2].current.left = p_item1;
	} else {
		tree_nodes[parent2].current.right = p_item1;
	}
	if (left1) {
		tree_version(left1, p_version);
		tree_nodes[left1].current.parent = p_item2;
	}
	if (right1) {
		tree_version(right1, p_version);
		tree_nodes[right1].current.parent = p_item2;
	}
	if (left2) {
		tree_version(left2, p_version);
		tree_nodes[left2].current.parent = p_item1;
	}
	if (right2) {
		tree_version(right2, p_version);
		tree_nodes[right2].current.parent = p_item1;
	}
	tree_nodes[prev1].current.next = p_item2;
	tree_nodes[next1].current.prev = p_item2;
	tree_nodes[prev2].current.next = p_item1;
	tree_nodes[next2].current.prev = p_item1;
	parent1 = tree_nodes[p_item1].current.parent;
	left1 = tree_nodes[p_item1].current.left;
	right1 = tree_nodes[p_item1].current.right;
	prev1 = tree_nodes[p_item1].current.prev;
	next1 = tree_nodes[p_item1].current.next;
	parent2 = tree_nodes[p_item2].current.parent;
	left2 = tree_nodes[p_item2].current.left;
	right2 = tree_nodes[p_item2].current.right;
	prev2 = tree_nodes[p_item2].current.prev;
	next2 = tree_nodes[p_item2].current.next;
	tree_nodes[p_item2].current.parent = parent1;
	tree_nodes[p_item2].current.left = left1;
	tree_nodes[p_item2].current.right = right1;
	tree_nodes[p_item2].current.prev = prev1;
	tree_nodes[p_item2].current.next = next1;
	tree_nodes[p_item1].current.parent = parent2;
	tree_nodes[p_item1].current.left = left2;
	tree_nodes[p_item1].current.right = right2;
	tree_nodes[p_item1].current.prev = prev2;
	tree_nodes[p_item1].current.next = next2;
	bool is_heavy = tree_nodes[p_item1].current.is_heavy;
	tree_nodes[p_item1].current.is_heavy = tree_nodes[p_item2].current.is_heavy;
	tree_nodes[p_item2].current.is_heavy = is_heavy;
	int sum_value = tree_nodes[p_item1].current.sum_value;
	tree_nodes[p_item1].current.sum_value = tree_nodes[p_item2].current.sum_value;
	tree_nodes[p_item2].current.sum_value = sum_value;
	uint32_t size = tree_nodes[p_item1].current.size;
	tree_nodes[p_item1].current.size = tree_nodes[p_item2].current.size;
	tree_nodes[p_item2].current.size = size;
	int diff = tree_nodes[p_item1].self_value - tree_nodes[p_item2].self_value;
	if (diff) {
		while (p_item1) {
			tree_version(p_item1, p_version);
			tree_nodes[p_item1].current.sum_value += diff;
			p_item1 = tree_nodes[p_item1].current.parent;
		}
		while (p_item2) {
			tree_version(p_item2, p_version);
			tree_nodes[p_item2].current.sum_value -= diff;
			p_item2 = tree_nodes[p_item2].current.parent;
		}
	}
}

void BentleyOttmann::tree_version(uint32_t p_item, const R128 &p_version) {
	DEV_ASSERT(p_item != 0);
	if (tree_nodes[p_item].version == p_version) {
		return;
	}
	tree_nodes[p_item].version = p_version;
	tree_nodes[p_item].previous = tree_nodes[p_item].current;
}

void BentleyOttmann::tree_index(uint32_t p_item) {
	int index = tree_nodes[tree_nodes[p_item].current.left].current.size;
	uint32_t current = p_item;
	uint32_t parent = tree_nodes[current].current.parent;
	while (parent) {
		if (tree_nodes[parent].current.right == current) {
			index += tree_nodes[tree_nodes[parent].current.left].current.size + 1;
		}
		current = parent;
		parent = tree_nodes[current].current.parent;
	}
	tree_nodes[p_item].index = index;
}

void BentleyOttmann::tree_index_previous(uint32_t p_item, const R128 &p_version) {
	int index;
	uint32_t current = p_item;
	uint32_t parent;
	if (tree_nodes[p_item].version == p_version) {
		parent = tree_nodes[p_item].previous.parent;
		if (tree_nodes[tree_nodes[p_item].previous.left].version == p_version) {
			index = tree_nodes[tree_nodes[p_item].previous.left].previous.size;
		} else {
			index = tree_nodes[tree_nodes[p_item].previous.left].current.size;
		}
	} else {
		parent = tree_nodes[p_item].current.parent;
		if (tree_nodes[tree_nodes[p_item].current.left].version == p_version) {
			index = tree_nodes[tree_nodes[p_item].current.left].previous.size;
		} else {
			index = tree_nodes[tree_nodes[p_item].current.left].current.size;
		}
	}
	while (parent) {
		if (tree_nodes[parent].version == p_version) {
			if (tree_nodes[parent].previous.right == current) {
				if (tree_nodes[tree_nodes[parent].previous.left].version == p_version) {
					index += tree_nodes[tree_nodes[parent].previous.left].previous.size + 1;
				} else {
					index += tree_nodes[tree_nodes[parent].previous.left].current.size + 1;
				}
			}
			current = parent;
			parent = tree_nodes[current].previous.parent;
		} else {
			if (tree_nodes[parent].current.right == current) {
				if (tree_nodes[tree_nodes[parent].current.left].version == p_version) {
					index += tree_nodes[tree_nodes[parent].current.left].previous.size + 1;
				} else {
					index += tree_nodes[tree_nodes[parent].current.left].current.size + 1;
				}
			}
			current = parent;
			parent = tree_nodes[current].current.parent;
		}
	}
	tree_nodes[p_item].index = index;
}

uint32_t BentleyOttmann::list_create(uint32_t p_element) {
	ListNode node;
	node.anchor = node.prev = node.next = list_nodes.size();
	node.element = p_element;
	list_nodes.push_back(node);
	return node.next;
}

void BentleyOttmann::list_insert(uint32_t p_insert_item, uint32_t p_list) {
	DEV_ASSERT(p_insert_item != p_list);
	DEV_ASSERT(list_nodes[p_list].anchor == p_list);
	if (list_nodes[p_insert_item].anchor == p_list) {
		return;
	}
	if (list_nodes[p_insert_item].anchor != p_insert_item) {
		list_remove(p_insert_item);
	}
	list_nodes[p_insert_item].anchor = p_list;
	list_nodes[p_insert_item].prev = p_list;
	list_nodes[p_insert_item].next = list_nodes[p_list].next;
	list_nodes[list_nodes[p_list].next].prev = p_insert_item;
	list_nodes[p_list].next = p_insert_item;
}

void BentleyOttmann::list_remove(uint32_t p_remove_item) {
	list_nodes[list_nodes[p_remove_item].next].prev = list_nodes[p_remove_item].prev;
	list_nodes[list_nodes[p_remove_item].prev].next = list_nodes[p_remove_item].next;
	list_nodes[p_remove_item].anchor = list_nodes[p_remove_item].prev = list_nodes[p_remove_item].next = p_remove_item;
}
