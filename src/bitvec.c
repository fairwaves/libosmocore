/* bit vector utility routines */

/* (C) 2009 by Harald Welte <laforge@gnumonks.org>
 * (C) 2012 Ivan Klyuchnikov
 * (C) 2015 by Sysmocom s.f.m.c. GmbH
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

/*! \addtogroup bitvec
 *  @{
 */

/*! \file bitvec.c
 *  \brief Osmocom bit vector abstraction
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include <osmocom/core/bits.h>
#include <osmocom/core/bitvec.h>

#define BITNUM_FROM_COMP(byte, bit)	((byte*8)+bit)

static inline unsigned int bytenum_from_bitnum(unsigned int bitnum)
{
	unsigned int bytenum = bitnum / 8;

	return bytenum;
}

/* convert ZERO/ONE/L/H to a bitmask at given pos in a byte */
static uint8_t bitval2mask(enum bit_value bit, uint8_t bitnum)
{
	int bitval;

	switch (bit) {
	case ZERO:
		bitval = (0 << bitnum);
		break;
	case ONE:
		bitval = (1 << bitnum);
		break;
	case L:
		bitval = ((0x2b ^ (0 << bitnum)) & (1 << bitnum));
		break;
	case H:
		bitval = ((0x2b ^ (1 << bitnum)) & (1 << bitnum));
		break;
	default:
		return 0;
	}
	return bitval;
}

/*! \brief check if the bit is 0 or 1 for a given position inside a bitvec
 *  \param[in] bv the bit vector on which to check
 *  \param[in] bitnr the bit number inside the bit vector to check
 *  \returns 
 */
enum bit_value bitvec_get_bit_pos(const struct bitvec *bv, unsigned int bitnr)
{
	unsigned int bytenum = bytenum_from_bitnum(bitnr);
	unsigned int bitnum = 7 - (bitnr % 8);
	uint8_t bitval;

	if (bytenum >= bv->data_len)
		return -EINVAL;

	bitval = bitval2mask(ONE, bitnum);

	if (bv->data[bytenum] & bitval)
		return ONE;

	return ZERO;
}

/*! \brief check if the bit is L or H for a given position inside a bitvec
 *  \param[in] bv the bit vector on which to check
 *  \param[in] bitnr the bit number inside the bit vector to check
 */
enum bit_value bitvec_get_bit_pos_high(const struct bitvec *bv,
					unsigned int bitnr)
{
	unsigned int bytenum = bytenum_from_bitnum(bitnr);
	unsigned int bitnum = 7 - (bitnr % 8);
	uint8_t bitval;

	if (bytenum >= bv->data_len)
		return -EINVAL;

	bitval = bitval2mask(H, bitnum);

	if ((bv->data[bytenum] & (1 << bitnum)) == bitval)
		return H;

	return L;
}

/*! \brief get the Nth set bit inside the bit vector
 *  \param[in] bv the bit vector to use
 *  \param[in] n the bit number to get
 *  \returns the bit number (offset) of the Nth set bit in \a bv
 */
unsigned int bitvec_get_nth_set_bit(const struct bitvec *bv, unsigned int n)
{
	unsigned int i, k = 0;

	for (i = 0; i < bv->data_len*8; i++) {
		if (bitvec_get_bit_pos(bv, i) == ONE) {
			k++;
			if (k == n)
				return i;
		}
	}

	return 0;
}

/*! \brief set a bit at given position in a bit vector
 *  \param[in] bv bit vector on which to operate
 *  \param[in] bitnr number of bit to be set
 *  \param[in] bit value to which the bit is to be set
 *  \returns 0 on success, negative value on error
 */
int bitvec_set_bit_pos(struct bitvec *bv, unsigned int bitnr,
			enum bit_value bit)
{
	unsigned int bytenum = bytenum_from_bitnum(bitnr);
	unsigned int bitnum = 7 - (bitnr % 8);
	uint8_t bitval;

	if (bytenum >= bv->data_len)
		return -EINVAL;

	/* first clear the bit */
	bitval = bitval2mask(ONE, bitnum);
	bv->data[bytenum] &= ~bitval;

	/* then set it to desired value */
	bitval = bitval2mask(bit, bitnum);
	bv->data[bytenum] |= bitval;

	return 0;
}

/*! \brief set the next bit inside a bitvec
 *  \param[in] bv bit vector to be used
 *  \param[in] bit value of the bit to be set
 *  \returns 0 on success, negative value on error
 */
int bitvec_set_bit(struct bitvec *bv, enum bit_value bit)
{
	int rc;

	rc = bitvec_set_bit_pos(bv, bv->cur_bit, bit);
	if (!rc)
		bv->cur_bit++;

	return rc;
}

/*! \brief get the next bit (low/high) inside a bitvec */
int bitvec_get_bit_high(struct bitvec *bv)
{
	int rc;

	rc = bitvec_get_bit_pos_high(bv, bv->cur_bit);
	if (rc >= 0)
		bv->cur_bit++;

	return rc;
}

/*! \brief set multiple bits (based on array of bitvals) at current pos
 *  \param[in] bv bit vector
 *  \param[in] bits array of \ref bit_value
 *  \param[in] count number of bits to set
 */
int bitvec_set_bits(struct bitvec *bv, enum bit_value *bits, unsigned int count)
{
	int i, rc;

	for (i = 0; i < count; i++) {
		rc = bitvec_set_bit(bv, bits[i]);
		if (rc)
			return rc;
	}

	return 0;
}

/*! \brief set multiple bits (based on numeric value) at current pos */
int bitvec_set_uint(struct bitvec *bv, unsigned int ui, unsigned int num_bits)
{
	int rc;
	unsigned i;
	for (i = 0; i < num_bits; i++) {
		int bit = 0;
		if (ui & (1u << (num_bits - i - 1)))
			bit = 1;
		rc = bitvec_set_bit(bv, bit);
		if (rc)
			return rc;
	}

	return 0;
}

/*! \brief get multiple bits (num_bits) from beginning of vector (MSB side) */
int16_t bitvec_get_int16_msb(const struct bitvec *bv, unsigned int num_bits)
{
	if (num_bits > 15 || bv->cur_bit < num_bits)
		return -EINVAL;

	if (num_bits < 9)
		return bv->data[0] >> (8 - num_bits);

	return osmo_load16be(bv->data) >> (16 - num_bits);
}

/*! \brief get multiple bits (based on numeric value) from current pos */
int bitvec_get_uint(struct bitvec *bv, unsigned int num_bits)
{
	int i;
	unsigned int ui = 0;

	for (i = 0; i < num_bits; i++) {
		int bit = bitvec_get_bit_pos(bv, bv->cur_bit);
		if (bit < 0)
			return bit;
		if (bit)
			ui |= (1 << (num_bits - i - 1));
		bv->cur_bit++;
	}

	return ui;
}

/*! \brief fill num_bits with \fill starting from the current position
 * returns 0 on success, negative otherwise (out of vector boundary)
 */
int bitvec_fill(struct bitvec *bv, unsigned int num_bits, enum bit_value fill)
{
	unsigned i, stop = bv->cur_bit + num_bits;
	for (i = bv->cur_bit; i < stop; i++)
		if (bitvec_set_bit(bv, fill) < 0)
			return -EINVAL;

	return 0;
}

/*! \brief pad all remaining bits up to num_bits */
int bitvec_spare_padding(struct bitvec *bv, unsigned int up_to_bit)
{
	int n = up_to_bit - bv->cur_bit + 1;
	if (n < 1)
		return 0;

	return bitvec_fill(bv, n, L);
}

/*! \brief find first bit set in bit vector */
int bitvec_find_bit_pos(const struct bitvec *bv, unsigned int n,
			enum bit_value val)
{
	unsigned int i;

	for (i = n; i < bv->data_len*8; i++) {
		if (bitvec_get_bit_pos(bv, i) == val)
			return i;
	}

	return -1;
}

/*! \brief get multiple bytes from current pos
 *  Assumes MSB first encoding.
 *  \param[in] bv bit vector
 *  \param[in] bytes array
 *  \param[in] count number of bytes to copy
 */
int bitvec_get_bytes(struct bitvec *bv, uint8_t *bytes, unsigned int count)
{
	int byte_offs = bytenum_from_bitnum(bv->cur_bit);
	int bit_offs = bv->cur_bit % 8;
	uint8_t c, last_c;
	int i;
	uint8_t *src;

	if (byte_offs + count + (bit_offs ? 1 : 0) > bv->data_len)
		return -EINVAL;

	if (bit_offs == 0) {
		memcpy(bytes, bv->data + byte_offs, count);
	} else {
		src = bv->data + byte_offs;
		last_c = *(src++);
		for (i = count; i > 0; i--) {
			c = *(src++);
			*(bytes++) =
				(last_c << bit_offs) |
				(c >> (8 - bit_offs));
			last_c = c;
		}
	}

	bv->cur_bit += count * 8;
	return 0;
}

/*! \brief set multiple bytes at current pos
 *  Assumes MSB first encoding.
 *  \param[in] bv bit vector
 *  \param[in] bytes array
 *  \param[in] count number of bytes to copy
 */
int bitvec_set_bytes(struct bitvec *bv, const uint8_t *bytes, unsigned int count)
{
	int byte_offs = bytenum_from_bitnum(bv->cur_bit);
	int bit_offs = bv->cur_bit % 8;
	uint8_t c, last_c;
	int i;
	uint8_t *dst;

	if (byte_offs + count + (bit_offs ? 1 : 0) > bv->data_len)
		return -EINVAL;

	if (bit_offs == 0) {
		memcpy(bv->data + byte_offs, bytes, count);
	} else if (count > 0) {
		dst = bv->data + byte_offs;
		/* Get lower bits of first dst byte */
		last_c = *dst >> (8 - bit_offs);
		for (i = count; i > 0; i--) {
			c = *(bytes++);
			*(dst++) =
				(last_c << (8 - bit_offs)) |
				(c >> bit_offs);
			last_c = c;
		}
		/* Overwrite lower bits of N+1 dst byte */
		*dst = (*dst & ((1 << (8 - bit_offs)) - 1)) |
			(last_c << (8 - bit_offs));
	}

	bv->cur_bit += count * 8;
	return 0;
}

struct bitvec *bitvec_alloc(unsigned int size, TALLOC_CTX *ctx)
{
	struct bitvec *bv = talloc_zero(ctx, struct bitvec);
	if (!bv)
		return NULL;

	bv->data = talloc_zero_array(bv, uint8_t, size);
	if (!(bv->data)) {
		talloc_free(bv);
		return NULL;
	}

	bv->data_len = size;
	bv->cur_bit = 0;
	return bv;
}

void bitvec_free(struct bitvec *bv)
{
	talloc_free(bv->data);
	talloc_free(bv);
}

unsigned int bitvec_pack(const struct bitvec *bv, uint8_t *buffer)
{
	unsigned int i = 0;
	for (i = 0; i < bv->data_len; i++)
		buffer[i] = bv->data[i];

	return i;
}

unsigned int bitvec_unpack(struct bitvec *bv, const uint8_t *buffer)
{
	unsigned int i = 0;
	for (i = 0; i < bv->data_len; i++)
		bv->data[i] = buffer[i];

	return i;
}


int bitvec_unhex(struct bitvec *bv, const char *src)
{
	unsigned i;
	unsigned val;
	unsigned write_index = 0;
	unsigned digits = bv->data_len * 2;

	for (i = 0; i < digits; i++) {
		if (sscanf(src + i, "%1x", &val) < 1) {
			return 1;
		}
		bitvec_write_field(bv, &write_index, val, 4);
	}
	return 0;
}

/*! \brief read part of the vector
 *  \param[in] bv The boolean vector to work on
 *  \param[in,out] read_index Where reading supposed to start in the vector
 *  \param[in] len How many bits to read from vector
 *  \returns read bits or negative value on error
 */
uint64_t bitvec_read_field(struct bitvec *bv, unsigned int *read_index, unsigned int len)
{
	unsigned int i;
	uint64_t ui = 0;
	bv->cur_bit = *read_index;

	for (i = 0; i < len; i++) {
		int bit = bitvec_get_bit_pos((const struct bitvec *)bv, bv->cur_bit);
		if (bit < 0)
			return bit;
		if (bit)
			ui |= ((uint64_t)1 << (len - i - 1));
		bv->cur_bit++;
	}
	*read_index += len;
	return ui;
}

/*! \brief write into the vector
 *  \param[in] bv The boolean vector to work on
 *  \param[in,out] write_index Where writing supposed to start in the vector
 *  \param[in] len How many bits to write
 *  \returns next write index or negative value on error
 */
int bitvec_write_field(struct bitvec *bv, unsigned int *write_index, uint64_t val, unsigned int len)
{
	unsigned int i;
	int rc;
	bv->cur_bit = *write_index;
	for (i = 0; i < len; i++) {
		int bit = 0;
		if (val & ((uint64_t)1 << (len - i - 1)))
			bit = 1;
		rc = bitvec_set_bit(bv, bit);
		if (rc)
			return rc;
	}
	*write_index += len;
	return 0;
}

/*! \brief convert enum to corresponding character */
char bit_value_to_char(enum bit_value v)
{
	switch (v) {
	case ZERO: return '0';
	case ONE: return '1';
	case L: return 'L';
	case H: return 'H';
	default: abort();
	}
}

/*! \brief prints bit vector to provided string
 * It's caller's responsibility to ensure that we won't shoot him in the foot:
 * the provided buffer should be at lest cur_bit + 1 bytes long
 */
void bitvec_to_string_r(const struct bitvec *bv, char *str)
{
	unsigned i, pos = 0;
	char *cur = str;
	for (i = 0; i < bv->cur_bit; i++) {
		if (0 == i % 8)
			*cur++ = ' ';
		*cur++ = bit_value_to_char(bitvec_get_bit_pos(bv, i));
		pos++;
	}
	*cur = 0;
}

/* we assume that x have at least 1 non-b bit */
static inline unsigned leading_bits(uint8_t x, bool b)
{
	if (b) {
		if (x < 0x80) return 0;
		if (x < 0xC0) return 1;
		if (x < 0xE0) return 2;
		if (x < 0xF0) return 3;
		if (x < 0xF8) return 4;
		if (x < 0xFC) return 5;
		if (x < 0xFE) return 6;
	} else {
		if (x > 0x7F) return 0;
		if (x > 0x3F) return 1;
		if (x > 0x1F) return 2;
		if (x > 0xF) return 3;
		if (x > 7) return 4;
		if (x > 3) return 5;
		if (x > 1) return 6;
	}
	return 7;
}
/*! \brief force bit vector to all 0 and current bit to the beginnig of the vector */
void bitvec_zero(struct bitvec *bv)
{
	bv->cur_bit = 0;
	memset(bv->data, 0, bv->data_len);
}

/*! \brief Return number (bits) of uninterrupted bit run in vector starting from the MSB
 *  \param[in] bv The boolean vector to work on
 *  \param[in] b The boolean, sequence of which is looked at from the vector start
 *  \returns Number of consecutive bits of \p b in \p bv
 */
unsigned bitvec_rl(const struct bitvec *bv, bool b)
{
	unsigned i;
	for (i = 0; i < (bv->cur_bit % 8 ? bv->cur_bit / 8 + 1 : bv->cur_bit / 8); i++) {
		if ( (b ? 0xFF : 0) != bv->data[i])
			return i * 8 + leading_bits(bv->data[i], b);
	}

	return bv->cur_bit;
}

/*! \brief Shifts bitvec to the left, n MSB bits lost */
void bitvec_shiftl(struct bitvec *bv, unsigned n)
{
	if (0 == n)
		return;
	if (n >= bv->cur_bit) {
		bitvec_zero(bv);
		return;
	}

	memmove(bv->data, bv->data + n / 8, bv->data_len - n / 8);

	uint8_t tmp[2];
	unsigned i;
	for (i = 0; i < bv->data_len - 2; i++) {
		uint16_t t = osmo_load16be(bv->data + i);
		osmo_store16be(t << (n % 8), &tmp);
		bv->data[i] = tmp[0];
	}

	bv->data[bv->data_len - 1] <<= (n % 8);
	bv->cur_bit -= n;
}

/*! @} */
