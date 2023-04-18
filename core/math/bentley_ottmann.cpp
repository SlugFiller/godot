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
#include "thirdparty/misc/r128.h"

/**
 * Big integers by embedding 28 bit "digits" into 32 bit integer arrays.
 * 28 bits ensures that multiplying any two "digits" keeps the result under
 * 64 bits, with 8 bits of room left to spare.
 */

template <uint32_t digits>
static void bigint28_from_r128(int32_t r_out[digits], R128 p_in);

template <>
static void bigint28_from_r128<5>(int32_t r_out[5], R128 p_in) {
	r_out[0] = static_cast<int32_t>(p_in.lo & 0xFFFFFFF);
	r_out[1] = static_cast<int32_t>((p_in.lo >> 28) & 0xFFFFFFF);
	r_out[2] = static_cast<int32_t>((p_in.lo >> 56) | ((p_in.hi << 8) & 0xFFFFFFF));
	r_out[3] = static_cast<int32_t>((p_in.hi >> 20) & 0xFFFFFFF);
	r_out[4] = static_cast<int32_t>(p_in.hi >> 32) >> 16;
}

template <uint32_t digits>
static R128 bigint28_to_r128(const int32_t p_in[digits]);

template <>
static R128 bigint28_to_r128<5>(const int32_t p_in[5]) {
	return R128(static_cast<R128_U64>(p_in[0]) | (static_cast<R128_U64>(p_in[1]) << 28) | (static_cast<R128_U64>(p_in[2]) << 56), (static_cast<R128_U64>(p_in[2]) >> 8) | (static_cast<R128_U64>(p_in[3]) << 20) | (static_cast<R128_U64>(p_in[4]) << 48));
}

template <>
static R128 bigint28_to_r128<10>(const int32_t p_in[10]) {
	DEV_ASSERT((p_in[5] == 0 && p_in[6] == 0 && p_in[7] == 0 && p_in[8] == 0 && p_in[9] == 0) || (p_in[5] == 0xFFFFFFF && p_in[6] == 0xFFFFFFF && p_in[7] == 0xFFFFFFF && p_in[8] == 0xFFFFFFF && p_in[9] == -1));
	return R128(static_cast<R128_U64>(p_in[0]) | (static_cast<R128_U64>(p_in[1]) << 28) | (static_cast<R128_U64>(p_in[2]) << 56), (static_cast<R128_U64>(p_in[2]) >> 8) | (static_cast<R128_U64>(p_in[3]) << 20) | (static_cast<R128_U64>(p_in[4]) << 48));
}

template <>
static R128 bigint28_to_r128<15>(const int32_t p_in[15]) {
	DEV_ASSERT((p_in[5] == 0 && p_in[6] == 0 && p_in[7] == 0 && p_in[8] == 0 && p_in[9] == 0 && p_in[10] == 0 && p_in[11] == 0 && p_in[12] == 0 && p_in[13] == 0 && p_in[14] == 0) || (p_in[5] == 0xFFFFFFF && p_in[6] == 0xFFFFFFF && p_in[7] == 0xFFFFFFF && p_in[8] == 0xFFFFFFF && p_in[9] == 0xFFFFFFF && p_in[10] == 0xFFFFFFF && p_in[11] == 0xFFFFFFF && p_in[12] == 0xFFFFFFF && p_in[13] == 0xFFFFFFF && p_in[14] == -1));
	return R128(static_cast<R128_U64>(p_in[0]) | (static_cast<R128_U64>(p_in[1]) << 28) | (static_cast<R128_U64>(p_in[2]) << 56), (static_cast<R128_U64>(p_in[2]) >> 8) | (static_cast<R128_U64>(p_in[3]) << 20) | (static_cast<R128_U64>(p_in[4]) << 48));
}

static int bigint28_clz(int32_t p_in) {
	static const int de_bruijn[32] = {
		31, 30, 29, 25, 28, 20, 24, 15,
		27, 17, 19, 10, 23, 8, 14, 5,
		0, 26, 21, 16, 18, 11, 9, 6,
		1, 22, 12, 7, 2, 13, 3, 4
	};
	uint32_t v = static_cast<uint32_t>(p_in);
	// Turn off every bit in v except the highest set, making it so that
	// v == 1 << (31 - clz(v)), and ergo v * x == x << (31 - clz(v))
	v |= (v >> 1);
	v |= (v >> 2);
	v |= (v >> 4);
	v |= (v >> 8);
	v |= (v >> 16);
	v -= (v >> 1);
	// De Bruijn 32-bit sequence derived using Burrows-Wheeler transform:
	// For each x between 0 and 31, 04653ADF << x >> 27 yields a different
	// number between 0 and 31, with no collisions
	return de_bruijn[(v * 0x04653ADFUL) >> 27];
}

template <uint32_t digits1, uint32_t digits2>
static void bigint28_copy(int32_t r_out[digits1], const int32_t p_in[digits2]);

template <>
static void bigint28_copy<5, 5>(int32_t r_out[5], const int32_t p_in[5]) {
	r_out[0] = p_in[0];
	r_out[1] = p_in[1];
	r_out[2] = p_in[2];
	r_out[3] = p_in[3];
	r_out[4] = p_in[4];
}

template <>
static void bigint28_copy<10, 10>(int32_t r_out[10], const int32_t p_in[10]) {
	r_out[0] = p_in[0];
	r_out[1] = p_in[1];
	r_out[2] = p_in[2];
	r_out[3] = p_in[3];
	r_out[4] = p_in[4];
	r_out[5] = p_in[5];
	r_out[6] = p_in[6];
	r_out[7] = p_in[7];
	r_out[8] = p_in[8];
	r_out[9] = p_in[9];
}

template <>
static void bigint28_copy<15, 15>(int32_t r_out[15], const int32_t p_in[15]) {
	r_out[0] = p_in[0];
	r_out[1] = p_in[1];
	r_out[2] = p_in[2];
	r_out[3] = p_in[3];
	r_out[4] = p_in[4];
	r_out[5] = p_in[5];
	r_out[6] = p_in[6];
	r_out[7] = p_in[7];
	r_out[8] = p_in[8];
	r_out[9] = p_in[9];
	r_out[10] = p_in[10];
	r_out[11] = p_in[11];
	r_out[12] = p_in[12];
	r_out[13] = p_in[13];
	r_out[14] = p_in[14];
}

template <>
static void bigint28_copy<5, 10>(int32_t r_out[5], const int32_t p_in[10]) {
	DEV_ASSERT((p_in[5] == 0 && p_in[6] == 0 && p_in[7] == 0 && p_in[8] == 0 && p_in[9] == 0) || (p_in[5] == 0xFFFFFFF && p_in[6] == 0xFFFFFFF && p_in[7] == 0xFFFFFFF && p_in[8] == 0xFFFFFFF && p_in[9] == -1));
	r_out[0] = p_in[0];
	r_out[1] = p_in[1];
	r_out[2] = p_in[2];
	r_out[3] = p_in[3];
	r_out[4] = p_in[4] | ((p_in[9] >> 31) << 28);
}

template <>
static void bigint28_copy<5, 15>(int32_t r_out[5], const int32_t p_in[15]) {
	DEV_ASSERT((p_in[5] == 0 && p_in[6] == 0 && p_in[7] == 0 && p_in[8] == 0 && p_in[9] == 0 && p_in[10] == 0 && p_in[11] == 0 && p_in[12] == 0 && p_in[13] == 0 && p_in[14] == 0) || (p_in[5] == 0xFFFFFFF && p_in[6] == 0xFFFFFFF && p_in[7] == 0xFFFFFFF && p_in[8] == 0xFFFFFFF && p_in[9] == 0xFFFFFFF && p_in[10] == 0xFFFFFFF && p_in[11] == 0xFFFFFFF && p_in[12] == 0xFFFFFFF && p_in[13] == 0xFFFFFFF && p_in[14] == -1));
	r_out[0] = p_in[0];
	r_out[1] = p_in[1];
	r_out[2] = p_in[2];
	r_out[3] = p_in[3];
	r_out[4] = p_in[4] | ((p_in[14] >> 31) << 28);
}

template <>
static void bigint28_copy<10, 15>(int32_t r_out[10], const int32_t p_in[15]) {
	DEV_ASSERT((p_in[10] == 0 && p_in[11] == 0 && p_in[12] == 0 && p_in[13] == 0 && p_in[14] == 0) || (p_in[10] == 0xFFFFFFF && p_in[11] == 0xFFFFFFF && p_in[12] == 0xFFFFFFF && p_in[13] == 0xFFFFFFF && p_in[14] == -1));
	r_out[0] = p_in[0];
	r_out[1] = p_in[1];
	r_out[2] = p_in[2];
	r_out[3] = p_in[3];
	r_out[4] = p_in[4];
	r_out[5] = p_in[5];
	r_out[6] = p_in[6];
	r_out[7] = p_in[7];
	r_out[8] = p_in[8];
	r_out[9] = p_in[9] | ((p_in[14] >> 31) << 28);
}

template <uint32_t digits>
static void bigint28_clear(int32_t r_out[digits]);

template <>
static void bigint28_clear<5>(int32_t r_out[5]) {
	r_out[0] = r_out[1] = r_out[2] = r_out[3] = r_out[4] = 0;
}

template <>
static void bigint28_clear<10>(int32_t r_out[10]) {
	r_out[0] = r_out[1] = r_out[2] = r_out[3] = r_out[4] = r_out[5] = r_out[6] = r_out[7] = r_out[8] = r_out[9] = 0;
}

template <>
static void bigint28_clear<15>(int32_t r_out[15]) {
	r_out[0] = r_out[1] = r_out[2] = r_out[3] = r_out[4] = r_out[5] = r_out[6] = r_out[7] = r_out[8] = r_out[9] = r_out[10] = r_out[11] = r_out[12] = r_out[13] = r_out[14] = 0;
}

template <uint32_t digits>
static void bigint28_neg(int32_t r_out[digits], const int32_t p_in[digits]);

template <>
static void bigint28_neg<5>(int32_t r_out[5], const int32_t p_in[5]) {
	r_out[0] = (p_in[0] ^ 0xFFFFFFF) + 1;
	r_out[1] = (p_in[1] ^ 0xFFFFFFF) + (r_out[0] >> 28);
	r_out[2] = (p_in[2] ^ 0xFFFFFFF) + (r_out[1] >> 28);
	r_out[3] = (p_in[3] ^ 0xFFFFFFF) + (r_out[2] >> 28);
	r_out[4] = (~p_in[4]) + (r_out[3] >> 28);
	r_out[0] &= 0xFFFFFFF;
	r_out[1] &= 0xFFFFFFF;
	r_out[2] &= 0xFFFFFFF;
	r_out[3] &= 0xFFFFFFF;
}

template <>
static void bigint28_neg<10>(int32_t r_out[10], const int32_t p_in[10]) {
	r_out[0] = (p_in[0] ^ 0xFFFFFFF) + 1;
	r_out[1] = (p_in[1] ^ 0xFFFFFFF) + (r_out[0] >> 28);
	r_out[2] = (p_in[2] ^ 0xFFFFFFF) + (r_out[1] >> 28);
	r_out[3] = (p_in[3] ^ 0xFFFFFFF) + (r_out[2] >> 28);
	r_out[4] = (p_in[4] ^ 0xFFFFFFF) + (r_out[3] >> 28);
	r_out[5] = (p_in[5] ^ 0xFFFFFFF) + (r_out[4] >> 28);
	r_out[6] = (p_in[6] ^ 0xFFFFFFF) + (r_out[5] >> 28);
	r_out[7] = (p_in[7] ^ 0xFFFFFFF) + (r_out[6] >> 28);
	r_out[8] = (p_in[8] ^ 0xFFFFFFF) + (r_out[7] >> 28);
	r_out[9] = (~p_in[9]) + (r_out[8] >> 28);
	r_out[0] &= 0xFFFFFFF;
	r_out[1] &= 0xFFFFFFF;
	r_out[2] &= 0xFFFFFFF;
	r_out[3] &= 0xFFFFFFF;
	r_out[4] &= 0xFFFFFFF;
	r_out[5] &= 0xFFFFFFF;
	r_out[6] &= 0xFFFFFFF;
	r_out[7] &= 0xFFFFFFF;
	r_out[8] &= 0xFFFFFFF;
}

template <>
static void bigint28_neg<15>(int32_t r_out[15], const int32_t p_in[15]) {
	r_out[0] = (p_in[0] ^ 0xFFFFFFF) + 1;
	r_out[1] = (p_in[1] ^ 0xFFFFFFF) + (r_out[0] >> 28);
	r_out[2] = (p_in[2] ^ 0xFFFFFFF) + (r_out[1] >> 28);
	r_out[3] = (p_in[3] ^ 0xFFFFFFF) + (r_out[2] >> 28);
	r_out[4] = (p_in[4] ^ 0xFFFFFFF) + (r_out[3] >> 28);
	r_out[5] = (p_in[5] ^ 0xFFFFFFF) + (r_out[4] >> 28);
	r_out[6] = (p_in[6] ^ 0xFFFFFFF) + (r_out[5] >> 28);
	r_out[7] = (p_in[7] ^ 0xFFFFFFF) + (r_out[6] >> 28);
	r_out[8] = (p_in[8] ^ 0xFFFFFFF) + (r_out[7] >> 28);
	r_out[9] = (p_in[9] ^ 0xFFFFFFF) + (r_out[8] >> 28);
	r_out[10] = (p_in[10] ^ 0xFFFFFFF) + (r_out[9] >> 28);
	r_out[11] = (p_in[11] ^ 0xFFFFFFF) + (r_out[10] >> 28);
	r_out[12] = (p_in[12] ^ 0xFFFFFFF) + (r_out[11] >> 28);
	r_out[13] = (p_in[13] ^ 0xFFFFFFF) + (r_out[12] >> 28);
	r_out[14] = (~p_in[14]) + (r_out[13] >> 28);
	r_out[0] &= 0xFFFFFFF;
	r_out[1] &= 0xFFFFFFF;
	r_out[2] &= 0xFFFFFFF;
	r_out[3] &= 0xFFFFFFF;
	r_out[4] &= 0xFFFFFFF;
	r_out[5] &= 0xFFFFFFF;
	r_out[6] &= 0xFFFFFFF;
	r_out[7] &= 0xFFFFFFF;
	r_out[8] &= 0xFFFFFFF;
	r_out[9] &= 0xFFFFFFF;
	r_out[10] &= 0xFFFFFFF;
	r_out[11] &= 0xFFFFFFF;
	r_out[12] &= 0xFFFFFFF;
	r_out[13] &= 0xFFFFFFF;
}

template <uint32_t digits>
static int bigint28_sign(const int32_t p_in[digits]);

template <>
static int bigint28_sign<5>(const int32_t p_in[5]) {
	if (p_in[4] < 0) {
		return -1;
	}
	if (p_in[0] || p_in[1] || p_in[2] || p_in[3] || p_in[4]) {
		return 1;
	}
	return 0;
}

template <>
static int bigint28_sign<10>(const int32_t p_in[10]) {
	if (p_in[9] < 0) {
		return -1;
	}
	if (p_in[0] || p_in[1] || p_in[2] || p_in[3] || p_in[4] || p_in[5] || p_in[6] || p_in[7] || p_in[8] || p_in[9]) {
		return 1;
	}
	return 0;
}

template <>
static int bigint28_sign<15>(const int32_t p_in[15]) {
	if (p_in[14] < 0) {
		return -1;
	}
	if (p_in[0] || p_in[1] || p_in[2] || p_in[3] || p_in[4] || p_in[5] || p_in[6] || p_in[7] || p_in[8] || p_in[9] || p_in[10] || p_in[11] || p_in[12] || p_in[13] || p_in[14]) {
		return 1;
	}
	return 0;
}

template <uint32_t digits>
static void bigint28_add1(int32_t r_out[digits]);

template <>
static void bigint28_add1<5>(int32_t r_out[5]) {
	r_out[0]++;
	r_out[1] += r_out[0] >> 28;
	r_out[2] += r_out[1] >> 28;
	r_out[3] += r_out[2] >> 28;
	r_out[4] += r_out[3] >> 28;
	r_out[0] &= 0xFFFFFFF;
	r_out[1] &= 0xFFFFFFF;
	r_out[2] &= 0xFFFFFFF;
	r_out[3] &= 0xFFFFFFF;
}

template <>
static void bigint28_add1<10>(int32_t r_out[10]) {
	r_out[0]++;
	r_out[1] += r_out[0] >> 28;
	r_out[2] += r_out[1] >> 28;
	r_out[3] += r_out[2] >> 28;
	r_out[4] += r_out[3] >> 28;
	r_out[5] += r_out[4] >> 28;
	r_out[6] += r_out[5] >> 28;
	r_out[7] += r_out[6] >> 28;
	r_out[8] += r_out[7] >> 28;
	r_out[9] += r_out[8] >> 28;
	r_out[0] &= 0xFFFFFFF;
	r_out[1] &= 0xFFFFFFF;
	r_out[2] &= 0xFFFFFFF;
	r_out[3] &= 0xFFFFFFF;
	r_out[4] &= 0xFFFFFFF;
	r_out[5] &= 0xFFFFFFF;
	r_out[6] &= 0xFFFFFFF;
	r_out[7] &= 0xFFFFFFF;
	r_out[8] &= 0xFFFFFFF;
}

template <>
static void bigint28_add1<15>(int32_t r_out[15]) {
	r_out[0]++;
	r_out[1] += r_out[0] >> 28;
	r_out[2] += r_out[1] >> 28;
	r_out[3] += r_out[2] >> 28;
	r_out[4] += r_out[3] >> 28;
	r_out[5] += r_out[4] >> 28;
	r_out[6] += r_out[5] >> 28;
	r_out[7] += r_out[6] >> 28;
	r_out[8] += r_out[7] >> 28;
	r_out[9] += r_out[8] >> 28;
	r_out[10] += r_out[9] >> 28;
	r_out[11] += r_out[10] >> 28;
	r_out[12] += r_out[11] >> 28;
	r_out[13] += r_out[12] >> 28;
	r_out[14] += r_out[13] >> 28;
	r_out[0] &= 0xFFFFFFF;
	r_out[1] &= 0xFFFFFFF;
	r_out[2] &= 0xFFFFFFF;
	r_out[3] &= 0xFFFFFFF;
	r_out[4] &= 0xFFFFFFF;
	r_out[5] &= 0xFFFFFFF;
	r_out[6] &= 0xFFFFFFF;
	r_out[7] &= 0xFFFFFFF;
	r_out[8] &= 0xFFFFFFF;
	r_out[9] &= 0xFFFFFFF;
	r_out[10] &= 0xFFFFFFF;
	r_out[11] &= 0xFFFFFFF;
	r_out[12] &= 0xFFFFFFF;
	r_out[13] &= 0xFFFFFFF;
}

template <uint32_t digits1, uint32_t digits2>
static void bigint28_add(int32_t r_out[digits1], const int32_t p_in[digits2]);

template <>
static void bigint28_add<10, 10>(int32_t r_out[10], const int32_t p_in[10]) {
	r_out[0] += p_in[0];
	r_out[1] += p_in[1] + (r_out[0] >> 28);
	r_out[2] += p_in[2] + (r_out[1] >> 28);
	r_out[3] += p_in[3] + (r_out[2] >> 28);
	r_out[4] += p_in[4] + (r_out[3] >> 28);
	r_out[5] += p_in[5] + (r_out[4] >> 28);
	r_out[6] += p_in[6] + (r_out[5] >> 28);
	r_out[7] += p_in[7] + (r_out[6] >> 28);
	r_out[8] += p_in[8] + (r_out[7] >> 28);
	r_out[9] += p_in[9] + (r_out[8] >> 28);
	r_out[0] &= 0xFFFFFFF;
	r_out[1] &= 0xFFFFFFF;
	r_out[2] &= 0xFFFFFFF;
	r_out[3] &= 0xFFFFFFF;
	r_out[4] &= 0xFFFFFFF;
	r_out[5] &= 0xFFFFFFF;
	r_out[6] &= 0xFFFFFFF;
	r_out[7] &= 0xFFFFFFF;
	r_out[8] &= 0xFFFFFFF;
}

template <>
static void bigint28_add<10, 5>(int32_t r_out[10], const int32_t p_in[5]) {
	r_out[0] += p_in[0];
	r_out[1] += p_in[1] + (r_out[0] >> 28);
	r_out[2] += p_in[2] + (r_out[1] >> 28);
	r_out[3] += p_in[3] + (r_out[2] >> 28);
	r_out[4] += p_in[4] + (r_out[3] >> 28);
	r_out[5] += r_out[4] >> 28;
	r_out[6] += r_out[5] >> 28;
	r_out[7] += r_out[6] >> 28;
	r_out[8] += r_out[7] >> 28;
	r_out[9] += r_out[8] >> 28;
	r_out[0] &= 0xFFFFFFF;
	r_out[1] &= 0xFFFFFFF;
	r_out[2] &= 0xFFFFFFF;
	r_out[3] &= 0xFFFFFFF;
	r_out[4] &= 0xFFFFFFF;
	r_out[5] &= 0xFFFFFFF;
	r_out[6] &= 0xFFFFFFF;
	r_out[7] &= 0xFFFFFFF;
	r_out[8] &= 0xFFFFFFF;
}

template <>
static void bigint28_add<15, 15>(int32_t r_out[10], const int32_t p_in[15]) {
	r_out[0] += p_in[0];
	r_out[1] += p_in[1] + (r_out[0] >> 28);
	r_out[2] += p_in[2] + (r_out[1] >> 28);
	r_out[3] += p_in[3] + (r_out[2] >> 28);
	r_out[4] += p_in[4] + (r_out[3] >> 28);
	r_out[5] += p_in[5] + (r_out[4] >> 28);
	r_out[6] += p_in[6] + (r_out[5] >> 28);
	r_out[7] += p_in[7] + (r_out[6] >> 28);
	r_out[8] += p_in[8] + (r_out[7] >> 28);
	r_out[9] += p_in[9] + (r_out[8] >> 28);
	r_out[10] += p_in[10] + (r_out[9] >> 28);
	r_out[11] += p_in[11] + (r_out[10] >> 28);
	r_out[12] += p_in[12] + (r_out[11] >> 28);
	r_out[13] += p_in[13] + (r_out[12] >> 28);
	r_out[14] += p_in[14] + (r_out[13] >> 28);
	r_out[0] &= 0xFFFFFFF;
	r_out[1] &= 0xFFFFFFF;
	r_out[2] &= 0xFFFFFFF;
	r_out[3] &= 0xFFFFFFF;
	r_out[4] &= 0xFFFFFFF;
	r_out[5] &= 0xFFFFFFF;
	r_out[6] &= 0xFFFFFFF;
	r_out[7] &= 0xFFFFFFF;
	r_out[8] &= 0xFFFFFFF;
	r_out[9] &= 0xFFFFFFF;
	r_out[10] &= 0xFFFFFFF;
	r_out[11] &= 0xFFFFFFF;
	r_out[12] &= 0xFFFFFFF;
	r_out[13] &= 0xFFFFFFF;
}

template <uint32_t digits1, uint32_t digits2>
static void bigint28_sub(int32_t r_out[digits1], const int32_t p_in[digits2]);

template <>
static void bigint28_sub<5, 5>(int32_t r_out[5], const int32_t p_in[5]) {
	r_out[0] -= p_in[0];
	r_out[1] -= p_in[1] + ((r_out[0] >> 28) & 1);
	r_out[2] -= p_in[2] + ((r_out[1] >> 28) & 1);
	r_out[3] -= p_in[3] + ((r_out[2] >> 28) & 1);
	r_out[4] -= p_in[4] + ((r_out[3] >> 28) & 1);
	r_out[0] &= 0xFFFFFFF;
	r_out[1] &= 0xFFFFFFF;
	r_out[2] &= 0xFFFFFFF;
	r_out[3] &= 0xFFFFFFF;
}

template <>
static void bigint28_sub<10, 10>(int32_t r_out[10], const int32_t p_in[10]) {
	r_out[0] -= p_in[0];
	r_out[1] -= p_in[1] + ((r_out[0] >> 28) & 1);
	r_out[2] -= p_in[2] + ((r_out[1] >> 28) & 1);
	r_out[3] -= p_in[3] + ((r_out[2] >> 28) & 1);
	r_out[4] -= p_in[4] + ((r_out[3] >> 28) & 1);
	r_out[5] -= p_in[5] + ((r_out[4] >> 28) & 1);
	r_out[6] -= p_in[6] + ((r_out[5] >> 28) & 1);
	r_out[7] -= p_in[7] + ((r_out[6] >> 28) & 1);
	r_out[8] -= p_in[8] + ((r_out[7] >> 28) & 1);
	r_out[9] -= p_in[9] + ((r_out[8] >> 28) & 1);
	r_out[0] &= 0xFFFFFFF;
	r_out[1] &= 0xFFFFFFF;
	r_out[2] &= 0xFFFFFFF;
	r_out[3] &= 0xFFFFFFF;
	r_out[4] &= 0xFFFFFFF;
	r_out[5] &= 0xFFFFFFF;
	r_out[6] &= 0xFFFFFFF;
	r_out[7] &= 0xFFFFFFF;
	r_out[8] &= 0xFFFFFFF;
}

template <>
static void bigint28_sub<10, 5>(int32_t r_out[10], const int32_t p_in[5]) {
	r_out[0] -= p_in[0];
	r_out[1] -= p_in[1] + ((r_out[0] >> 28) & 1);
	r_out[2] -= p_in[2] + ((r_out[1] >> 28) & 1);
	r_out[3] -= p_in[3] + ((r_out[2] >> 28) & 1);
	r_out[4] -= p_in[4] + ((r_out[3] >> 28) & 1);
	r_out[5] -= (r_out[4] >> 28) & 1;
	r_out[6] -= (r_out[5] >> 28) & 1;
	r_out[7] -= (r_out[6] >> 28) & 1;
	r_out[8] -= (r_out[7] >> 28) & 1;
	r_out[9] -= (r_out[8] >> 28) & 1;
	r_out[0] &= 0xFFFFFFF;
	r_out[1] &= 0xFFFFFFF;
	r_out[2] &= 0xFFFFFFF;
	r_out[3] &= 0xFFFFFFF;
	r_out[4] &= 0xFFFFFFF;
	r_out[5] &= 0xFFFFFFF;
	r_out[6] &= 0xFFFFFFF;
	r_out[7] &= 0xFFFFFFF;
	r_out[8] &= 0xFFFFFFF;
}

template <>
static void bigint28_sub<15, 15>(int32_t r_out[15], const int32_t p_in[15]) {
	r_out[0] -= p_in[0];
	r_out[1] -= p_in[1] + ((r_out[0] >> 28) & 1);
	r_out[2] -= p_in[2] + ((r_out[1] >> 28) & 1);
	r_out[3] -= p_in[3] + ((r_out[2] >> 28) & 1);
	r_out[4] -= p_in[4] + ((r_out[3] >> 28) & 1);
	r_out[5] -= p_in[5] + ((r_out[4] >> 28) & 1);
	r_out[6] -= p_in[6] + ((r_out[5] >> 28) & 1);
	r_out[7] -= p_in[7] + ((r_out[6] >> 28) & 1);
	r_out[8] -= p_in[8] + ((r_out[7] >> 28) & 1);
	r_out[9] -= p_in[9] + ((r_out[8] >> 28) & 1);
	r_out[10] -= p_in[10] + ((r_out[9] >> 28) & 1);
	r_out[11] -= p_in[11] + ((r_out[10] >> 28) & 1);
	r_out[12] -= p_in[12] + ((r_out[11] >> 28) & 1);
	r_out[13] -= p_in[13] + ((r_out[12] >> 28) & 1);
	r_out[14] -= p_in[14] + ((r_out[13] >> 28) & 1);
	r_out[0] &= 0xFFFFFFF;
	r_out[1] &= 0xFFFFFFF;
	r_out[2] &= 0xFFFFFFF;
	r_out[3] &= 0xFFFFFFF;
	r_out[4] &= 0xFFFFFFF;
	r_out[5] &= 0xFFFFFFF;
	r_out[6] &= 0xFFFFFFF;
	r_out[7] &= 0xFFFFFFF;
	r_out[8] &= 0xFFFFFFF;
	r_out[9] &= 0xFFFFFFF;
	r_out[10] &= 0xFFFFFFF;
	r_out[11] &= 0xFFFFFFF;
	r_out[12] &= 0xFFFFFFF;
	r_out[13] &= 0xFFFFFFF;
}

template <>
static void bigint28_sub<15, 10>(int32_t r_out[15], const int32_t p_in[10]) {
	r_out[0] -= p_in[0];
	r_out[1] -= p_in[1] + ((r_out[0] >> 28) & 1);
	r_out[2] -= p_in[2] + ((r_out[1] >> 28) & 1);
	r_out[3] -= p_in[3] + ((r_out[2] >> 28) & 1);
	r_out[4] -= p_in[4] + ((r_out[3] >> 28) & 1);
	r_out[5] -= p_in[5] + ((r_out[4] >> 28) & 1);
	r_out[6] -= p_in[6] + ((r_out[5] >> 28) & 1);
	r_out[7] -= p_in[7] + ((r_out[6] >> 28) & 1);
	r_out[8] -= p_in[8] + ((r_out[7] >> 28) & 1);
	r_out[9] -= p_in[9] + ((r_out[8] >> 28) & 1);
	r_out[10] -= (r_out[9] >> 28) & 1;
	r_out[11] -= (r_out[10] >> 28) & 1;
	r_out[12] -= (r_out[11] >> 28) & 1;
	r_out[13] -= (r_out[12] >> 28) & 1;
	r_out[14] -= (r_out[13] >> 28) & 1;
	r_out[0] &= 0xFFFFFFF;
	r_out[1] &= 0xFFFFFFF;
	r_out[2] &= 0xFFFFFFF;
	r_out[3] &= 0xFFFFFFF;
	r_out[4] &= 0xFFFFFFF;
	r_out[5] &= 0xFFFFFFF;
	r_out[6] &= 0xFFFFFFF;
	r_out[7] &= 0xFFFFFFF;
	r_out[8] &= 0xFFFFFFF;
	r_out[9] &= 0xFFFFFFF;
	r_out[10] &= 0xFFFFFFF;
	r_out[11] &= 0xFFFFFFF;
	r_out[12] &= 0xFFFFFFF;
	r_out[13] &= 0xFFFFFFF;
}

template <uint32_t digits1, uint32_t digits2>
static void bigint28_sub_positive(int32_t r_out[digits1], const int32_t p_in[digits2]);

template <>
static void bigint28_sub_positive<10, 15>(int32_t r_out[10], const int32_t p_in[15]) {
	DEV_ASSERT(p_in[10] == 0 && p_in[11] == 0 && p_in[12] == 0 && p_in[13] == 0 && p_in[14] == 0);
	r_out[0] -= p_in[0];
	r_out[1] -= p_in[1] + ((r_out[0] >> 28) & 1);
	r_out[2] -= p_in[2] + ((r_out[1] >> 28) & 1);
	r_out[3] -= p_in[3] + ((r_out[2] >> 28) & 1);
	r_out[4] -= p_in[4] + ((r_out[3] >> 28) & 1);
	r_out[5] -= p_in[5] + ((r_out[4] >> 28) & 1);
	r_out[6] -= p_in[6] + ((r_out[5] >> 28) & 1);
	r_out[7] -= p_in[7] + ((r_out[6] >> 28) & 1);
	r_out[8] -= p_in[8] + ((r_out[7] >> 28) & 1);
	r_out[9] -= p_in[9] + ((r_out[8] >> 28) & 1);
	r_out[0] &= 0xFFFFFFF;
	r_out[1] &= 0xFFFFFFF;
	r_out[2] &= 0xFFFFFFF;
	r_out[3] &= 0xFFFFFFF;
	r_out[4] &= 0xFFFFFFF;
	r_out[5] &= 0xFFFFFFF;
	r_out[6] &= 0xFFFFFFF;
	r_out[7] &= 0xFFFFFFF;
	r_out[8] &= 0xFFFFFFF;
}

template <>
static void bigint28_sub_positive<15, 25>(int32_t r_out[15], const int32_t p_in[25]) {
	DEV_ASSERT(p_in[15] == 0 && p_in[16] == 0 && p_in[17] == 0 && p_in[18] == 0 && p_in[19] == 0 && p_in[20] == 0 && p_in[21] == 0 && p_in[22] == 0 && p_in[23] == 0 && p_in[24] == 0);
	r_out[0] -= p_in[0];
	r_out[1] -= p_in[1] + ((r_out[0] >> 28) & 1);
	r_out[2] -= p_in[2] + ((r_out[1] >> 28) & 1);
	r_out[3] -= p_in[3] + ((r_out[2] >> 28) & 1);
	r_out[4] -= p_in[4] + ((r_out[3] >> 28) & 1);
	r_out[5] -= p_in[5] + ((r_out[4] >> 28) & 1);
	r_out[6] -= p_in[6] + ((r_out[5] >> 28) & 1);
	r_out[7] -= p_in[7] + ((r_out[6] >> 28) & 1);
	r_out[8] -= p_in[8] + ((r_out[7] >> 28) & 1);
	r_out[9] -= p_in[9] + ((r_out[8] >> 28) & 1);
	r_out[10] -= p_in[10] + ((r_out[9] >> 28) & 1);
	r_out[11] -= p_in[11] + ((r_out[10] >> 28) & 1);
	r_out[12] -= p_in[12] + ((r_out[11] >> 28) & 1);
	r_out[13] -= p_in[13] + ((r_out[12] >> 28) & 1);
	r_out[14] -= p_in[14] + ((r_out[13] >> 28) & 1);
	r_out[0] &= 0xFFFFFFF;
	r_out[1] &= 0xFFFFFFF;
	r_out[2] &= 0xFFFFFFF;
	r_out[3] &= 0xFFFFFFF;
	r_out[4] &= 0xFFFFFFF;
	r_out[5] &= 0xFFFFFFF;
	r_out[6] &= 0xFFFFFFF;
	r_out[7] &= 0xFFFFFFF;
	r_out[8] &= 0xFFFFFFF;
	r_out[9] &= 0xFFFFFFF;
	r_out[10] &= 0xFFFFFFF;
	r_out[11] &= 0xFFFFFFF;
	r_out[12] &= 0xFFFFFFF;
	r_out[13] &= 0xFFFFFFF;
}

template <uint32_t digits>
static void bigint28_shl1(int32_t r_out[digits]);

template <>
static void bigint28_shl1<5>(int32_t r_out[5]) {
	r_out[4] = (r_out[4] << 1) | (r_out[3] >> 27);
	r_out[3] = ((r_out[3] << 1) | (r_out[2] >> 27)) & 0xFFFFFFF;
	r_out[2] = ((r_out[2] << 1) | (r_out[1] >> 27)) & 0xFFFFFFF;
	r_out[1] = ((r_out[1] << 1) | (r_out[0] >> 27)) & 0xFFFFFFF;
	r_out[0] = (r_out[0] << 1) & 0xFFFFFFF;
}

template <>
static void bigint28_shl1<10>(int32_t r_out[10]) {
	r_out[9] = (r_out[9] << 1) | (r_out[8] >> 27);
	r_out[8] = ((r_out[8] << 1) | (r_out[7] >> 27)) & 0xFFFFFFF;
	r_out[7] = ((r_out[7] << 1) | (r_out[6] >> 27)) & 0xFFFFFFF;
	r_out[6] = ((r_out[6] << 1) | (r_out[5] >> 27)) & 0xFFFFFFF;
	r_out[5] = ((r_out[5] << 1) | (r_out[4] >> 27)) & 0xFFFFFFF;
	r_out[4] = ((r_out[4] << 1) | (r_out[3] >> 27)) & 0xFFFFFFF;
	r_out[3] = ((r_out[3] << 1) | (r_out[2] >> 27)) & 0xFFFFFFF;
	r_out[2] = ((r_out[2] << 1) | (r_out[1] >> 27)) & 0xFFFFFFF;
	r_out[1] = ((r_out[1] << 1) | (r_out[0] >> 27)) & 0xFFFFFFF;
	r_out[0] = (r_out[0] << 1) & 0xFFFFFFF;
}

template <uint32_t digits>
static void bigint28_shr(int32_t r_out[digits], int p_amount);

template <>
static void bigint28_shr<10>(int32_t r_out[10], int p_amount) {
	if (p_amount >= 140) {
		if (p_amount >= 196) {
			if (p_amount >= 252) {
				r_out[0] = r_out[9] >> (p_amount - 252);
				r_out[1] = r_out[2] = r_out[3] = r_out[4] = r_out[5] = r_out[6] = r_out[7] = r_out[8] = r_out[9] = (r_out[9] >> 31);
			} else if (p_amount >= 224) {
				r_out[0] = ((r_out[9] << (252 - p_amount)) & 0xFFFFFFF) | (r_out[8] >> (p_amount - 224));
				r_out[1] = r_out[9] >> (p_amount - 224);
				r_out[2] = r_out[3] = r_out[4] = r_out[5] = r_out[6] = r_out[7] = r_out[8] = r_out[9] = (r_out[9] >> 31);
			} else {
				r_out[0] = ((r_out[8] << (224 - p_amount)) & 0xFFFFFFF) | (r_out[7] >> (p_amount - 196));
				r_out[1] = ((r_out[9] << (224 - p_amount)) & 0xFFFFFFF) | (r_out[8] >> (p_amount - 196));
				r_out[2] = r_out[9] >> (p_amount - 196);
				r_out[3] = r_out[4] = r_out[5] = r_out[6] = r_out[7] = r_out[8] = r_out[9] = (r_out[9] >> 31);
			}
		} else if (p_amount >= 168) {
			r_out[0] = ((r_out[7] << (196 - p_amount)) & 0xFFFFFFF) | (r_out[6] >> (p_amount - 168));
			r_out[1] = ((r_out[8] << (196 - p_amount)) & 0xFFFFFFF) | (r_out[7] >> (p_amount - 168));
			r_out[2] = ((r_out[9] << (196 - p_amount)) & 0xFFFFFFF) | (r_out[8] >> (p_amount - 168));
			r_out[3] = r_out[9] >> (p_amount - 168);
			r_out[4] = r_out[5] = r_out[6] = r_out[7] = r_out[8] = r_out[9] = (r_out[9] >> 31);
		} else {
			r_out[0] = ((r_out[6] << (168 - p_amount)) & 0xFFFFFFF) | (r_out[5] >> (p_amount - 140));
			r_out[1] = ((r_out[7] << (168 - p_amount)) & 0xFFFFFFF) | (r_out[6] >> (p_amount - 140));
			r_out[2] = ((r_out[8] << (168 - p_amount)) & 0xFFFFFFF) | (r_out[7] >> (p_amount - 140));
			r_out[3] = ((r_out[9] << (168 - p_amount)) & 0xFFFFFFF) | (r_out[8] >> (p_amount - 140));
			r_out[4] = r_out[9] >> (p_amount - 140);
			r_out[5] = r_out[6] = r_out[7] = r_out[8] = r_out[9] = (r_out[9] >> 31);
		}
	} else if (p_amount >= 56) {
		if (p_amount >= 112) {
			r_out[0] = ((r_out[5] << (140 - p_amount)) & 0xFFFFFFF) | (r_out[4] >> (p_amount - 112));
			r_out[1] = ((r_out[6] << (140 - p_amount)) & 0xFFFFFFF) | (r_out[5] >> (p_amount - 112));
			r_out[2] = ((r_out[7] << (140 - p_amount)) & 0xFFFFFFF) | (r_out[6] >> (p_amount - 112));
			r_out[3] = ((r_out[8] << (140 - p_amount)) & 0xFFFFFFF) | (r_out[7] >> (p_amount - 112));
			r_out[4] = ((r_out[9] << (140 - p_amount)) & 0xFFFFFFF) | (r_out[8] >> (p_amount - 112));
			r_out[5] = r_out[9] >> (p_amount - 112);
			r_out[6] = r_out[7] = r_out[8] = r_out[9] = (r_out[9] >> 31);
		} else if (p_amount >= 84) {
			r_out[0] = ((r_out[4] << (112 - p_amount)) & 0xFFFFFFF) | (r_out[3] >> (p_amount - 84));
			r_out[1] = ((r_out[5] << (112 - p_amount)) & 0xFFFFFFF) | (r_out[4] >> (p_amount - 84));
			r_out[2] = ((r_out[6] << (112 - p_amount)) & 0xFFFFFFF) | (r_out[5] >> (p_amount - 84));
			r_out[3] = ((r_out[7] << (112 - p_amount)) & 0xFFFFFFF) | (r_out[6] >> (p_amount - 84));
			r_out[4] = ((r_out[8] << (112 - p_amount)) & 0xFFFFFFF) | (r_out[7] >> (p_amount - 84));
			r_out[5] = ((r_out[9] << (112 - p_amount)) & 0xFFFFFFF) | (r_out[8] >> (p_amount - 84));
			r_out[6] = r_out[9] >> (p_amount - 84);
			r_out[7] = r_out[8] = r_out[9] = (r_out[9] >> 31);
		} else {
			r_out[0] = ((r_out[3] << (84 - p_amount)) & 0xFFFFFFF) | (r_out[2] >> (p_amount - 56));
			r_out[1] = ((r_out[4] << (84 - p_amount)) & 0xFFFFFFF) | (r_out[3] >> (p_amount - 56));
			r_out[2] = ((r_out[5] << (84 - p_amount)) & 0xFFFFFFF) | (r_out[4] >> (p_amount - 56));
			r_out[3] = ((r_out[6] << (84 - p_amount)) & 0xFFFFFFF) | (r_out[5] >> (p_amount - 56));
			r_out[4] = ((r_out[7] << (84 - p_amount)) & 0xFFFFFFF) | (r_out[6] >> (p_amount - 56));
			r_out[5] = ((r_out[8] << (84 - p_amount)) & 0xFFFFFFF) | (r_out[7] >> (p_amount - 56));
			r_out[6] = ((r_out[9] << (84 - p_amount)) & 0xFFFFFFF) | (r_out[8] >> (p_amount - 56));
			r_out[7] = r_out[9] >> (p_amount - 56);
			r_out[8] = r_out[9] = (r_out[9] >> 31);
		}
	} else if (p_amount >= 28) {
		r_out[0] = ((r_out[2] << (56 - p_amount)) & 0xFFFFFFF) | (r_out[1] >> (p_amount - 28));
		r_out[1] = ((r_out[3] << (56 - p_amount)) & 0xFFFFFFF) | (r_out[2] >> (p_amount - 28));
		r_out[2] = ((r_out[4] << (56 - p_amount)) & 0xFFFFFFF) | (r_out[3] >> (p_amount - 28));
		r_out[3] = ((r_out[5] << (56 - p_amount)) & 0xFFFFFFF) | (r_out[4] >> (p_amount - 28));
		r_out[4] = ((r_out[6] << (56 - p_amount)) & 0xFFFFFFF) | (r_out[5] >> (p_amount - 28));
		r_out[5] = ((r_out[7] << (56 - p_amount)) & 0xFFFFFFF) | (r_out[6] >> (p_amount - 28));
		r_out[6] = ((r_out[8] << (56 - p_amount)) & 0xFFFFFFF) | (r_out[7] >> (p_amount - 28));
		r_out[7] = ((r_out[9] << (56 - p_amount)) & 0xFFFFFFF) | (r_out[8] >> (p_amount - 28));
		r_out[8] = r_out[9] >> (p_amount - 28);
		r_out[9] = (r_out[9] >> 31);
	} else {
		r_out[0] = ((r_out[1] << (28 - p_amount)) & 0xFFFFFFF) | (r_out[0] >> p_amount);
		r_out[1] = ((r_out[2] << (28 - p_amount)) & 0xFFFFFFF) | (r_out[1] >> p_amount);
		r_out[2] = ((r_out[3] << (28 - p_amount)) & 0xFFFFFFF) | (r_out[2] >> p_amount);
		r_out[3] = ((r_out[4] << (28 - p_amount)) & 0xFFFFFFF) | (r_out[3] >> p_amount);
		r_out[4] = ((r_out[5] << (28 - p_amount)) & 0xFFFFFFF) | (r_out[4] >> p_amount);
		r_out[5] = ((r_out[6] << (28 - p_amount)) & 0xFFFFFFF) | (r_out[5] >> p_amount);
		r_out[6] = ((r_out[7] << (28 - p_amount)) & 0xFFFFFFF) | (r_out[6] >> p_amount);
		r_out[7] = ((r_out[8] << (28 - p_amount)) & 0xFFFFFFF) | (r_out[7] >> p_amount);
		r_out[8] = ((r_out[9] << (28 - p_amount)) & 0xFFFFFFF) | (r_out[8] >> p_amount);
		r_out[9] = r_out[9] >> p_amount;
	}
}

template <>
static void bigint28_shr<15>(int32_t r_out[15], int p_amount) {
	if (p_amount >= 196) {
		if (p_amount >= 308) {
			if (p_amount >= 364) {
				if (p_amount >= 392) {
					r_out[0] = r_out[14] >> (p_amount - 392);
					r_out[1] = r_out[2] = r_out[3] = r_out[4] = r_out[5] = r_out[6] = r_out[7] = r_out[8] = r_out[9] = r_out[10] = r_out[11] = r_out[12] = r_out[13] = r_out[14] = (r_out[14] >> 31);
				} else {
					r_out[0] = ((r_out[14] << (392 - p_amount)) & 0xFFFFFFF) | (r_out[13] >> (p_amount - 364));
					r_out[1] = r_out[14] >> (p_amount - 364);
					r_out[2] = r_out[3] = r_out[4] = r_out[5] = r_out[6] = r_out[7] = r_out[8] = r_out[9] = r_out[10] = r_out[11] = r_out[12] = r_out[13] = r_out[14] = (r_out[14] >> 31);
				}
			} else if (p_amount >= 336) {
				r_out[0] = ((r_out[13] << (364 - p_amount)) & 0xFFFFFFF) | (r_out[12] >> (p_amount - 336));
				r_out[1] = ((r_out[14] << (364 - p_amount)) & 0xFFFFFFF) | (r_out[13] >> (p_amount - 336));
				r_out[2] = r_out[14] >> (p_amount - 336);
				r_out[3] = r_out[4] = r_out[5] = r_out[6] = r_out[7] = r_out[8] = r_out[9] = r_out[10] = r_out[11] = r_out[12] = r_out[13] = r_out[14] = (r_out[14] >> 31);
			} else {
				r_out[0] = ((r_out[12] << (336 - p_amount)) & 0xFFFFFFF) | (r_out[11] >> (p_amount - 308));
				r_out[1] = ((r_out[13] << (336 - p_amount)) & 0xFFFFFFF) | (r_out[12] >> (p_amount - 308));
				r_out[2] = ((r_out[14] << (336 - p_amount)) & 0xFFFFFFF) | (r_out[13] >> (p_amount - 308));
				r_out[3] = r_out[14] >> (p_amount - 308);
				r_out[4] = r_out[5] = r_out[6] = r_out[7] = r_out[8] = r_out[9] = r_out[10] = r_out[11] = r_out[12] = r_out[13] = r_out[14] = (r_out[14] >> 31);
			}
		} else if (p_amount >= 252) {
			if (p_amount >= 280) {
				r_out[0] = ((r_out[11] << (308 - p_amount)) & 0xFFFFFFF) | (r_out[10] >> (p_amount - 280));
				r_out[1] = ((r_out[12] << (308 - p_amount)) & 0xFFFFFFF) | (r_out[11] >> (p_amount - 280));
				r_out[2] = ((r_out[13] << (308 - p_amount)) & 0xFFFFFFF) | (r_out[12] >> (p_amount - 280));
				r_out[3] = ((r_out[14] << (308 - p_amount)) & 0xFFFFFFF) | (r_out[13] >> (p_amount - 280));
				r_out[4] = r_out[14] >> (p_amount - 280);
				r_out[5] = r_out[6] = r_out[7] = r_out[8] = r_out[9] = r_out[10] = r_out[11] = r_out[12] = r_out[13] = r_out[14] = (r_out[14] >> 31);
			} else {
				r_out[0] = ((r_out[10] << (280 - p_amount)) & 0xFFFFFFF) | (r_out[9] >> (p_amount - 252));
				r_out[1] = ((r_out[11] << (280 - p_amount)) & 0xFFFFFFF) | (r_out[10] >> (p_amount - 252));
				r_out[2] = ((r_out[12] << (280 - p_amount)) & 0xFFFFFFF) | (r_out[11] >> (p_amount - 252));
				r_out[3] = ((r_out[13] << (280 - p_amount)) & 0xFFFFFFF) | (r_out[12] >> (p_amount - 252));
				r_out[4] = ((r_out[14] << (280 - p_amount)) & 0xFFFFFFF) | (r_out[13] >> (p_amount - 252));
				r_out[5] = r_out[14] >> (p_amount - 252);
				r_out[6] = r_out[7] = r_out[8] = r_out[9] = r_out[10] = r_out[11] = r_out[12] = r_out[13] = r_out[14] = (r_out[14] >> 31);
			}
		} else if (p_amount >= 224) {
			r_out[0] = ((r_out[9] << (252 - p_amount)) & 0xFFFFFFF) | (r_out[8] >> (p_amount - 224));
			r_out[1] = ((r_out[10] << (252 - p_amount)) & 0xFFFFFFF) | (r_out[9] >> (p_amount - 224));
			r_out[2] = ((r_out[11] << (252 - p_amount)) & 0xFFFFFFF) | (r_out[10] >> (p_amount - 224));
			r_out[3] = ((r_out[12] << (252 - p_amount)) & 0xFFFFFFF) | (r_out[11] >> (p_amount - 224));
			r_out[4] = ((r_out[13] << (252 - p_amount)) & 0xFFFFFFF) | (r_out[12] >> (p_amount - 224));
			r_out[5] = ((r_out[14] << (252 - p_amount)) & 0xFFFFFFF) | (r_out[13] >> (p_amount - 224));
			r_out[6] = r_out[14] >> (p_amount - 224);
			r_out[7] = r_out[8] = r_out[9] = r_out[10] = r_out[11] = r_out[12] = r_out[13] = r_out[14] = (r_out[14] >> 31);
		} else {
			r_out[0] = ((r_out[8] << (224 - p_amount)) & 0xFFFFFFF) | (r_out[7] >> (p_amount - 196));
			r_out[1] = ((r_out[9] << (224 - p_amount)) & 0xFFFFFFF) | (r_out[8] >> (p_amount - 196));
			r_out[2] = ((r_out[10] << (224 - p_amount)) & 0xFFFFFFF) | (r_out[9] >> (p_amount - 196));
			r_out[3] = ((r_out[11] << (224 - p_amount)) & 0xFFFFFFF) | (r_out[10] >> (p_amount - 196));
			r_out[4] = ((r_out[12] << (224 - p_amount)) & 0xFFFFFFF) | (r_out[11] >> (p_amount - 196));
			r_out[5] = ((r_out[13] << (224 - p_amount)) & 0xFFFFFFF) | (r_out[12] >> (p_amount - 196));
			r_out[6] = ((r_out[14] << (224 - p_amount)) & 0xFFFFFFF) | (r_out[13] >> (p_amount - 196));
			r_out[7] = r_out[14] >> (p_amount - 196);
			r_out[8] = r_out[9] = r_out[10] = r_out[11] = r_out[12] = r_out[13] = r_out[14] = (r_out[14] >> 31);
		}
	} else if (p_amount >= 84) {
		if (p_amount >= 140) {
			if (p_amount >= 168) {
				r_out[0] = ((r_out[7] << (196 - p_amount)) & 0xFFFFFFF) | (r_out[6] >> (p_amount - 168));
				r_out[1] = ((r_out[8] << (196 - p_amount)) & 0xFFFFFFF) | (r_out[7] >> (p_amount - 168));
				r_out[2] = ((r_out[9] << (196 - p_amount)) & 0xFFFFFFF) | (r_out[8] >> (p_amount - 168));
				r_out[3] = ((r_out[10] << (196 - p_amount)) & 0xFFFFFFF) | (r_out[9] >> (p_amount - 168));
				r_out[4] = ((r_out[11] << (196 - p_amount)) & 0xFFFFFFF) | (r_out[10] >> (p_amount - 168));
				r_out[5] = ((r_out[12] << (196 - p_amount)) & 0xFFFFFFF) | (r_out[11] >> (p_amount - 168));
				r_out[6] = ((r_out[13] << (196 - p_amount)) & 0xFFFFFFF) | (r_out[12] >> (p_amount - 168));
				r_out[7] = ((r_out[14] << (196 - p_amount)) & 0xFFFFFFF) | (r_out[13] >> (p_amount - 168));
				r_out[8] = r_out[14] >> (p_amount - 168);
				r_out[9] = r_out[10] = r_out[11] = r_out[12] = r_out[13] = r_out[14] = (r_out[14] >> 31);
			} else {
				r_out[0] = ((r_out[6] << (168 - p_amount)) & 0xFFFFFFF) | (r_out[5] >> (p_amount - 140));
				r_out[1] = ((r_out[7] << (168 - p_amount)) & 0xFFFFFFF) | (r_out[6] >> (p_amount - 140));
				r_out[2] = ((r_out[8] << (168 - p_amount)) & 0xFFFFFFF) | (r_out[7] >> (p_amount - 140));
				r_out[3] = ((r_out[9] << (168 - p_amount)) & 0xFFFFFFF) | (r_out[8] >> (p_amount - 140));
				r_out[4] = ((r_out[10] << (168 - p_amount)) & 0xFFFFFFF) | (r_out[9] >> (p_amount - 140));
				r_out[5] = ((r_out[11] << (168 - p_amount)) & 0xFFFFFFF) | (r_out[10] >> (p_amount - 140));
				r_out[6] = ((r_out[12] << (168 - p_amount)) & 0xFFFFFFF) | (r_out[11] >> (p_amount - 140));
				r_out[7] = ((r_out[13] << (168 - p_amount)) & 0xFFFFFFF) | (r_out[12] >> (p_amount - 140));
				r_out[8] = ((r_out[14] << (168 - p_amount)) & 0xFFFFFFF) | (r_out[13] >> (p_amount - 140));
				r_out[9] = r_out[14] >> (p_amount - 140);
				r_out[10] = r_out[11] = r_out[12] = r_out[13] = r_out[14] = (r_out[14] >> 31);
			}
		} else if (p_amount >= 112) {
			r_out[0] = ((r_out[5] << (140 - p_amount)) & 0xFFFFFFF) | (r_out[4] >> (p_amount - 112));
			r_out[1] = ((r_out[6] << (140 - p_amount)) & 0xFFFFFFF) | (r_out[5] >> (p_amount - 112));
			r_out[2] = ((r_out[7] << (140 - p_amount)) & 0xFFFFFFF) | (r_out[6] >> (p_amount - 112));
			r_out[3] = ((r_out[8] << (140 - p_amount)) & 0xFFFFFFF) | (r_out[7] >> (p_amount - 112));
			r_out[4] = ((r_out[9] << (140 - p_amount)) & 0xFFFFFFF) | (r_out[8] >> (p_amount - 112));
			r_out[5] = ((r_out[10] << (140 - p_amount)) & 0xFFFFFFF) | (r_out[9] >> (p_amount - 112));
			r_out[6] = ((r_out[11] << (140 - p_amount)) & 0xFFFFFFF) | (r_out[10] >> (p_amount - 112));
			r_out[7] = ((r_out[12] << (140 - p_amount)) & 0xFFFFFFF) | (r_out[11] >> (p_amount - 112));
			r_out[8] = ((r_out[13] << (140 - p_amount)) & 0xFFFFFFF) | (r_out[12] >> (p_amount - 112));
			r_out[9] = ((r_out[14] << (140 - p_amount)) & 0xFFFFFFF) | (r_out[13] >> (p_amount - 112));
			r_out[10] = r_out[14] >> (p_amount - 112);
			r_out[11] = r_out[12] = r_out[13] = r_out[14] = (r_out[14] >> 31);
		} else {
			r_out[0] = ((r_out[4] << (112 - p_amount)) & 0xFFFFFFF) | (r_out[3] >> (p_amount - 84));
			r_out[1] = ((r_out[5] << (112 - p_amount)) & 0xFFFFFFF) | (r_out[4] >> (p_amount - 84));
			r_out[2] = ((r_out[6] << (112 - p_amount)) & 0xFFFFFFF) | (r_out[5] >> (p_amount - 84));
			r_out[3] = ((r_out[7] << (112 - p_amount)) & 0xFFFFFFF) | (r_out[6] >> (p_amount - 84));
			r_out[4] = ((r_out[8] << (112 - p_amount)) & 0xFFFFFFF) | (r_out[7] >> (p_amount - 84));
			r_out[5] = ((r_out[9] << (112 - p_amount)) & 0xFFFFFFF) | (r_out[8] >> (p_amount - 84));
			r_out[6] = ((r_out[10] << (112 - p_amount)) & 0xFFFFFFF) | (r_out[9] >> (p_amount - 84));
			r_out[7] = ((r_out[11] << (112 - p_amount)) & 0xFFFFFFF) | (r_out[10] >> (p_amount - 84));
			r_out[8] = ((r_out[12] << (112 - p_amount)) & 0xFFFFFFF) | (r_out[11] >> (p_amount - 84));
			r_out[9] = ((r_out[13] << (112 - p_amount)) & 0xFFFFFFF) | (r_out[12] >> (p_amount - 84));
			r_out[10] = ((r_out[14] << (112 - p_amount)) & 0xFFFFFFF) | (r_out[13] >> (p_amount - 84));
			r_out[11] = r_out[14] >> (p_amount - 84);
			r_out[12] = r_out[13] = r_out[14] = (r_out[14] >> 31);
		}
	} else if (p_amount >= 56) {
		r_out[0] = ((r_out[3] << (84 - p_amount)) & 0xFFFFFFF) | (r_out[2] >> (p_amount - 56));
		r_out[1] = ((r_out[4] << (84 - p_amount)) & 0xFFFFFFF) | (r_out[3] >> (p_amount - 56));
		r_out[2] = ((r_out[5] << (84 - p_amount)) & 0xFFFFFFF) | (r_out[4] >> (p_amount - 56));
		r_out[3] = ((r_out[6] << (84 - p_amount)) & 0xFFFFFFF) | (r_out[5] >> (p_amount - 56));
		r_out[4] = ((r_out[7] << (84 - p_amount)) & 0xFFFFFFF) | (r_out[6] >> (p_amount - 56));
		r_out[5] = ((r_out[8] << (84 - p_amount)) & 0xFFFFFFF) | (r_out[7] >> (p_amount - 56));
		r_out[6] = ((r_out[9] << (84 - p_amount)) & 0xFFFFFFF) | (r_out[8] >> (p_amount - 56));
		r_out[7] = ((r_out[10] << (84 - p_amount)) & 0xFFFFFFF) | (r_out[9] >> (p_amount - 56));
		r_out[8] = ((r_out[11] << (84 - p_amount)) & 0xFFFFFFF) | (r_out[10] >> (p_amount - 56));
		r_out[9] = ((r_out[12] << (84 - p_amount)) & 0xFFFFFFF) | (r_out[11] >> (p_amount - 56));
		r_out[10] = ((r_out[13] << (84 - p_amount)) & 0xFFFFFFF) | (r_out[12] >> (p_amount - 56));
		r_out[11] = ((r_out[14] << (84 - p_amount)) & 0xFFFFFFF) | (r_out[13] >> (p_amount - 56));
		r_out[12] = r_out[14] >> (p_amount - 56);
		r_out[13] = r_out[14] = (r_out[14] >> 31);
	} else if (p_amount >= 28) {
		r_out[0] = ((r_out[2] << (56 - p_amount)) & 0xFFFFFFF) | (r_out[1] >> (p_amount - 28));
		r_out[1] = ((r_out[3] << (56 - p_amount)) & 0xFFFFFFF) | (r_out[2] >> (p_amount - 28));
		r_out[2] = ((r_out[4] << (56 - p_amount)) & 0xFFFFFFF) | (r_out[3] >> (p_amount - 28));
		r_out[3] = ((r_out[5] << (56 - p_amount)) & 0xFFFFFFF) | (r_out[4] >> (p_amount - 28));
		r_out[4] = ((r_out[6] << (56 - p_amount)) & 0xFFFFFFF) | (r_out[5] >> (p_amount - 28));
		r_out[5] = ((r_out[7] << (56 - p_amount)) & 0xFFFFFFF) | (r_out[6] >> (p_amount - 28));
		r_out[6] = ((r_out[8] << (56 - p_amount)) & 0xFFFFFFF) | (r_out[7] >> (p_amount - 28));
		r_out[7] = ((r_out[9] << (56 - p_amount)) & 0xFFFFFFF) | (r_out[8] >> (p_amount - 28));
		r_out[8] = ((r_out[10] << (56 - p_amount)) & 0xFFFFFFF) | (r_out[9] >> (p_amount - 28));
		r_out[9] = ((r_out[11] << (56 - p_amount)) & 0xFFFFFFF) | (r_out[10] >> (p_amount - 28));
		r_out[10] = ((r_out[12] << (56 - p_amount)) & 0xFFFFFFF) | (r_out[11] >> (p_amount - 28));
		r_out[11] = ((r_out[13] << (56 - p_amount)) & 0xFFFFFFF) | (r_out[12] >> (p_amount - 28));
		r_out[12] = ((r_out[14] << (56 - p_amount)) & 0xFFFFFFF) | (r_out[13] >> (p_amount - 28));
		r_out[13] = r_out[14] >> (p_amount - 28);
		r_out[14] = (r_out[14] >> 31);
	} else {
		r_out[0] = ((r_out[1] << (28 - p_amount)) & 0xFFFFFFF) | (r_out[0] >> p_amount);
		r_out[1] = ((r_out[2] << (28 - p_amount)) & 0xFFFFFFF) | (r_out[1] >> p_amount);
		r_out[2] = ((r_out[3] << (28 - p_amount)) & 0xFFFFFFF) | (r_out[2] >> p_amount);
		r_out[3] = ((r_out[4] << (28 - p_amount)) & 0xFFFFFFF) | (r_out[3] >> p_amount);
		r_out[4] = ((r_out[5] << (28 - p_amount)) & 0xFFFFFFF) | (r_out[4] >> p_amount);
		r_out[5] = ((r_out[6] << (28 - p_amount)) & 0xFFFFFFF) | (r_out[5] >> p_amount);
		r_out[6] = ((r_out[7] << (28 - p_amount)) & 0xFFFFFFF) | (r_out[6] >> p_amount);
		r_out[7] = ((r_out[8] << (28 - p_amount)) & 0xFFFFFFF) | (r_out[7] >> p_amount);
		r_out[8] = ((r_out[9] << (28 - p_amount)) & 0xFFFFFFF) | (r_out[8] >> p_amount);
		r_out[9] = ((r_out[10] << (28 - p_amount)) & 0xFFFFFFF) | (r_out[9] >> p_amount);
		r_out[10] = ((r_out[11] << (28 - p_amount)) & 0xFFFFFFF) | (r_out[10] >> p_amount);
		r_out[11] = ((r_out[12] << (28 - p_amount)) & 0xFFFFFFF) | (r_out[11] >> p_amount);
		r_out[12] = ((r_out[13] << (28 - p_amount)) & 0xFFFFFFF) | (r_out[12] >> p_amount);
		r_out[13] = ((r_out[14] << (28 - p_amount)) & 0xFFFFFFF) | (r_out[13] >> p_amount);
		r_out[14] = r_out[14] >> p_amount;
	}
}

template <uint32_t digits1, uint32_t digits2>
static void bigint28_mul_positive(int32_t r_out[digits1 + digits2], const int32_t p_in1[digits1], const int32_t p_in2[digits2]);

template <>
static void bigint28_mul_positive<5, 5>(int32_t r_out[10], const int32_t p_in1[5], const int32_t p_in2[5]) {
	DEV_ASSERT(p_in1[4] >= 0 && p_in2[4] >= 0);
	uint64_t v = static_cast<uint64_t>(p_in1[0]) * static_cast<uint64_t>(p_in2[0]);
	r_out[0] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[1]) * static_cast<uint64_t>(p_in2[0]) + static_cast<uint64_t>(p_in1[0]) * static_cast<uint64_t>(p_in2[1]) + (v >> 28);
	r_out[1] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[2]) * static_cast<uint64_t>(p_in2[0]) + static_cast<uint64_t>(p_in1[1]) * static_cast<uint64_t>(p_in2[1]) + static_cast<uint64_t>(p_in1[0]) * static_cast<uint64_t>(p_in2[2]) + (v >> 28);
	r_out[2] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[3]) * static_cast<uint64_t>(p_in2[0]) + static_cast<uint64_t>(p_in1[2]) * static_cast<uint64_t>(p_in2[1]) + static_cast<uint64_t>(p_in1[1]) * static_cast<uint64_t>(p_in2[2]) + static_cast<uint64_t>(p_in1[0]) * static_cast<uint64_t>(p_in2[3]) + (v >> 28);
	r_out[3] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[4]) * static_cast<uint64_t>(p_in2[0]) + static_cast<uint64_t>(p_in1[3]) * static_cast<uint64_t>(p_in2[1]) + static_cast<uint64_t>(p_in1[2]) * static_cast<uint64_t>(p_in2[2]) + static_cast<uint64_t>(p_in1[1]) * static_cast<uint64_t>(p_in2[3]) + static_cast<uint64_t>(p_in1[0]) * static_cast<uint64_t>(p_in2[4]) + (v >> 28);
	r_out[4] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[4]) * static_cast<uint64_t>(p_in2[1]) + static_cast<uint64_t>(p_in1[3]) * static_cast<uint64_t>(p_in2[2]) + static_cast<uint64_t>(p_in1[2]) * static_cast<uint64_t>(p_in2[3]) + static_cast<uint64_t>(p_in1[1]) * static_cast<uint64_t>(p_in2[4]) + (v >> 28);
	r_out[5] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[4]) * static_cast<uint64_t>(p_in2[2]) + static_cast<uint64_t>(p_in1[3]) * static_cast<uint64_t>(p_in2[3]) + static_cast<uint64_t>(p_in1[2]) * static_cast<uint64_t>(p_in2[4]) + (v >> 28);
	r_out[6] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[4]) * static_cast<uint64_t>(p_in2[3]) + static_cast<uint64_t>(p_in1[3]) * static_cast<uint64_t>(p_in2[4]) + (v >> 28);
	r_out[7] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[4]) * static_cast<uint64_t>(p_in2[4]) + (v >> 28);
	r_out[8] = static_cast<int32_t>(v & 0xFFFFFFF);
	r_out[9] = static_cast<int32_t>(v >> 28);
}

template <>
static void bigint28_mul_positive<10, 5>(int32_t r_out[15], const int32_t p_in1[10], const int32_t p_in2[5]) {
	DEV_ASSERT(p_in1[9] >= 0 && p_in2[4] >= 0);
	uint64_t v = static_cast<uint64_t>(p_in1[0]) * static_cast<uint64_t>(p_in2[0]);
	r_out[0] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[1]) * static_cast<uint64_t>(p_in2[0]) + static_cast<uint64_t>(p_in1[0]) * static_cast<uint64_t>(p_in2[1]) + (v >> 28);
	r_out[1] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[2]) * static_cast<uint64_t>(p_in2[0]) + static_cast<uint64_t>(p_in1[1]) * static_cast<uint64_t>(p_in2[1]) + static_cast<uint64_t>(p_in1[0]) * static_cast<uint64_t>(p_in2[2]) + (v >> 28);
	r_out[2] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[3]) * static_cast<uint64_t>(p_in2[0]) + static_cast<uint64_t>(p_in1[2]) * static_cast<uint64_t>(p_in2[1]) + static_cast<uint64_t>(p_in1[1]) * static_cast<uint64_t>(p_in2[2]) + static_cast<uint64_t>(p_in1[0]) * static_cast<uint64_t>(p_in2[3]) + (v >> 28);
	r_out[3] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[4]) * static_cast<uint64_t>(p_in2[0]) + static_cast<uint64_t>(p_in1[3]) * static_cast<uint64_t>(p_in2[1]) + static_cast<uint64_t>(p_in1[2]) * static_cast<uint64_t>(p_in2[2]) + static_cast<uint64_t>(p_in1[1]) * static_cast<uint64_t>(p_in2[3]) + static_cast<uint64_t>(p_in1[0]) * static_cast<uint64_t>(p_in2[4]) + (v >> 28);
	r_out[4] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[5]) * static_cast<uint64_t>(p_in2[0]) + static_cast<uint64_t>(p_in1[4]) * static_cast<uint64_t>(p_in2[1]) + static_cast<uint64_t>(p_in1[3]) * static_cast<uint64_t>(p_in2[2]) + static_cast<uint64_t>(p_in1[2]) * static_cast<uint64_t>(p_in2[3]) + static_cast<uint64_t>(p_in1[1]) * static_cast<uint64_t>(p_in2[4]) + (v >> 28);
	r_out[5] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[6]) * static_cast<uint64_t>(p_in2[0]) + static_cast<uint64_t>(p_in1[5]) * static_cast<uint64_t>(p_in2[1]) + static_cast<uint64_t>(p_in1[4]) * static_cast<uint64_t>(p_in2[2]) + static_cast<uint64_t>(p_in1[3]) * static_cast<uint64_t>(p_in2[3]) + static_cast<uint64_t>(p_in1[2]) * static_cast<uint64_t>(p_in2[4]) + (v >> 28);
	r_out[6] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[7]) * static_cast<uint64_t>(p_in2[0]) + static_cast<uint64_t>(p_in1[6]) * static_cast<uint64_t>(p_in2[1]) + static_cast<uint64_t>(p_in1[5]) * static_cast<uint64_t>(p_in2[2]) + static_cast<uint64_t>(p_in1[4]) * static_cast<uint64_t>(p_in2[3]) + static_cast<uint64_t>(p_in1[3]) * static_cast<uint64_t>(p_in2[4]) + (v >> 28);
	r_out[7] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[8]) * static_cast<uint64_t>(p_in2[0]) + static_cast<uint64_t>(p_in1[7]) * static_cast<uint64_t>(p_in2[1]) + static_cast<uint64_t>(p_in1[6]) * static_cast<uint64_t>(p_in2[2]) + static_cast<uint64_t>(p_in1[5]) * static_cast<uint64_t>(p_in2[3]) + static_cast<uint64_t>(p_in1[4]) * static_cast<uint64_t>(p_in2[4]) + (v >> 28);
	r_out[8] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[9]) * static_cast<uint64_t>(p_in2[0]) + static_cast<uint64_t>(p_in1[8]) * static_cast<uint64_t>(p_in2[1]) + static_cast<uint64_t>(p_in1[7]) * static_cast<uint64_t>(p_in2[2]) + static_cast<uint64_t>(p_in1[6]) * static_cast<uint64_t>(p_in2[3]) + static_cast<uint64_t>(p_in1[5]) * static_cast<uint64_t>(p_in2[4]) + (v >> 28);
	r_out[9] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[9]) * static_cast<uint64_t>(p_in2[1]) + static_cast<uint64_t>(p_in1[8]) * static_cast<uint64_t>(p_in2[2]) + static_cast<uint64_t>(p_in1[7]) * static_cast<uint64_t>(p_in2[3]) + static_cast<uint64_t>(p_in1[6]) * static_cast<uint64_t>(p_in2[4]) + (v >> 28);
	r_out[10] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[9]) * static_cast<uint64_t>(p_in2[2]) + static_cast<uint64_t>(p_in1[8]) * static_cast<uint64_t>(p_in2[3]) + static_cast<uint64_t>(p_in1[7]) * static_cast<uint64_t>(p_in2[4]) + (v >> 28);
	r_out[11] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[9]) * static_cast<uint64_t>(p_in2[3]) + static_cast<uint64_t>(p_in1[8]) * static_cast<uint64_t>(p_in2[4]) + (v >> 28);
	r_out[12] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[9]) * static_cast<uint64_t>(p_in2[4]) + (v >> 28);
	r_out[13] = static_cast<int32_t>(v & 0xFFFFFFF);
	r_out[14] = static_cast<int32_t>(v >> 28);
}

template <>
static void bigint28_mul_positive<15, 10>(int32_t r_out[25], const int32_t p_in1[15], const int32_t p_in2[10]) {
	DEV_ASSERT(p_in1[14] >= 0 && p_in2[9] >= 0);
	uint64_t v = static_cast<uint64_t>(p_in1[0]) * static_cast<uint64_t>(p_in2[0]);
	r_out[0] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[1]) * static_cast<uint64_t>(p_in2[0]) + static_cast<uint64_t>(p_in1[0]) * static_cast<uint64_t>(p_in2[1]) + (v >> 28);
	r_out[1] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[2]) * static_cast<uint64_t>(p_in2[0]) + static_cast<uint64_t>(p_in1[1]) * static_cast<uint64_t>(p_in2[1]) + static_cast<uint64_t>(p_in1[0]) * static_cast<uint64_t>(p_in2[2]) + (v >> 28);
	r_out[2] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[3]) * static_cast<uint64_t>(p_in2[0]) + static_cast<uint64_t>(p_in1[2]) * static_cast<uint64_t>(p_in2[1]) + static_cast<uint64_t>(p_in1[1]) * static_cast<uint64_t>(p_in2[2]) + static_cast<uint64_t>(p_in1[0]) * static_cast<uint64_t>(p_in2[3]) + (v >> 28);
	r_out[3] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[4]) * static_cast<uint64_t>(p_in2[0]) + static_cast<uint64_t>(p_in1[3]) * static_cast<uint64_t>(p_in2[1]) + static_cast<uint64_t>(p_in1[2]) * static_cast<uint64_t>(p_in2[2]) + static_cast<uint64_t>(p_in1[1]) * static_cast<uint64_t>(p_in2[3]) + static_cast<uint64_t>(p_in1[0]) * static_cast<uint64_t>(p_in2[4]) + (v >> 28);
	r_out[4] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[5]) * static_cast<uint64_t>(p_in2[0]) + static_cast<uint64_t>(p_in1[4]) * static_cast<uint64_t>(p_in2[1]) + static_cast<uint64_t>(p_in1[3]) * static_cast<uint64_t>(p_in2[2]) + static_cast<uint64_t>(p_in1[2]) * static_cast<uint64_t>(p_in2[3]) + static_cast<uint64_t>(p_in1[1]) * static_cast<uint64_t>(p_in2[4]) + static_cast<uint64_t>(p_in1[0]) * static_cast<uint64_t>(p_in2[5]) + (v >> 28);
	r_out[5] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[6]) * static_cast<uint64_t>(p_in2[0]) + static_cast<uint64_t>(p_in1[5]) * static_cast<uint64_t>(p_in2[1]) + static_cast<uint64_t>(p_in1[4]) * static_cast<uint64_t>(p_in2[2]) + static_cast<uint64_t>(p_in1[3]) * static_cast<uint64_t>(p_in2[3]) + static_cast<uint64_t>(p_in1[2]) * static_cast<uint64_t>(p_in2[4]) + static_cast<uint64_t>(p_in1[1]) * static_cast<uint64_t>(p_in2[5]) + static_cast<uint64_t>(p_in1[0]) * static_cast<uint64_t>(p_in2[6]) + (v >> 28);
	r_out[6] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[7]) * static_cast<uint64_t>(p_in2[0]) + static_cast<uint64_t>(p_in1[6]) * static_cast<uint64_t>(p_in2[1]) + static_cast<uint64_t>(p_in1[5]) * static_cast<uint64_t>(p_in2[2]) + static_cast<uint64_t>(p_in1[4]) * static_cast<uint64_t>(p_in2[3]) + static_cast<uint64_t>(p_in1[3]) * static_cast<uint64_t>(p_in2[4]) + static_cast<uint64_t>(p_in1[2]) * static_cast<uint64_t>(p_in2[5]) + static_cast<uint64_t>(p_in1[1]) * static_cast<uint64_t>(p_in2[6]) + static_cast<uint64_t>(p_in1[0]) * static_cast<uint64_t>(p_in2[7]) + (v >> 28);
	r_out[7] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[8]) * static_cast<uint64_t>(p_in2[0]) + static_cast<uint64_t>(p_in1[7]) * static_cast<uint64_t>(p_in2[1]) + static_cast<uint64_t>(p_in1[6]) * static_cast<uint64_t>(p_in2[2]) + static_cast<uint64_t>(p_in1[5]) * static_cast<uint64_t>(p_in2[3]) + static_cast<uint64_t>(p_in1[4]) * static_cast<uint64_t>(p_in2[4]) + static_cast<uint64_t>(p_in1[3]) * static_cast<uint64_t>(p_in2[5]) + static_cast<uint64_t>(p_in1[2]) * static_cast<uint64_t>(p_in2[6]) + static_cast<uint64_t>(p_in1[1]) * static_cast<uint64_t>(p_in2[7]) + static_cast<uint64_t>(p_in1[0]) * static_cast<uint64_t>(p_in2[8]) + (v >> 28);
	r_out[8] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[9]) * static_cast<uint64_t>(p_in2[0]) + static_cast<uint64_t>(p_in1[8]) * static_cast<uint64_t>(p_in2[1]) + static_cast<uint64_t>(p_in1[7]) * static_cast<uint64_t>(p_in2[2]) + static_cast<uint64_t>(p_in1[6]) * static_cast<uint64_t>(p_in2[3]) + static_cast<uint64_t>(p_in1[5]) * static_cast<uint64_t>(p_in2[4]) + static_cast<uint64_t>(p_in1[4]) * static_cast<uint64_t>(p_in2[5]) + static_cast<uint64_t>(p_in1[3]) * static_cast<uint64_t>(p_in2[6]) + static_cast<uint64_t>(p_in1[2]) * static_cast<uint64_t>(p_in2[7]) + static_cast<uint64_t>(p_in1[1]) * static_cast<uint64_t>(p_in2[8]) + static_cast<uint64_t>(p_in1[0]) * static_cast<uint64_t>(p_in2[9]) + (v >> 28);
	r_out[9] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[10]) * static_cast<uint64_t>(p_in2[0]) + static_cast<uint64_t>(p_in1[9]) * static_cast<uint64_t>(p_in2[1]) + static_cast<uint64_t>(p_in1[8]) * static_cast<uint64_t>(p_in2[2]) + static_cast<uint64_t>(p_in1[7]) * static_cast<uint64_t>(p_in2[3]) + static_cast<uint64_t>(p_in1[6]) * static_cast<uint64_t>(p_in2[4]) + static_cast<uint64_t>(p_in1[5]) * static_cast<uint64_t>(p_in2[5]) + static_cast<uint64_t>(p_in1[4]) * static_cast<uint64_t>(p_in2[6]) + static_cast<uint64_t>(p_in1[3]) * static_cast<uint64_t>(p_in2[7]) + static_cast<uint64_t>(p_in1[2]) * static_cast<uint64_t>(p_in2[8]) + static_cast<uint64_t>(p_in1[1]) * static_cast<uint64_t>(p_in2[9]) + (v >> 28);
	r_out[10] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[11]) * static_cast<uint64_t>(p_in2[0]) + static_cast<uint64_t>(p_in1[10]) * static_cast<uint64_t>(p_in2[1]) + static_cast<uint64_t>(p_in1[9]) * static_cast<uint64_t>(p_in2[2]) + static_cast<uint64_t>(p_in1[8]) * static_cast<uint64_t>(p_in2[3]) + static_cast<uint64_t>(p_in1[7]) * static_cast<uint64_t>(p_in2[4]) + static_cast<uint64_t>(p_in1[6]) * static_cast<uint64_t>(p_in2[5]) + static_cast<uint64_t>(p_in1[5]) * static_cast<uint64_t>(p_in2[6]) + static_cast<uint64_t>(p_in1[4]) * static_cast<uint64_t>(p_in2[7]) + static_cast<uint64_t>(p_in1[3]) * static_cast<uint64_t>(p_in2[8]) + static_cast<uint64_t>(p_in1[2]) * static_cast<uint64_t>(p_in2[9]) + (v >> 28);
	r_out[11] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[12]) * static_cast<uint64_t>(p_in2[0]) + static_cast<uint64_t>(p_in1[11]) * static_cast<uint64_t>(p_in2[1]) + static_cast<uint64_t>(p_in1[10]) * static_cast<uint64_t>(p_in2[2]) + static_cast<uint64_t>(p_in1[9]) * static_cast<uint64_t>(p_in2[3]) + static_cast<uint64_t>(p_in1[8]) * static_cast<uint64_t>(p_in2[4]) + static_cast<uint64_t>(p_in1[7]) * static_cast<uint64_t>(p_in2[5]) + static_cast<uint64_t>(p_in1[6]) * static_cast<uint64_t>(p_in2[6]) + static_cast<uint64_t>(p_in1[5]) * static_cast<uint64_t>(p_in2[7]) + static_cast<uint64_t>(p_in1[4]) * static_cast<uint64_t>(p_in2[8]) + static_cast<uint64_t>(p_in1[3]) * static_cast<uint64_t>(p_in2[9]) + (v >> 28);
	r_out[12] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[13]) * static_cast<uint64_t>(p_in2[0]) + static_cast<uint64_t>(p_in1[12]) * static_cast<uint64_t>(p_in2[1]) + static_cast<uint64_t>(p_in1[11]) * static_cast<uint64_t>(p_in2[2]) + static_cast<uint64_t>(p_in1[10]) * static_cast<uint64_t>(p_in2[3]) + static_cast<uint64_t>(p_in1[9]) * static_cast<uint64_t>(p_in2[4]) + static_cast<uint64_t>(p_in1[8]) * static_cast<uint64_t>(p_in2[5]) + static_cast<uint64_t>(p_in1[7]) * static_cast<uint64_t>(p_in2[6]) + static_cast<uint64_t>(p_in1[6]) * static_cast<uint64_t>(p_in2[7]) + static_cast<uint64_t>(p_in1[5]) * static_cast<uint64_t>(p_in2[8]) + static_cast<uint64_t>(p_in1[4]) * static_cast<uint64_t>(p_in2[9]) + (v >> 28);
	r_out[13] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[14]) * static_cast<uint64_t>(p_in2[0]) + static_cast<uint64_t>(p_in1[13]) * static_cast<uint64_t>(p_in2[1]) + static_cast<uint64_t>(p_in1[12]) * static_cast<uint64_t>(p_in2[2]) + static_cast<uint64_t>(p_in1[11]) * static_cast<uint64_t>(p_in2[3]) + static_cast<uint64_t>(p_in1[10]) * static_cast<uint64_t>(p_in2[4]) + static_cast<uint64_t>(p_in1[9]) * static_cast<uint64_t>(p_in2[5]) + static_cast<uint64_t>(p_in1[8]) * static_cast<uint64_t>(p_in2[6]) + static_cast<uint64_t>(p_in1[7]) * static_cast<uint64_t>(p_in2[7]) + static_cast<uint64_t>(p_in1[6]) * static_cast<uint64_t>(p_in2[8]) + static_cast<uint64_t>(p_in1[5]) * static_cast<uint64_t>(p_in2[9]) + (v >> 28);
	r_out[14] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[14]) * static_cast<uint64_t>(p_in2[1]) + static_cast<uint64_t>(p_in1[13]) * static_cast<uint64_t>(p_in2[2]) + static_cast<uint64_t>(p_in1[12]) * static_cast<uint64_t>(p_in2[3]) + static_cast<uint64_t>(p_in1[11]) * static_cast<uint64_t>(p_in2[4]) + static_cast<uint64_t>(p_in1[10]) * static_cast<uint64_t>(p_in2[5]) + static_cast<uint64_t>(p_in1[9]) * static_cast<uint64_t>(p_in2[6]) + static_cast<uint64_t>(p_in1[8]) * static_cast<uint64_t>(p_in2[7]) + static_cast<uint64_t>(p_in1[7]) * static_cast<uint64_t>(p_in2[8]) + static_cast<uint64_t>(p_in1[6]) * static_cast<uint64_t>(p_in2[9]) + (v >> 28);
	r_out[15] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[14]) * static_cast<uint64_t>(p_in2[2]) + static_cast<uint64_t>(p_in1[13]) * static_cast<uint64_t>(p_in2[3]) + static_cast<uint64_t>(p_in1[12]) * static_cast<uint64_t>(p_in2[4]) + static_cast<uint64_t>(p_in1[11]) * static_cast<uint64_t>(p_in2[5]) + static_cast<uint64_t>(p_in1[10]) * static_cast<uint64_t>(p_in2[6]) + static_cast<uint64_t>(p_in1[9]) * static_cast<uint64_t>(p_in2[7]) + static_cast<uint64_t>(p_in1[8]) * static_cast<uint64_t>(p_in2[8]) + static_cast<uint64_t>(p_in1[7]) * static_cast<uint64_t>(p_in2[9]) + (v >> 28);
	r_out[16] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[14]) * static_cast<uint64_t>(p_in2[3]) + static_cast<uint64_t>(p_in1[13]) * static_cast<uint64_t>(p_in2[4]) + static_cast<uint64_t>(p_in1[12]) * static_cast<uint64_t>(p_in2[5]) + static_cast<uint64_t>(p_in1[11]) * static_cast<uint64_t>(p_in2[6]) + static_cast<uint64_t>(p_in1[10]) * static_cast<uint64_t>(p_in2[7]) + static_cast<uint64_t>(p_in1[9]) * static_cast<uint64_t>(p_in2[8]) + static_cast<uint64_t>(p_in1[8]) * static_cast<uint64_t>(p_in2[9]) + (v >> 28);
	r_out[17] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[14]) * static_cast<uint64_t>(p_in2[4]) + static_cast<uint64_t>(p_in1[13]) * static_cast<uint64_t>(p_in2[5]) + static_cast<uint64_t>(p_in1[12]) * static_cast<uint64_t>(p_in2[6]) + static_cast<uint64_t>(p_in1[11]) * static_cast<uint64_t>(p_in2[7]) + static_cast<uint64_t>(p_in1[10]) * static_cast<uint64_t>(p_in2[8]) + static_cast<uint64_t>(p_in1[9]) * static_cast<uint64_t>(p_in2[9]) + (v >> 28);
	r_out[18] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[14]) * static_cast<uint64_t>(p_in2[5]) + static_cast<uint64_t>(p_in1[13]) * static_cast<uint64_t>(p_in2[6]) + static_cast<uint64_t>(p_in1[12]) * static_cast<uint64_t>(p_in2[7]) + static_cast<uint64_t>(p_in1[11]) * static_cast<uint64_t>(p_in2[8]) + static_cast<uint64_t>(p_in1[10]) * static_cast<uint64_t>(p_in2[9]) + (v >> 28);
	r_out[19] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[14]) * static_cast<uint64_t>(p_in2[6]) + static_cast<uint64_t>(p_in1[13]) * static_cast<uint64_t>(p_in2[7]) + static_cast<uint64_t>(p_in1[12]) * static_cast<uint64_t>(p_in2[8]) + static_cast<uint64_t>(p_in1[11]) * static_cast<uint64_t>(p_in2[9]) + (v >> 28);
	r_out[20] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[14]) * static_cast<uint64_t>(p_in2[7]) + static_cast<uint64_t>(p_in1[13]) * static_cast<uint64_t>(p_in2[8]) + static_cast<uint64_t>(p_in1[12]) * static_cast<uint64_t>(p_in2[9]) + (v >> 28);
	r_out[21] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[14]) * static_cast<uint64_t>(p_in2[8]) + static_cast<uint64_t>(p_in1[13]) * static_cast<uint64_t>(p_in2[9]) + (v >> 28);
	r_out[22] = static_cast<int32_t>(v & 0xFFFFFFF);
	v = static_cast<uint64_t>(p_in1[14]) * static_cast<uint64_t>(p_in2[9]) + (v >> 28);
	r_out[23] = static_cast<int32_t>(v & 0xFFFFFFF);
	r_out[24] = static_cast<int32_t>(v >> 28);
}

template <uint32_t digits1, uint32_t digits2>
static void bigint28_mul(int32_t r_out[digits1 + digits2], const int32_t p_in1[digits1], const int32_t p_in2[digits2]) {
	if (p_in1[digits1 - 1] < 0) {
		int32_t in1_neg[digits1];
		bigint28_neg<digits1>(in1_neg, p_in1);
		if (p_in2[digits2 - 1] < 0) {
			int32_t in2_neg[digits2];
			bigint28_neg<digits2>(in2_neg, p_in2);
			bigint28_mul_positive<digits1, digits2>(r_out, in1_neg, in2_neg);
		} else {
			int32_t out_neg[digits1 + digits2];
			bigint28_mul_positive<digits1, digits2>(out_neg, in1_neg, p_in2);
			bigint28_neg<digits1 + digits2>(r_out, out_neg);
		}
	} else if (p_in2[digits2 - 1] < 0) {
		int32_t in2_neg[digits2];
		int32_t out_neg[digits1 + digits2];
		bigint28_neg<digits2>(in2_neg, p_in2);
		bigint28_mul_positive<digits1, digits2>(out_neg, p_in1, in2_neg);
		bigint28_neg<digits1 + digits2>(r_out, out_neg);
	} else {
		bigint28_mul_positive<digits1, digits2>(r_out, p_in1, p_in2);
	}
}

template <uint32_t digits>
static void bigint28_div_positive_1(int32_t r_out[digits], int32_t &r_mod, const int32_t p_in1[digits], int32_t p_in2);

template <>
static void bigint28_div_positive_1<10>(int32_t r_out[10], int32_t &r_mod, const int32_t p_in1[10], int32_t p_in2) {
	DEV_ASSERT(p_in1[9] >= 0 && p_in2 > 0);
	uint64_t in2 = static_cast<uint64_t>(p_in2);
	uint64_t v = static_cast<uint64_t>(p_in1[9]);
	r_out[9] = static_cast<int32_t>(v / in2);
	v = ((v % in2) << 28) | static_cast<uint64_t>(p_in1[8]);
	r_out[8] = static_cast<int32_t>(v / in2);
	v = ((v % in2) << 28) | static_cast<uint64_t>(p_in1[7]);
	r_out[7] = static_cast<int32_t>(v / in2);
	v = ((v % in2) << 28) | static_cast<uint64_t>(p_in1[6]);
	r_out[6] = static_cast<int32_t>(v / in2);
	v = ((v % in2) << 28) | static_cast<uint64_t>(p_in1[5]);
	r_out[5] = static_cast<int32_t>(v / in2);
	v = ((v % in2) << 28) | static_cast<uint64_t>(p_in1[4]);
	r_out[4] = static_cast<int32_t>(v / in2);
	v = ((v % in2) << 28) | static_cast<uint64_t>(p_in1[3]);
	r_out[3] = static_cast<int32_t>(v / in2);
	v = ((v % in2) << 28) | static_cast<uint64_t>(p_in1[2]);
	r_out[2] = static_cast<int32_t>(v / in2);
	v = ((v % in2) << 28) | static_cast<uint64_t>(p_in1[1]);
	r_out[1] = static_cast<int32_t>(v / in2);
	v = ((v % in2) << 28) | static_cast<uint64_t>(p_in1[0]);
	r_out[0] = static_cast<int32_t>(v / in2);
	r_mod = (v % in2);
}

template <>
static void bigint28_div_positive_1<15>(int32_t r_out[15], int32_t &r_mod, const int32_t p_in1[15], int32_t p_in2) {
	DEV_ASSERT(p_in1[14] >= 0 && p_in2 > 0);
	uint64_t in2 = static_cast<uint64_t>(p_in2);
	uint64_t v = static_cast<uint64_t>(p_in1[14]);
	r_out[14] = static_cast<int32_t>(v / in2);
	v = ((v % in2) << 28) | static_cast<uint64_t>(p_in1[13]);
	r_out[13] = static_cast<int32_t>(v / in2);
	v = ((v % in2) << 28) | static_cast<uint64_t>(p_in1[12]);
	r_out[12] = static_cast<int32_t>(v / in2);
	v = ((v % in2) << 28) | static_cast<uint64_t>(p_in1[11]);
	r_out[11] = static_cast<int32_t>(v / in2);
	v = ((v % in2) << 28) | static_cast<uint64_t>(p_in1[10]);
	r_out[10] = static_cast<int32_t>(v / in2);
	v = ((v % in2) << 28) | static_cast<uint64_t>(p_in1[9]);
	r_out[9] = static_cast<int32_t>(v / in2);
	v = ((v % in2) << 28) | static_cast<uint64_t>(p_in1[8]);
	r_out[8] = static_cast<int32_t>(v / in2);
	v = ((v % in2) << 28) | static_cast<uint64_t>(p_in1[7]);
	r_out[7] = static_cast<int32_t>(v / in2);
	v = ((v % in2) << 28) | static_cast<uint64_t>(p_in1[6]);
	r_out[6] = static_cast<int32_t>(v / in2);
	v = ((v % in2) << 28) | static_cast<uint64_t>(p_in1[5]);
	r_out[5] = static_cast<int32_t>(v / in2);
	v = ((v % in2) << 28) | static_cast<uint64_t>(p_in1[4]);
	r_out[4] = static_cast<int32_t>(v / in2);
	v = ((v % in2) << 28) | static_cast<uint64_t>(p_in1[3]);
	r_out[3] = static_cast<int32_t>(v / in2);
	v = ((v % in2) << 28) | static_cast<uint64_t>(p_in1[2]);
	r_out[2] = static_cast<int32_t>(v / in2);
	v = ((v % in2) << 28) | static_cast<uint64_t>(p_in1[1]);
	r_out[1] = static_cast<int32_t>(v / in2);
	v = ((v % in2) << 28) | static_cast<uint64_t>(p_in1[0]);
	r_out[0] = static_cast<int32_t>(v / in2);
	r_mod = (v % in2);
}

template <uint32_t digits1, uint32_t digits2>
static void bigint28_div_positive(int32_t r_out[digits1], int32_t r_mod[digits2], const int32_t p_in1[digits1], const int32_t p_in2[digits2]) {
	DEV_ASSERT(p_in1[digits1 - 1] >= 0 && bigint28_sign<digits2>(p_in2) > 0);
	int32_t div_msb = 0;
	int div_shift = 0;
	if (digits2 > 9 && p_in2[9]) {
		int bit = bigint28_clz(p_in2[9]);
		div_msb = (p_in2[9] << (bit - 4)) | (p_in2[8] >> (32 - bit));
		div_shift = 256 - bit;
	} else if (digits2 > 8 && p_in2[8]) {
		int bit = bigint28_clz(p_in2[8]);
		div_msb = (p_in2[8] << (bit - 4)) | (p_in2[7] >> (32 - bit));
		div_shift = 228 - bit;
	} else if (digits2 > 7 && p_in2[7]) {
		int bit = bigint28_clz(p_in2[7]);
		div_msb = (p_in2[7] << (bit - 4)) | (p_in2[6] >> (32 - bit));
		div_shift = 200 - bit;
	} else if (digits2 > 6 && p_in2[6]) {
		int bit = bigint28_clz(p_in2[6]);
		div_msb = (p_in2[6] << (bit - 4)) | (p_in2[5] >> (32 - bit));
		div_shift = 172 - bit;
	} else if (digits2 > 5 && p_in2[5]) {
		int bit = bigint28_clz(p_in2[5]);
		div_msb = (p_in2[5] << (bit - 4)) | (p_in2[4] >> (32 - bit));
		div_shift = 144 - bit;
	} else if (digits2 > 4 && p_in2[4]) {
		int bit = bigint28_clz(p_in2[4]);
		div_msb = (p_in2[4] << (bit - 4)) | (p_in2[3] >> (32 - bit));
		div_shift = 116 - bit;
	} else if (digits2 > 3 && p_in2[3]) {
		int bit = bigint28_clz(p_in2[3]);
		div_msb = (p_in2[3] << (bit - 4)) | (p_in2[2] >> (32 - bit));
		div_shift = 88 - bit;
	} else if (digits2 > 2 && p_in2[2]) {
		int bit = bigint28_clz(p_in2[2]);
		div_msb = (p_in2[2] << (bit - 4)) | (p_in2[1] >> (32 - bit));
		div_shift = 60 - bit;
	} else if (digits2 > 1 && p_in2[1]) {
		int bit = bigint28_clz(p_in2[1]);
		div_msb = (p_in2[1] << (bit - 4)) | (p_in2[0] >> (32 - bit));
		div_shift = 32 - bit;
	} else {
		bigint28_clear<digits2>(r_mod);
		bigint28_div_positive_1<digits1>(r_out, r_mod[0], p_in1, p_in2[0]);
		return;
	}
	div_msb++;
	int32_t mod[digits1];
	bigint28_copy<digits1, digits1>(mod, p_in1);
	bigint28_clear<digits1>(r_out);
	int32_t in2_shl1[digits2];
	bigint28_copy<digits2, digits2>(in2_shl1, p_in2);
	bigint28_shl1<digits2>(in2_shl1);
	int32_t check[digits1];
	bigint28_copy<digits1, digits1>(check, mod);
	bigint28_sub<digits1, digits2>(check, in2_shl1);
	while (check[digits1 - 1] >= 0) {
		int32_t tmp_mod;
		int32_t tmp_div[digits1];
		bigint28_div_positive_1<digits1>(tmp_div, tmp_mod, mod, div_msb);
		bigint28_shr<digits1>(tmp_div, div_shift);
		bigint28_add<digits1, digits1>(r_out, tmp_div);
		int32_t tmp_mul[digits1 + digits2];
		bigint28_mul_positive<digits1, digits2>(tmp_mul, tmp_div, p_in2);
		bigint28_sub_positive<digits1, digits1 + digits2>(mod, tmp_mul);
		bigint28_copy<digits1, digits1>(check, mod);
		bigint28_sub<digits1, digits2>(check, in2_shl1);
	}
	bigint28_copy<digits1, digits1>(check, mod);
	bigint28_sub<digits1, digits2>(check, p_in2);
	if (check[digits1 - 1] >= 0) {
		bigint28_copy<digits1, digits1>(mod, check);
		bigint28_add1<digits1>(r_out);
	}
	bigint28_copy<digits2, digits1>(r_mod, mod);
}

template <uint32_t digits1, uint32_t digits2>
static void bigint28_div(int32_t r_out[digits1], int32_t r_mod[digits2], const int32_t p_in1[digits1], const int32_t p_in2[digits2]) {
	DEV_ASSERT(bigint28_sign<digits2>(p_in2) > 0);
	if (p_in1[digits1 - 1] < 0) {
		int32_t in1_neg[digits1];
		int32_t out_neg[digits1];
		bigint28_neg<digits1>(in1_neg, p_in1);
		bigint28_div_positive<digits1, digits2>(out_neg, r_mod, in1_neg, p_in2);
		bigint28_neg<digits1>(r_out, out_neg);
		if (bigint28_sign<digits2>(r_mod) > 0) {
			int32_t mod[digits2];
			bigint28_add1<digits1>(r_out);
			bigint28_copy<digits2, digits2>(mod, p_in2);
			bigint28_sub<digits2, digits2>(mod, r_mod);
			bigint28_copy<digits2, digits2>(r_mod, mod);
		}
	} else {
		bigint28_div_positive<digits1, digits2>(r_out, r_mod, p_in1, p_in2);
	}
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
		int32_t start_x[5];
		int32_t start_y[5];
		int32_t end_x[5];
		int32_t end_y[5];
		bigint28_from_r128<5>(start_x, R128((p_edges[j].x - rect.position.x) / rect.size.x));
		bigint28_from_r128<5>(start_y, R128((p_edges[j].y - rect.position.y) / rect.size.y));
		bigint28_from_r128<5>(end_x, R128((p_edges[j + 1].x - rect.position.x) / rect.size.x));
		bigint28_from_r128<5>(end_y, R128((p_edges[j + 1].y - rect.position.y) / rect.size.y));
		int32_t check[5];
		bigint28_copy<5, 5>(check, start_x);
		bigint28_sub<5, 5>(check, end_x);
		int sign = bigint28_sign<5>(check);
		if (sign < 0) {
			add_edge(add_point(add_slice(start_x), start_y), add_point(add_slice(end_x), end_y), p_winding[i]);
		} else if (sign > 0) {
			add_edge(add_point(add_slice(end_x), end_y), add_point(add_slice(start_x), start_y), -p_winding[i]);
		} else {
			int32_t check[5];
			bigint28_copy<5, 5>(check, start_y);
			bigint28_sub<5, 5>(check, end_y);
			sign = bigint28_sign<5>(check);
			if (sign < 0) {
				add_vertical_edge(add_slice(start_x), start_y, end_y);
			} else if (sign > 0) {
				add_vertical_edge(add_slice(start_x), end_y, start_y);
			}
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
				DEV_ASSERT(edges[list_nodes[check_iter].element].next_check == slice);
				uint32_t check_iter_next = list_nodes[check_iter].next;
				if (points[edges[list_nodes[check_iter].element].point_end].slice == slice) {
					uint32_t treenode_edge_prev = tree_nodes[edges[list_nodes[check_iter].element].treenode_edges].current.prev;
					if (treenode_edge_prev != edges_tree) {
						edges[tree_nodes[treenode_edge_prev].element].next_check = slice;
						list_insert(edges[tree_nodes[treenode_edge_prev].element].listnode_check, slices[slice].check_list);
					}
					list_insert(edges[list_nodes[check_iter].element].listnode_incoming, incoming_list);
					tree_remove(edges[list_nodes[check_iter].element].treenode_edges, slice);
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
					int32_t cross1[10];
					int32_t cross2[10];
					const Edge &edge = edges[tree_nodes[treenode_edge].element];
					bigint28_mul<5, 5>(cross1, verticals[tree_nodes[vertical_iter].element].y, edge.dir_x);
					bigint28_mul<5, 5>(cross2, slices[slice].x, edge.dir_y);
					bigint28_sub<10, 10>(cross1, cross2);
					bigint28_sub<10, 10>(cross1, edge.cross);
					if (bigint28_sign<10>(cross1) <= 0) {
						break;
					}
					int32_t y[5];
					edge_intersect_x(y, tree_nodes[treenode_edge].element, slices[slice].x);
					add_point(slice, y);
					list_insert(edges[tree_nodes[treenode_edge].element].listnode_incoming, incoming_list);
					list_insert(edges[tree_nodes[treenode_edge].element].listnode_outgoing, outgoing_list);
					DEV_ASSERT(is_point_on_edge(add_point(slice, y), tree_nodes[treenode_edge].element, false));
				}
				vertical_iter = tree_nodes[vertical_iter].current.next;
			}
		}

		{
			// Add edges starting at this slice
			uint32_t check_iter = list_nodes[slices[slice].check_list].next;
			while (check_iter != slices[slice].check_list) {
				DEV_ASSERT(edges[list_nodes[check_iter].element].next_check == slice);
				if (points[edges[list_nodes[check_iter].element].point_start].slice == slice) {
					uint32_t treenode_edge = get_edge_before_end(slices[slice].x, points[edges[list_nodes[check_iter].element].point_start].y, points[edges[list_nodes[check_iter].element].point_end].x, points[edges[list_nodes[check_iter].element].point_end].y);
					list_insert(edges[list_nodes[check_iter].element].listnode_outgoing, outgoing_list);
					tree_insert(edges[list_nodes[check_iter].element].treenode_edges, treenode_edge, slice);
					if (treenode_edge != edges_tree) {
						edges[tree_nodes[treenode_edge].element].next_check = slice;
						list_insert(edges[tree_nodes[treenode_edge].element].listnode_check, slices[slice].check_list);
					}
				}
				check_iter = list_nodes[check_iter].next;
			}
		}

		{
			// Check order changes of edges, and mark as intersections
			int32_t x[5];
			bigint28_copy<5, 5>(x, slices[slice].x);
			bigint28_add1<5>(x);
			while (list_nodes[slices[slice].check_list].next != slices[slice].check_list) {
				uint32_t edge = list_nodes[list_nodes[slices[slice].check_list].next].element;
				DEV_ASSERT(edges[edge].next_check == slice);
				// Reset the next check of the checked edge to its end point.
				// This will be reduced to the nearest intersection if one is found.
				edges[edge].next_check = points[edges[edge].point_end].slice;
				list_insert(edges[edge].listnode_check, slices[points[edges[edge].point_end].slice].check_list);
				uint32_t treenode_edge_next = tree_nodes[edges[edge].treenode_edges].current.next;
				if (treenode_edge_next == edges_tree) {
					continue;
				}
				uint32_t edge_next = tree_nodes[treenode_edge_next].element;
				Edge &edge1 = edges[edge];
				Edge &edge2 = edges[edge_next];
				int32_t factor1[10];
				int32_t total1[15];
				int32_t factor2[10];
				int32_t total2[15];
				bigint28_mul<5, 5>(factor1, x, edge1.dir_y);
				bigint28_add<10, 10>(factor1, edge1.cross);
				bigint28_mul<10, 5>(total1, factor1, edge2.dir_x);
				bigint28_mul<5, 5>(factor2, x, edge2.dir_y);
				bigint28_add<10, 10>(factor2, edge2.cross);
				bigint28_mul<10, 5>(total2, factor2, edge1.dir_x);
				bigint28_sub<15, 15>(total2, total1);
				if (total2[14] >= 0) {
					continue;
				}
				int32_t y[5];
				edge_intersect_edge(y, edge, edge_next);
				add_point(slice, y);
				if (tree_nodes[edges[edge].treenode_edges].self_value == 0) {
					tree_remove(edges[edge].treenode_edges, slice);
					list_insert(edges[edge].listnode_incoming, incoming_list);
					list_insert(edges[edge_next].listnode_incoming, incoming_list);
					list_insert(edges[edge_next].listnode_outgoing, outgoing_list);
					list_remove(edges[edge].listnode_check);
					uint32_t treenode_edge_prev = tree_nodes[treenode_edge_next].current.prev;
					if (treenode_edge_prev != edges_tree) {
						edges[tree_nodes[treenode_edge_prev].element].next_check = slice;
						list_insert(edges[tree_nodes[treenode_edge_prev].element].listnode_check, slices[slice].check_list);
					}
				} else if (tree_nodes[treenode_edge_next].self_value == 0) {
					tree_remove(treenode_edge_next, slice);
					list_insert(edges[edge].listnode_incoming, incoming_list);
					list_insert(edges[edge_next].listnode_incoming, incoming_list);
					list_insert(edges[edge].listnode_outgoing, outgoing_list);
					list_remove(edges[edge_next].listnode_check);
					edges[edge].next_check = slice;
					list_insert(edges[edge].listnode_check, slices[slice].check_list);
				} else {
					tree_swap(edges[edge].treenode_edges, treenode_edge_next, slice);
					list_insert(edges[edge].listnode_incoming, incoming_list);
					list_insert(edges[edge_next].listnode_incoming, incoming_list);
					list_insert(edges[edge].listnode_outgoing, outgoing_list);
					list_insert(edges[edge_next].listnode_outgoing, outgoing_list);
					edges[edge].next_check = slice;
					list_insert(edges[edge].listnode_check, slices[slice].check_list);
					uint32_t treenode_edge_prev = tree_nodes[treenode_edge_next].current.prev;
					if (treenode_edge_prev != edges_tree) {
						edges[tree_nodes[treenode_edge_prev].element].next_check = slice;
						list_insert(edges[tree_nodes[treenode_edge_prev].element].listnode_check, slices[slice].check_list);
					}
				}
			}
		}

		{
			// Add incoming edges to points
			while (list_nodes[incoming_list].next != incoming_list) {
				uint32_t edge = list_nodes[list_nodes[incoming_list].next].element;
				list_remove(list_nodes[incoming_list].next);
				tree_index_previous(edges[edge].treenode_edges, slice);
				uint32_t treenode_point = get_point_before_edge(slice, edge, false);
				if (treenode_point == slices[slice].points_tree) {
					treenode_point = tree_nodes[treenode_point].current.next;
				} else if (tree_nodes[treenode_point].current.next != slices[slice].points_tree && !is_point_on_edge(tree_nodes[treenode_point].element, edge, false)) {
					int32_t check[5];
					bigint28_copy<5, 5>(check, points[edges[edge].point_start].y);
					bigint28_sub<5, 5>(check, points[edges[edge].point_end].y);
					if (bigint28_sign<5>(check) < 0 || is_point_on_edge(tree_nodes[tree_nodes[treenode_point].current.next].element, edge, false)) {
						treenode_point = tree_nodes[treenode_point].current.next;
					}
				}
				DEV_ASSERT(treenode_point != slices[slice].points_tree);
				tree_insert(edges[edge].treenode_incoming, point_get_incoming_before(tree_nodes[treenode_point].element, tree_nodes[edges[edge].treenode_edges].index));
			}
		}

		{
			// Add outgoing edges to points
			while (list_nodes[outgoing_list].next != outgoing_list) {
				uint32_t edge = list_nodes[list_nodes[outgoing_list].next].element;
				list_remove(list_nodes[outgoing_list].next);
				tree_index(edges[edge].treenode_edges);
				uint32_t treenode_point = get_point_before_edge(slice, edge, true);
				if (treenode_point == slices[slice].points_tree) {
					treenode_point = tree_nodes[treenode_point].current.next;
				} else if (tree_nodes[treenode_point].current.next != slices[slice].points_tree && !is_point_on_edge(tree_nodes[treenode_point].element, edge, true)) {
					int32_t check[5];
					bigint28_copy<5, 5>(check, points[edges[edge].point_start].y);
					bigint28_sub<5, 5>(check, points[edges[edge].point_end].y);
					if (bigint28_sign<5>(check) > 0 || is_point_on_edge(tree_nodes[tree_nodes[treenode_point].current.next].element, edge, true)) {
						treenode_point = tree_nodes[treenode_point].current.next;
					}
				}
				DEV_ASSERT(treenode_point != slices[slice].points_tree);
				tree_insert(edges[edge].treenode_outgoing, point_get_outgoing_before(tree_nodes[treenode_point].element, tree_nodes[edges[edge].treenode_edges].index));
			}
		}

		{
			// Erase unused points
			uint32_t point_iter = tree_nodes[slices[slice].points_tree].current.next;
			while (point_iter != slices[slice].points_tree) {
				uint32_t point = tree_nodes[point_iter].element;
				uint32_t point_iter_next = tree_nodes[point_iter].current.next;
				if (tree_nodes[points[point].incoming_tree].current.next == points[point].incoming_tree && tree_nodes[points[point].outgoing_tree].current.next == points[point].outgoing_tree) {
					tree_remove(point_iter);
				}
				point_iter = point_iter_next;
			}
		}

		{
			// Force edges going through a point to treat it as intersection
			uint32_t point_iter = tree_nodes[slices[slice].points_tree].current.next;
			while (point_iter != slices[slice].points_tree) {
				uint32_t point = tree_nodes[point_iter].element;
				// Edges are currently sorted by their y at the next x. To get their sorting
				// by the y at the current x, we need to use the previous tree
				uint32_t treenode_edge = get_edge_before_previous(slice, points[point].y);
				// Find first edge coinciding with the point
				while (treenode_edge != edges_tree && is_point_on_edge(point, tree_nodes[treenode_edge].element, false)) {
					if (tree_nodes[treenode_edge].version == slice) {
						treenode_edge = tree_nodes[treenode_edge].previous.prev;
					} else {
						treenode_edge = tree_nodes[treenode_edge].current.prev;
					}
				}
				if (tree_nodes[treenode_edge].version == slice) {
					treenode_edge = tree_nodes[treenode_edge].previous.next;
				} else {
					treenode_edge = tree_nodes[treenode_edge].current.next;
				}
				while (treenode_edge != edges_tree && is_point_on_edge(point, tree_nodes[treenode_edge].element, false)) {
					if (tree_nodes[edges[tree_nodes[treenode_edge].element].treenode_incoming].current.parent == 0) {
						tree_index_previous(treenode_edge, slice);
						tree_insert(edges[tree_nodes[treenode_edge].element].treenode_incoming, point_get_incoming_before(point, tree_nodes[treenode_edge].index));
					}
					if (tree_nodes[treenode_edge].current.parent != 0 && tree_nodes[edges[tree_nodes[treenode_edge].element].treenode_outgoing].current.parent == 0) {
						// If the edge wasn't removed this slice, add outgoing too
						tree_index(treenode_edge);
						tree_insert(edges[tree_nodes[treenode_edge].element].treenode_outgoing, point_get_outgoing_before(point, tree_nodes[treenode_edge].index));
					}
					if (tree_nodes[treenode_edge].version == slice) {
						treenode_edge = tree_nodes[treenode_edge].previous.next;
					} else {
						treenode_edge = tree_nodes[treenode_edge].current.next;
					}
				}
				point_iter = tree_nodes[point_iter].current.next;
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
					if (tree_nodes[treenode_edge_first].version == slice) {
						treenode_edge_before = tree_nodes[treenode_edge_first].previous.prev;
					} else {
						treenode_edge_before = tree_nodes[treenode_edge_first].current.prev;
					}
				} else {
					treenode_edge_before = get_edge_before_previous(slice, points[point].y);
				}
				if (treenode_edge_before == treenode_edge_previous) {
					if (winding != 0 && (!p_winding_even_odd || (winding & 1))) {
						DEV_ASSERT(treenode_edge_previous != edges_tree);
						triangles.push_back(point_previous);
						triangles.push_back(point);
						if (tree_nodes[treenode_edge_previous].version == slice) {
							DEV_ASSERT(tree_nodes[treenode_edge_previous].previous.next != edges_tree);
							triangles.push_back(edges[tree_nodes[tree_nodes[treenode_edge_previous].previous.next].element].point_outgoing);
						} else {
							DEV_ASSERT(tree_nodes[treenode_edge_previous].current.next != edges_tree);
							triangles.push_back(edges[tree_nodes[tree_nodes[treenode_edge_previous].current.next].element].point_outgoing);
						}
					}
				} else {
					treenode_edge_previous = treenode_edge_before;
					winding = edge_get_winding_previous(treenode_edge_previous, slice);
					if (winding != 0 && (!p_winding_even_odd || (winding & 1))) {
						DEV_ASSERT(treenode_edge_previous != edges_tree);
						triangles.push_back(edges[tree_nodes[treenode_edge_previous].element].point_outgoing);
						triangles.push_back(point);
						if (tree_nodes[treenode_edge_previous].version == slice) {
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
					DEV_ASSERT(edges[tree_nodes[edge_incoming_iter].element].treenode_edges == (tree_nodes[treenode_edge_previous].version == slice ? tree_nodes[treenode_edge_previous].previous.next : tree_nodes[treenode_edge_previous].current.next));
					treenode_edge_previous = edges[tree_nodes[edge_incoming_iter].element].treenode_edges;
					winding += tree_nodes[treenode_edge_previous].self_value;
					if (winding != 0 && (!p_winding_even_odd || (winding & 1))) {
						DEV_ASSERT(treenode_edge_previous != edges_tree);
						triangles.push_back(edges[tree_nodes[treenode_edge_previous].element].point_outgoing);
						triangles.push_back(point);
						if (tree_nodes[treenode_edge_previous].version == slice) {
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
			// Set outgoing points for subsequent triangle production
			uint32_t point_iter = tree_nodes[slices[slice].points_tree].current.next;
			while (point_iter != slices[slice].points_tree) {
				uint32_t point = tree_nodes[point_iter].element;
				uint32_t edge_outgoing_iter = tree_nodes[points[point].outgoing_tree].current.next;
				while (edge_outgoing_iter != points[point].outgoing_tree) {
					edges[tree_nodes[edge_outgoing_iter].element].point_outgoing = point;
					edge_outgoing_iter = tree_nodes[edge_outgoing_iter].current.next;
				}
				point_iter = tree_nodes[point_iter].current.next;
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
						int32_t check[5];
						bigint28_copy<5, 5>(check, points[edges[tree_nodes[treenode_edge_before].element].point_end].x);
						bigint28_sub<5, 5>(check, points[edges[tree_nodes[tree_nodes[treenode_edge_before].current.next].element].point_end].x);
						if (bigint28_sign<5>(check) < 0) {
							add_edge(point, edges[tree_nodes[treenode_edge_before].element].point_end, 0);
						} else {
							add_edge(point, edges[tree_nodes[tree_nodes[treenode_edge_before].current.next].element].point_end, 0);
						}
						// Adding the edge at the current slice will cause it to be added to the check list.
						// Remove it, and add it to the point's outgoing edges.
						DEV_ASSERT(list_nodes[slices[slice].check_list].next != slices[slice].check_list);
						uint32_t edge = list_nodes[list_nodes[slices[slice].check_list].next].element;
						tree_insert(edges[edge].treenode_edges, treenode_edge_before, slice);
						tree_insert(edges[edge].treenode_outgoing, points[point].outgoing_tree);
						edges[edge].next_check = points[edges[edge].point_end].slice;
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
							int32_t a_x[5];
							int32_t a_y[5];
							int32_t b_x[5];
							int32_t b_y[5];
							bigint28_copy<5, 5>(a_x, points[point].x);
							bigint28_sub<5, 5>(a_x, points[point_other_outgoing].x);
							bigint28_copy<5, 5>(a_y, points[point].y);
							bigint28_sub<5, 5>(a_y, points[point_other_outgoing].y);
							bigint28_copy<5, 5>(b_x, points[point_edge_end].x);
							bigint28_sub<5, 5>(b_x, points[point_other_outgoing].x);
							bigint28_copy<5, 5>(b_y, points[point_edge_end].y);
							bigint28_sub<5, 5>(b_y, points[point_other_outgoing].y);
							int32_t cross1[10];
							int32_t cross2[10];
							bigint28_mul<5, 5>(cross1, a_x, b_y);
							bigint28_mul<5, 5>(cross2, a_y, b_x);
							bigint28_sub<10, 10>(cross1, cross2);
							// Perfect accuracy isn't needed here. Getting it "wrong" will simply
							// result in either a near-line-like flipped triangle, or at most one
							// unnecessary vertex
							if (bigint28_sign<10>(cross1) > 0) {
								DEV_ASSERT(list_nodes[slices[slice].check_list].next == slices[slice].check_list);
								add_edge(point, edges[tree_nodes[treenode_edge_other].element].point_end, 0);
								DEV_ASSERT(list_nodes[slices[slice].check_list].next != slices[slice].check_list);
								uint32_t edge = list_nodes[list_nodes[slices[slice].check_list].next].element;
								tree_insert(edges[edge].treenode_edges, treenode_edge_other, slice);
								tree_insert(edges[edge].treenode_outgoing, points[point].outgoing_tree);
								edges[edge].next_check = points[edges[edge].point_end].slice;
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
							int32_t a_x[5];
							int32_t a_y[5];
							int32_t b_x[5];
							int32_t b_y[5];
							bigint28_copy<5, 5>(a_x, points[point].x);
							bigint28_sub<5, 5>(a_x, points[point_other_outgoing].x);
							bigint28_copy<5, 5>(a_y, points[point].y);
							bigint28_sub<5, 5>(a_y, points[point_other_outgoing].y);
							bigint28_copy<5, 5>(b_x, points[point_edge_end].x);
							bigint28_sub<5, 5>(b_x, points[point_other_outgoing].x);
							bigint28_copy<5, 5>(b_y, points[point_edge_end].y);
							bigint28_sub<5, 5>(b_y, points[point_other_outgoing].y);
							int32_t cross1[10];
							int32_t cross2[10];
							bigint28_mul<5, 5>(cross1, a_x, b_y);
							bigint28_mul<5, 5>(cross2, a_y, b_x);
							bigint28_sub<10, 10>(cross1, cross2);
							// Perfect accuracy isn't needed here. Getting it "wrong" will simply
							// result in either a near-line-like flipped triangle, or at most one
							// unnecessary vertex
							if (bigint28_sign<10>(cross1) < 0) {
								DEV_ASSERT(list_nodes[slices[slice].check_list].next == slices[slice].check_list);
								add_edge(point, edges[tree_nodes[treenode_edge_other].element].point_end, 0);
								DEV_ASSERT(list_nodes[slices[slice].check_list].next != slices[slice].check_list);
								uint32_t edge = list_nodes[list_nodes[slices[slice].check_list].next].element;
								tree_insert(edges[edge].treenode_edges, edges[edge_last].treenode_edges, slice);
								tree_insert(edges[edge].treenode_outgoing, edges[edge_last].treenode_outgoing);
								edges[edge].next_check = points[edges[edge].point_end].slice;
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
							check_intersection(treenode_edge);
						}
					}
					{
						uint32_t treenode_edge = edges[tree_nodes[tree_nodes[points[point].outgoing_tree].current.prev].element].treenode_edges;
						if (tree_nodes[treenode_edge].current.next != edges_tree) {
							check_intersection(treenode_edge);
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
				out_points.push_back(Vector2(static_cast<double>(bigint28_to_r128<5>(points[triangles[i]].x)) * rect.size.x + rect.position.x, static_cast<double>(bigint28_to_r128<5>(points[triangles[i]].y)) * rect.size.y + rect.position.y));
				points[triangles[i]].used = out_points.size();
			}
			out_triangles.push_back(points[triangles[i]].used - 1);
		}
	}
}

uint32_t BentleyOttmann::add_slice(const int32_t p_x[5]) {
	uint32_t insert_after = slices_tree;
	uint32_t current = tree_nodes[slices_tree].current.right;
	if (current) {
		while (true) {
			int32_t x[5];
			bigint28_copy<5, 5>(x, p_x);
			bigint28_sub<5, 5>(x, slices[tree_nodes[current].element].x);
			int sign = bigint28_sign<5>(x);
			if (sign < 0) {
				if (tree_nodes[current].current.left) {
					current = tree_nodes[current].current.left;
					continue;
				}
				insert_after = tree_nodes[current].current.prev;
				break;
			}
			if (sign > 0) {
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
	bigint28_copy<5, 5>(slice.x, p_x);
	slice.points_tree = tree_create();
	slice.vertical_tree = tree_create();
	slice.check_list = list_create();
	tree_insert(tree_create(slices.size()), insert_after);
	slices.push_back(slice);
	return slices.size() - 1;
}

uint32_t BentleyOttmann::add_point(uint32_t p_slice, const int32_t p_y[5]) {
	uint32_t insert_after = slices[p_slice].points_tree;
	uint32_t current = tree_nodes[slices[p_slice].points_tree].current.right;
	if (current) {
		while (true) {
			int32_t y[5];
			bigint28_copy<5, 5>(y, p_y);
			bigint28_sub<5, 5>(y, points[tree_nodes[current].element].y);
			int sign = bigint28_sign<5>(y);
			if (sign < 0) {
				if (tree_nodes[current].current.left) {
					current = tree_nodes[current].current.left;
					continue;
				}
				insert_after = tree_nodes[current].current.prev;
				break;
			}
			if (sign > 0) {
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
	bigint28_copy<5, 5>(point.x, slices[p_slice].x);
	bigint28_copy<5, 5>(point.y, p_y);
	point.incoming_tree = tree_create();
	point.outgoing_tree = tree_create();
	tree_insert(tree_create(points.size()), insert_after);
	points.push_back(point);
	return points.size() - 1;
}

uint32_t BentleyOttmann::get_point_before_edge(uint32_t p_slice, uint32_t p_edge, bool p_next_x) {
	uint32_t current = tree_nodes[slices[p_slice].points_tree].current.right;
	if (!current) {
		return slices[p_slice].points_tree;
	}
	const Edge &edge = edges[p_edge];
	int32_t x[5];
	bigint28_copy<5, 5>(x, slices[p_slice].x);
	if (p_next_x) {
		bigint28_add1<5>(x);
	}
	while (true) {
		int32_t cross1[10];
		int32_t cross2[10];
		bigint28_mul<5, 5>(cross1, points[tree_nodes[current].element].y, edge.dir_x);
		bigint28_mul<5, 5>(cross2, x, edge.dir_y);
		bigint28_sub<10, 10>(cross1, cross2);
		bigint28_sub<10, 10>(cross1, edge.cross);
		int sign = bigint28_sign<10>(cross1);
		if (sign > 0) {
			if (tree_nodes[current].current.left) {
				current = tree_nodes[current].current.left;
				continue;
			}
			return tree_nodes[current].current.prev;
		}
		if (sign < 0 && tree_nodes[current].current.right) {
			current = tree_nodes[current].current.right;
			continue;
		}
		return current;
	}
}

bool BentleyOttmann::is_point_on_edge(uint32_t p_point, uint32_t p_edge, bool p_next_x) {
	const Edge &edge = edges[p_edge];
	int32_t x[5];
	bigint28_copy<5, 5>(x, points[p_point].x);
	if (p_next_x) {
		bigint28_add1<5>(x);
	}
	int32_t cross1[10];
	int32_t cross2[10];
	bigint28_mul<5, 5>(cross1, points[p_point].y, edge.dir_x);
	bigint28_mul<5, 5>(cross2, x, edge.dir_y);
	bigint28_sub<10, 10>(cross1, cross2);
	bigint28_sub<10, 10>(cross1, edge.cross);
	bigint28_shl1<10>(cross1);
	bigint28_copy<10, 10>(cross2, cross1);
	bigint28_sub<10, 5>(cross1, edge.dir_x);
	bigint28_add<10, 5>(cross2, edge.dir_x);
	return bigint28_sign<10>(cross1) <= 0 && bigint28_sign<10>(cross2) > 0;
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
	Edge edge;
	edge.point_start = edge.point_outgoing = p_point_start;
	edge.point_end = p_point_end;
	edge.treenode_edges = tree_create(edges.size(), p_winding);
	edge.treenode_incoming = tree_create(edges.size());
	edge.treenode_outgoing = tree_create(edges.size());
	edge.listnode_incoming = list_create(edges.size());
	edge.listnode_outgoing = list_create(edges.size());
	edge.listnode_check = list_create(edges.size());
	int32_t cross[10];
	bigint28_copy<5, 5>(edge.dir_x, points[p_point_end].x);
	bigint28_copy<5, 5>(edge.dir_y, points[p_point_end].y);
	bigint28_sub<5, 5>(edge.dir_x, points[p_point_start].x);
	bigint28_sub<5, 5>(edge.dir_y, points[p_point_start].y);
	DEV_ASSERT(bigint28_sign<5>(edge.dir_x) > 0);
	edge.next_check = points[p_point_start].slice;
	bigint28_mul<5, 5>(edge.cross, points[p_point_start].y, edge.dir_x);
	bigint28_mul<5, 5>(cross, points[p_point_start].x, edge.dir_y);
	bigint28_sub<10, 10>(edge.cross, cross);
	edges.push_back(edge);
	list_insert(edge.listnode_check, slices[points[p_point_start].slice].check_list);
}

void BentleyOttmann::add_vertical_edge(uint32_t p_slice, const int32_t p_y_start[5], const int32_t p_y_end[5]) {
	uint32_t start;
	uint32_t current = tree_nodes[slices[p_slice].vertical_tree].current.right;
	if (!current) {
		Vertical vertical;
		bigint28_copy<5, 5>(vertical.y, p_y_start);
		vertical.is_start = true;
		start = tree_create(verticals.size());
		verticals.push_back(vertical);
		tree_insert(start, slices[p_slice].vertical_tree);
	} else {
		while (true) {
			int32_t y[5];
			bigint28_copy<5, 5>(y, p_y_start);
			bigint28_sub<5, 5>(y, verticals[tree_nodes[current].element].y);
			int sign = bigint28_sign<5>(y);
			if (sign < 0) {
				if (tree_nodes[current].current.left) {
					current = tree_nodes[current].current.left;
					continue;
				}
				if (verticals[tree_nodes[current].element].is_start) {
					Vertical vertical;
					bigint28_copy<5, 5>(vertical.y, p_y_start);
					vertical.is_start = true;
					start = tree_create(verticals.size());
					verticals.push_back(vertical);
					tree_insert(start, tree_nodes[current].current.prev);
				} else {
					start = tree_nodes[current].current.prev;
				}
				break;
			}
			if (sign > 0) {
				if (tree_nodes[current].current.right) {
					current = tree_nodes[current].current.right;
					continue;
				}
				if (!verticals[tree_nodes[current].element].is_start) {
					Vertical vertical;
					bigint28_copy<5, 5>(vertical.y, p_y_start);
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
	while (tree_nodes[start].current.next != slices[p_slice].vertical_tree) {
		int32_t y[5];
		bigint28_copy<5, 5>(y, p_y_end);
		bigint28_sub<5, 5>(y, verticals[tree_nodes[tree_nodes[start].current.next].element].y);
		int sign = bigint28_sign<5>(y);
		if (sign < 0 || (sign == 0 && !verticals[tree_nodes[tree_nodes[start].current.next].element].is_start)) {
			break;
		}
		tree_remove(tree_nodes[start].current.next);
	}
	if (tree_nodes[start].current.next == slices[p_slice].vertical_tree || verticals[tree_nodes[tree_nodes[start].current.next].element].is_start) {
		Vertical vertical;
		bigint28_copy<5, 5>(vertical.y, p_y_end);
		vertical.is_start = false;
		tree_insert(tree_create(verticals.size()), start);
		verticals.push_back(vertical);
	}
}

void BentleyOttmann::edge_intersect_x(int32_t r_y[5], uint32_t p_edge, const int32_t p_x[5]) {
	const Edge &edge = edges[p_edge];
	int32_t total[10];
	int32_t y[10];
	int32_t mod[5];
	bigint28_mul<5, 5>(total, p_x, edge.dir_y);
	bigint28_add<10, 10>(total, edge.cross);
	bigint28_div<10, 5>(y, mod, total, edge.dir_x);
	bigint28_shl1<5>(mod);
	bigint28_sub<5, 5>(mod, edge.dir_x);
	if (mod[4] >= 0) {
		bigint28_add1<10>(y);
	}
	bigint28_copy<5, 10>(r_y, y);
}

void BentleyOttmann::edge_intersect_edge(int32_t r_y[5], uint32_t p_edge1, uint32_t p_edge2) {
	const Edge &edge1 = edges[p_edge1];
	const Edge &edge2 = edges[p_edge2];
	int32_t total1[15];
	int32_t total2[15];
	int32_t factor1[10];
	int32_t factor2[10];
	int32_t y[15];
	int32_t mod[10];
	bigint28_mul<10, 5>(total1, edge1.cross, edge2.dir_y);
	bigint28_mul<10, 5>(total2, edge2.cross, edge1.dir_y);
	bigint28_sub<15, 15>(total2, total1);
	bigint28_mul<5, 5>(factor1, edge1.dir_y, edge2.dir_x);
	bigint28_mul<5, 5>(factor2, edge2.dir_y, edge1.dir_x);
	bigint28_sub<10, 10>(factor1, factor2);
	bigint28_div<15, 10>(y, mod, total2, factor1);
	bigint28_shl1<10>(mod);
	bigint28_sub<10, 10>(mod, factor1);
	if (mod[9] >= 0) {
		bigint28_add1<15>(y);
	}
	bigint28_copy<5, 15>(r_y, y);
}

uint32_t BentleyOttmann::get_edge_before(const int32_t p_x[5], const int32_t p_y[5]) {
	uint32_t current = tree_nodes[edges_tree].current.right;
	if (!current) {
		return edges_tree;
	}
	while (true) {
		int32_t cross1[10];
		int32_t cross2[10];
		const Edge &edge = edges[tree_nodes[current].element];
		bigint28_mul<5, 5>(cross1, p_y, edge.dir_x);
		bigint28_mul<5, 5>(cross2, p_x, edge.dir_y);
		bigint28_sub<10, 10>(cross1, cross2);
		bigint28_sub<10, 10>(cross1, edge.cross);
		int sign = bigint28_sign<10>(cross1);
		if (sign > 0) {
			if (tree_nodes[current].current.right) {
				current = tree_nodes[current].current.right;
				continue;
			}
			return current;
		}
		if (sign < 0 && tree_nodes[current].current.left) {
			current = tree_nodes[current].current.left;
			continue;
		}
		return tree_nodes[current].current.prev;
	}
}

uint32_t BentleyOttmann::get_edge_before_end(const int32_t p_x[5], const int32_t p_y[5], const int32_t p_end_x[5], const int32_t p_end_y[5]) {
	uint32_t current = tree_nodes[edges_tree].current.right;
	if (!current) {
		return edges_tree;
	}
	int32_t a_x[5];
	int32_t a_y[5];
	bigint28_copy<5, 5>(a_x, p_end_x);
	bigint28_copy<5, 5>(a_y, p_end_y);
	bigint28_sub<5, 5>(a_x, p_x);
	bigint28_sub<5, 5>(a_y, p_y);
	while (true) {
		int32_t cross1[10];
		int32_t cross2[10];
		const Edge &edge = edges[tree_nodes[current].element];
		bigint28_mul<5, 5>(cross1, p_y, edge.dir_x);
		bigint28_mul<5, 5>(cross2, p_x, edge.dir_y);
		bigint28_sub<10, 10>(cross1, cross2);
		bigint28_sub<10, 10>(cross1, edge.cross);
		int sign = bigint28_sign<10>(cross1);
		if (sign > 0) {
			if (tree_nodes[current].current.right) {
				current = tree_nodes[current].current.right;
				continue;
			}
			return current;
		}
		if (sign < 0) {
			if (tree_nodes[current].current.left) {
				current = tree_nodes[current].current.left;
				continue;
			}
			return tree_nodes[current].current.prev;
		}
		// This is a best-effort attempt, since edges are not guaranteed
		// to be sorted by end.
		int32_t b_x[5];
		int32_t b_y[5];
		bigint28_copy<5, 5>(b_x, points[edges[tree_nodes[current].element].point_end].x);
		bigint28_copy<5, 5>(b_y, points[edges[tree_nodes[current].element].point_end].y);
		bigint28_sub<5, 5>(b_x, p_x);
		bigint28_sub<5, 5>(b_y, p_y);
		bigint28_mul<5, 5>(cross1, a_y, b_x);
		bigint28_mul<5, 5>(cross2, a_x, b_y);
		bigint28_sub<10, 10>(cross1, cross2);
		sign = bigint28_sign<10>(cross1);
		if (sign > 0) {
			if (tree_nodes[current].current.right) {
				current = tree_nodes[current].current.right;
				continue;
			}
			return current;
		}
		if (sign < 0 && tree_nodes[current].current.left) {
			current = tree_nodes[current].current.left;
			continue;
		}
		return tree_nodes[current].current.prev;
	}
}

uint32_t BentleyOttmann::get_edge_before_previous(uint32_t p_slice, const int32_t p_y[5]) {
	uint32_t current;
	if (tree_nodes[edges_tree].version == p_slice) {
		current = tree_nodes[edges_tree].previous.right;
	} else {
		current = tree_nodes[edges_tree].current.right;
	}
	if (!current) {
		return edges_tree;
	}
	while (true) {
		int32_t cross1[10];
		int32_t cross2[10];
		const Edge &edge = edges[tree_nodes[current].element];
		bigint28_mul<5, 5>(cross1, p_y, edge.dir_x);
		bigint28_mul<5, 5>(cross2, slices[p_slice].x, edge.dir_y);
		bigint28_sub<10, 10>(cross1, cross2);
		bigint28_sub<10, 10>(cross1, edge.cross);
		int sign = bigint28_sign<10>(cross1);
		if (sign > 0) {
			if (tree_nodes[current].version == p_slice) {
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
		if (tree_nodes[current].version == p_slice) {
			if (sign < 0 && tree_nodes[current].previous.left) {
				current = tree_nodes[current].previous.left;
				continue;
			}
			return tree_nodes[current].previous.prev;
		} else {
			if (sign < 0 && tree_nodes[current].current.left) {
				current = tree_nodes[current].current.left;
				continue;
			}
			return tree_nodes[current].current.prev;
		}
	}
}

int BentleyOttmann::edge_get_winding_previous(uint32_t p_treenode_edge, uint32_t p_version) {
	int winding = tree_nodes[p_treenode_edge].self_value;
	uint32_t current = p_treenode_edge;
	uint32_t parent;
	if (tree_nodes[p_treenode_edge].version == p_version) {
		parent = tree_nodes[p_treenode_edge].previous.parent;
		if (tree_nodes[tree_nodes[p_treenode_edge].previous.left].version == p_version) {
			winding += tree_nodes[tree_nodes[p_treenode_edge].previous.left].previous.sum_value;
		} else {
			winding += tree_nodes[tree_nodes[p_treenode_edge].previous.left].current.sum_value;
		}
	} else {
		parent = tree_nodes[p_treenode_edge].current.parent;
		if (tree_nodes[tree_nodes[p_treenode_edge].current.left].version == p_version) {
			winding += tree_nodes[tree_nodes[p_treenode_edge].current.left].previous.sum_value;
		} else {
			winding += tree_nodes[tree_nodes[p_treenode_edge].current.left].current.sum_value;
		}
	}
	while (parent) {
		if (tree_nodes[parent].version == p_version) {
			if (tree_nodes[parent].previous.right == current) {
				if (tree_nodes[tree_nodes[parent].previous.left].version == p_version) {
					winding += tree_nodes[tree_nodes[parent].previous.left].previous.sum_value + tree_nodes[parent].self_value;
				} else {
					winding += tree_nodes[tree_nodes[parent].previous.left].current.sum_value + tree_nodes[parent].self_value;
				}
			}
			current = parent;
			parent = tree_nodes[current].previous.parent;
		} else {
			if (tree_nodes[parent].current.right == current) {
				if (tree_nodes[tree_nodes[parent].current.left].version == p_version) {
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

void BentleyOttmann::check_intersection(uint32_t p_treenode_edge) {
	DEV_ASSERT(p_treenode_edge != edges_tree && tree_nodes[p_treenode_edge].current.next != edges_tree);
	Edge &edge1 = edges[tree_nodes[p_treenode_edge].element];
	Edge &edge2 = edges[tree_nodes[tree_nodes[p_treenode_edge].current.next].element];
	int32_t max[5];
	int32_t check[5];
	bigint28_copy<5, 5>(check, slices[edge1.next_check].x);
	bigint28_sub<5, 5>(check, slices[edge2.next_check].x);
	if (bigint28_sign<5>(check) < 0) {
		bigint28_copy<5, 5>(max, slices[edge1.next_check].x);
	} else {
		bigint28_copy<5, 5>(max, slices[edge2.next_check].x);
	}
	int32_t max_factor1[10];
	int32_t max_total1[15];
	int32_t max_factor2[10];
	int32_t max_total2[15];
	bigint28_mul<5, 5>(max_factor1, max, edge1.dir_y);
	bigint28_add<10, 10>(max_factor1, edge1.cross);
	bigint28_mul<10, 5>(max_total1, max_factor1, edge2.dir_x);
	bigint28_mul<5, 5>(max_factor2, max, edge2.dir_y);
	bigint28_add<10, 10>(max_factor2, edge2.cross);
	bigint28_mul<10, 5>(max_total2, max_factor2, edge1.dir_x);
	bigint28_sub<15, 15>(max_total2, max_total1);
	if (max_total2[14] >= 0) {
		return;
	}
	int32_t total1[15];
	int32_t total2[15];
	int32_t factor1[10];
	int32_t factor2[10];
	int32_t x_next_check[15];
	int32_t mod[10];
	bigint28_mul<10, 5>(total1, edge1.cross, edge2.dir_x);
	bigint28_mul<10, 5>(total2, edge2.cross, edge1.dir_x);
	bigint28_sub<15, 15>(total2, total1);
	bigint28_mul<5, 5>(factor1, edge1.dir_y, edge2.dir_x);
	bigint28_mul<5, 5>(factor2, edge2.dir_y, edge1.dir_x);
	bigint28_sub<10, 10>(factor1, factor2);
	bigint28_div<15, 10>(x_next_check, mod, total2, factor1);
	int32_t x_slice[5];
	bigint28_copy<5, 15>(x_slice, x_next_check);
	edge1.next_check = add_slice(x_slice);
	list_insert(edge1.listnode_check, slices[edge1.next_check].check_list);
}

uint32_t BentleyOttmann::tree_create(uint32_t p_element, int p_value) {
	TreeNode node;
	node.previous.prev = node.previous.next = node.current.prev = node.current.next = tree_nodes.size();
	node.element = p_element;
	node.self_value = p_value;
	tree_nodes.push_back(node);
	return node.current.next;
}

void BentleyOttmann::tree_insert(uint32_t p_insert_item, uint32_t p_insert_after, uint32_t p_version) {
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

void BentleyOttmann::tree_remove(uint32_t p_remove_item, uint32_t p_version) {
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

void BentleyOttmann::tree_rotate(uint32_t p_item, uint32_t p_version) {
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

void BentleyOttmann::tree_swap(uint32_t p_item1, uint32_t p_item2, uint32_t p_version) {
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

void BentleyOttmann::tree_version(uint32_t p_item, uint32_t p_version) {
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

void BentleyOttmann::tree_index_previous(uint32_t p_item, uint32_t p_version) {
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
