/*
 * librle implements the Return Link Encapsulation (RLE) protocol
 *
 * Copyright (C) 2015-2016, Thales Alenia Space France - All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file   test_rle_frag_ctxtless.c
 * @brief  Body file used for the fragmentation withou context tests.
 * @author Henrick Deschamps
 * @date   02/2016
 * @copyright
 *   Copyright (C) 2016, Thales Alenia Space France - All Rights Reserved
 */

#include "test_rle_frag_ctxtless.h"

#include "rle_transmitter.h"
#include "rle_ctx.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define DEFAULT_SDU_LEN 100

/**
 * @brief  Encapsulate SDU in ALPDU in a fragmentation buffer.
 *
 * @param[in,out]  transmitter  The transmitter used in encapsulation.
 * @param[in,out]  f_buff       The fragmentation buffer used for encapsulation.
 * @param[in]      sdu_len      The length of the SDU to encapsulate. DEFAULT_SDU_LEN by default
 *                              if NULL.
 *
 * @return  true if encapsulation is OK, else false.
 */
static int quick_encapsulation(struct rle_transmitter *const t,
                               struct rle_frag_buf *const f_buff,
                               const size_t *const sdu_len);

static int quick_encapsulation(struct rle_transmitter *const t,
                               struct rle_frag_buf *const f_buff,
                               const size_t *const sdu_len)
{
	bool output = false;
	enum rle_encap_status ret_encap;
	int ret;

	const struct rle_sdu sdu = {
		.buffer = (unsigned char *)payload_initializer,
		.size = sdu_len ? *sdu_len : DEFAULT_SDU_LEN,
		.protocol_type = 0x1234,
	};

	if (!f_buff) {
		PRINT_ERROR("Fragmentation buffer is NULL. Cannot test encap with too big SDU.");
		goto out;
	}

	ret = rle_frag_buf_init(f_buff);
	assert(ret == 0); /* cannot fail since f_buff is not NULL */

	if (rle_frag_buf_cpy_sdu(f_buff, &sdu) != 0) {
		PRINT_ERROR("Unable to copy SDU in fragmentation buffer.");
		goto out;
	}

	ret_encap = rle_encap_contextless(t, f_buff);
	if (ret_encap == RLE_ENCAP_OK) {
		output = true;
	}

out:

	return output;
}

bool test_frag_ctxtless_null_transmitter(void)
{
	bool output = false;
	enum rle_frag_status ret = RLE_FRAG_ERR;

	struct rle_frag_buf *f_buff = rle_frag_buf_new();

	const struct rle_config conf = {
		.allow_ptype_omission = 0,
		.use_compressed_ptype = 0,
		.allow_alpdu_crc = 0,
		.allow_alpdu_sequence_number = 1,
		.use_explicit_payload_header_map = 0,
		.implicit_protocol_type = 0x00,
		.implicit_ppdu_label_size = 0,
		.implicit_payload_label_size = 0,
		.type_0_alpdu_label_size = 0,
	};

	unsigned char *ppdu;
	size_t ppdu_len = 50;
	struct rle_transmitter *transmitter;

	PRINT_TEST("Special case : Fragmentation with a null transmitter.");

	transmitter = rle_transmitter_new(&conf);
	if (!transmitter) {
		PRINT_ERROR("Transmitter is NULL. Cannot test fragmentation with null transmitter.");
		goto out;
	}

	if (quick_encapsulation(transmitter, f_buff, NULL) != true) {
		PRINT_ERROR("Unable to encapsulate. Cannot test fragmentation with "
		            "null transmitter.");
		goto out;
	}

	ret = rle_frag_contextless(NULL, f_buff, &ppdu, &ppdu_len);
	if (ret == RLE_FRAG_ERR_NULL_TRMT) {
		output = true;
	}

out:

	if (transmitter) {
		rle_transmitter_destroy(&transmitter);
	}

	if (f_buff) {
		rle_frag_buf_del(&f_buff);
	}

	PRINT_TEST_STATUS(output);
	printf("\n");
	return output;
}

bool test_frag_ctxtless_null_f_buff(void)
{
	bool output = false;
	enum rle_frag_status ret = RLE_FRAG_ERR;

	struct rle_frag_buf *f_buff = rle_frag_buf_new();

	const struct rle_config conf = {
		.allow_ptype_omission = 0,
		.use_compressed_ptype = 0,
		.allow_alpdu_crc = 0,
		.allow_alpdu_sequence_number = 1,
		.use_explicit_payload_header_map = 0,
		.implicit_protocol_type = 0x00,
		.implicit_ppdu_label_size = 0,
		.implicit_payload_label_size = 0,
		.type_0_alpdu_label_size = 0,
	};
	struct rle_transmitter *transmitter;

	unsigned char *ppdu;
	size_t ppdu_len = 50;

	PRINT_TEST("Special case : Fragmentation with a null fragmentation buffer.");

	transmitter = rle_transmitter_new(&conf);
	if (!transmitter) {
		PRINT_ERROR("Transmitter is NULL. Cannot test fragmentation with null fragmentation "
		            "buffer.");
		goto out;
	}

	if (quick_encapsulation(transmitter, f_buff, NULL) != true) {
		PRINT_ERROR("Unable to encapsulate. Cannot test fragmentation with null fragmentation "
		            "buffer.");
		goto out;
	}

	ret = rle_frag_contextless(transmitter, NULL, &ppdu, &ppdu_len);

	if (ret == RLE_FRAG_ERR_NULL_F_BUFF) {
		output = true;
	}

out:

	if (transmitter) {
		rle_transmitter_destroy(&transmitter);
	}

	if (f_buff) {
		rle_frag_buf_del(&f_buff);
	}

	PRINT_TEST_STATUS(output);
	printf("\n");
	return output;
}

/**
 * @brief         Fragmentation test with a fragmentation buffer not initialized.
 *
 * @return        true if the RLE_FRAG_ERR_N_INIT_F_BUFF is raised, else false.
 */
bool test_frag_ctxtless_f_buff_not_init(void)
{
	bool output = false;
	enum rle_frag_status frag_ret;
	int ret;

	struct rle_frag_buf *f_buff = rle_frag_buf_new();

	const struct rle_config conf = {
		.allow_ptype_omission = 0,
		.use_compressed_ptype = 0,
		.allow_alpdu_crc = 0,
		.allow_alpdu_sequence_number = 1,
		.use_explicit_payload_header_map = 0,
		.implicit_protocol_type = 0x00,
		.implicit_ppdu_label_size = 0,
		.implicit_payload_label_size = 0,
		.type_0_alpdu_label_size = 0,
	};
	struct rle_transmitter *transmitter;

	unsigned char *ppdu;
	size_t ppdu_len = 50;

	PRINT_TEST("Special case : Fragmentation with a fragmentation buffer not initialized.");

	transmitter = rle_transmitter_new(&conf);
	if (!transmitter) {
		PRINT_ERROR("Transmitter is NULL. Cannot test fragmentation with fragmentation "
		            "buffer not initialized.");
		goto out;
	}

	ret = rle_frag_buf_init(f_buff);
	assert(ret == 0); /* cannot fail since f_buff is not NULL */

	frag_ret = rle_frag_contextless(transmitter, f_buff, &ppdu, &ppdu_len);
	if (frag_ret == RLE_FRAG_ERR_N_INIT_F_BUFF) {
		output = true;
	}

out:

	if (transmitter) {
		rle_transmitter_destroy(&transmitter);
	}

	if (f_buff) {
		rle_frag_buf_del(&f_buff);
	}

	PRINT_TEST_STATUS(output);
	printf("\n");
	return output;
}

/**
 * @brief         Fragmentation test with NULL PPDU buffer
 *
 * @return        true if error is reported, else false.
 */
bool test_frag_ctxtless_null_ppdu(void)
{
	bool output = false;
	enum rle_frag_status ret = RLE_FRAG_ERR;

	struct rle_frag_buf *f_buff = rle_frag_buf_new();

	const struct rle_config conf = {
		.allow_ptype_omission = 0,
		.use_compressed_ptype = 0,
		.allow_alpdu_crc = 0,
		.allow_alpdu_sequence_number = 1,
		.use_explicit_payload_header_map = 0,
		.implicit_protocol_type = 0x00,
		.implicit_ppdu_label_size = 0,
		.implicit_payload_label_size = 0,
		.type_0_alpdu_label_size = 0,
	};
	struct rle_transmitter *transmitter;

	size_t ppdu_len = 50;

	PRINT_TEST("Special case : Fragmentation with a null PPDU length.");

	transmitter = rle_transmitter_new(&conf);
	if (!transmitter) {
		PRINT_ERROR("Transmitter is NULL. Cannot test fragmentation with null PPDU length.");
		goto out;
	}

	if (quick_encapsulation(transmitter, f_buff, NULL) != true) {
		PRINT_ERROR("Unable to encapsulate. Cannot test fragmentation with null "
		            "PPDU length.");
		goto out;
	}

	ret = rle_frag_contextless(transmitter, f_buff, NULL, &ppdu_len);
	if (ret == RLE_FRAG_ERR) {
		output = true;
	}

out:

	if (transmitter) {
		rle_transmitter_destroy(&transmitter);
	}

	if (f_buff) {
		rle_frag_buf_del(&f_buff);
	}

	PRINT_TEST_STATUS(output);
	printf("\n");
	return output;
}


/**
 * @brief         Fragmentation test without length given as input.
 *
 *                Must not segfault.
 *
 * @return        true if exception is raised, else false.
 */
bool test_frag_ctxtless_no_len(void)
{
	bool output = false;
	enum rle_frag_status ret = RLE_FRAG_ERR;

	struct rle_frag_buf *f_buff = rle_frag_buf_new();

	const struct rle_config conf = {
		.allow_ptype_omission = 0,
		.use_compressed_ptype = 0,
		.allow_alpdu_crc = 0,
		.allow_alpdu_sequence_number = 1,
		.use_explicit_payload_header_map = 0,
		.implicit_protocol_type = 0x00,
		.implicit_ppdu_label_size = 0,
		.implicit_payload_label_size = 0,
		.type_0_alpdu_label_size = 0,
	};
	struct rle_transmitter *transmitter;

	unsigned char *ppdu;

	PRINT_TEST("Special case : Fragmentation with a null PPDU length.");

	transmitter = rle_transmitter_new(&conf);
	if (!transmitter) {
		PRINT_ERROR("Transmitter is NULL. Cannot test fragmentation with null PPDU length.");
		goto out;
	}

	if (quick_encapsulation(transmitter, f_buff, NULL) != true) {
		PRINT_ERROR("Unable to encapsulate. Cannot test fragmentation with null "
		            "PPDU length.");
		goto out;
	}

	ret = rle_frag_contextless(transmitter, f_buff, &ppdu, NULL);

	if (ret == RLE_FRAG_ERR) {
		output = true;
	}

out:

	if (transmitter) {
		rle_transmitter_destroy(&transmitter);
	}

	if (f_buff) {
		rle_frag_buf_del(&f_buff);
	}

	PRINT_TEST_STATUS(output);
	printf("\n");
	return output;
}

/**
 * @brief         Fragmentation test with a too small burst size.
 *
 * @return        true if the RLE_FRAG_ERR_BURST_TOO_SMALL is raised, else false.
 */
bool test_frag_ctxtless_too_small(void)
{
	bool output = false;
	enum rle_frag_status ret = RLE_FRAG_ERR;

	struct rle_frag_buf *f_buff = rle_frag_buf_new();

	const struct rle_config conf = {
		.allow_ptype_omission = 0,
		.use_compressed_ptype = 0,
		.allow_alpdu_crc = 0,
		.allow_alpdu_sequence_number = 1,
		.use_explicit_payload_header_map = 0,
		.implicit_protocol_type = 0x00,
		.implicit_ppdu_label_size = 0,
		.implicit_payload_label_size = 0,
		.type_0_alpdu_label_size = 0,
	};
	struct rle_transmitter *transmitter;

	unsigned char *ppdu;
	size_t ppdu_len = 1;

	PRINT_TEST("Special case : Fragmentation with a too small PPDU length.");

	transmitter = rle_transmitter_new(&conf);
	if (!transmitter) {
		PRINT_ERROR("Transmitter is NULL. Cannot test fragmentation with fragmentation too "
		            "small.");
		goto out;
	}

	if (quick_encapsulation(transmitter, f_buff, NULL) != true) {
		PRINT_ERROR("Unable to encapsulate. Cannot test fragmentation with fragmentation too "
		            "small.");
		goto out;
	}

	ret = rle_frag_contextless(transmitter, f_buff, &ppdu, &ppdu_len);

	if (ret == RLE_FRAG_ERR_BURST_TOO_SMALL) {
		output = true;
	}

out:

	if (transmitter) {
		rle_transmitter_destroy(&transmitter);
	}

	if (f_buff) {
		rle_frag_buf_del(&f_buff);
	}

	PRINT_TEST_STATUS(output);
	printf("\n");
	return output;
}

/**
 * @brief         Fragmentation test with a too big PPDU requested
 *
 * @return        true if error is raised, else false.
 */
bool test_frag_ctxtless_too_big(void)
{
	bool output = false;
	enum rle_frag_status ret = RLE_FRAG_ERR;

	struct rle_frag_buf *f_buff = rle_frag_buf_new();

	const struct rle_config conf = {
		.allow_ptype_omission = 0,
		.use_compressed_ptype = 0,
		.allow_alpdu_crc = 0,
		.allow_alpdu_sequence_number = 1,
		.use_explicit_payload_header_map = 0,
		.implicit_protocol_type = 0x00,
		.implicit_ppdu_label_size = 0,
		.implicit_payload_label_size = 0,
		.type_0_alpdu_label_size = 0,
	};
	struct rle_transmitter *transmitter;

	unsigned char *ppdu;
	size_t ppdu_len_good = 1000;
	size_t ppdu_len_wrong = 1000;
	const size_t sdu_len_good = 500;
	const size_t sdu_len_wrong = 2000;

	PRINT_TEST("Special case : Fragmentation with too big PPDU length.");

	transmitter = rle_transmitter_new(&conf);
	if (!transmitter) {
		PRINT_ERROR("Transmitter is NULL. Cannot test fragmentation with fragmentation "
		            "too big.");
		goto out;
	}

	if (quick_encapsulation(transmitter, f_buff, &sdu_len_good) != true) {
		PRINT_ERROR("Unable to encapsulate. Cannot test fragmentation with fragmentation "
		            "too big.");
		goto out;
	}

	ret = rle_frag_contextless(transmitter, f_buff, &ppdu, &ppdu_len_good);
	if (ret != RLE_FRAG_OK) {
		PRINT_ERROR("Unable to fragment good length. Cannot test fragmentation with "
		            "fragmentation too big.");
		goto out;
	}

	rle_transmitter_destroy(&transmitter);

	transmitter = rle_transmitter_new(&conf);
	if (!transmitter) {
		PRINT_ERROR("Transmitter is NULL. Cannot test fragmentation with fragmentation "
		            "too big.");
		goto out;
	}

	if (quick_encapsulation(transmitter, f_buff, &sdu_len_wrong) != true) {
		PRINT_ERROR("Unable to encapsulate. Cannot test fragmentation with fragmentation "
		            "too big.");
		goto out;
	}

	ret = rle_frag_contextless(transmitter, f_buff, &ppdu, &ppdu_len_wrong);

	if (ret == RLE_FRAG_ERR) {
		output = true;
	}

out:

	if (transmitter) {
		rle_transmitter_destroy(&transmitter);
	}

	if (f_buff) {
		rle_frag_buf_del(&f_buff);
	}

	PRINT_TEST_STATUS(output);
	printf("\n");
	return output;
}
