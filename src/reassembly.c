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
 * @file   reassembly.c
 * @brief  RLE reassembly functions
 * @author Aurelien Castanie, Henrick Deschamps
 * @date   03/2015
 * @copyright
 *   Copyright (C) 2015, Thales Alenia Space France - All Rights Reserved
 */

#include "reassembly.h"
#include "rle_receiver.h"
#include "constants.h"
#include "header.h"
#include "trailer.h"
#include "crc.h"
#include "rle_header_proto_type_field.h"

#ifndef __KERNEL__

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <net/ethernet.h>

#else

#include <linux/string.h>

#endif


/*------------------------------------------------------------------------------------------------*/
/*--------------------------------- PRIVATE CONSTANTS AND MACROS ---------------------------------*/
/*------------------------------------------------------------------------------------------------*/

#define MODULE_ID RLE_MOD_ID_REASSEMBLY


static bool reassembly_insert_vlan_ptype(const uint8_t *const sdu_frag,
                                         const size_t sdu_frag_len,
                                         struct rle_sdu *const reassembled_sdu)
__attribute__((warn_unused_result, nonnull(1, 3)));


/**
 * @brief Insert the suppressed VLAN protocol type in the given VLAN/IP SDU
 *
 * This function helps handling the special case for VLAN with embedded IPv4/IPv6:
 * the protocol field of the VLAN header is suppressed by the RLE transmitter and
 * shall be rebuilt by the RLE receiver according to the first 4 bits of the IP
 * payload.
 *
 * @param      sdu_frag          The combined SDU fragments extracted from PPDUs
 * @param      sdu_frag_len      The length of the combined SDU fragments extracted from PPDUs
 * @param[out] reassembled_sdu   The reassembled SDU with the VLAN protocol type inserted
 * @return                       true if insertion was successful,
 *                               false if frame is too short or malformed
 */
static bool reassembly_insert_vlan_ptype(const uint8_t *const sdu_frag,
                                         const size_t sdu_frag_len,
                                         struct rle_sdu *const reassembled_sdu)
{
	/* minimum SDU length:
	 *    Ethernet header + VLAN header w/o protocol field + 1 byte of IP header */
	const size_t comp_eth_vlan_len =
		sizeof(struct ether_header) + sizeof(struct vlan_hdr) - sizeof(uint16_t);
	const size_t sdu_min_len = comp_eth_vlan_len + 1;
	uint16_t vlan_uncomp_ptype;

	RLE_DEBUG("compressed protocol type 0x%02x requires to insert back the "
	          "protocol type in the VLAN header with information from the IP "
	          "payload", RLE_PROTO_TYPE_VLAN_COMP_WO_PTYPE_FIELD);

	/* drop frames that are too short: the protocol type cannot be deduced from the VLAN payload */
	if (sdu_frag_len < sdu_min_len) {
		RLE_ERR("ALPDU fragment is too short to deduce the VLAN protocol type "
		        "from the first IP byte: %zu bytes available, %zu bytes required "
		        "at least\n", sdu_frag_len, sdu_min_len);
		goto error;
	}

	{
		const struct ether_header *const eth_hdr = (struct ether_header *)sdu_frag;
		const uint16_t eth_proto_type = ntohs(eth_hdr->ether_type);

		const uint8_t *const vlan_hdr = (uint8_t *)(eth_hdr + 1);

		const uint8_t *const vlan_payload =
			vlan_hdr + sizeof(struct vlan_hdr) - sizeof(uint16_t);
		const uint8_t ip_version = (vlan_payload[0] >> 4) & 0x0f;

		/* drop frames with unexpected protocol type in Ethernet frame: it should be VLAN */
		if (eth_proto_type != RLE_PROTO_TYPE_VLAN_UNCOMP) {
			RLE_ERR("failed to deduce VLAN protocol type from VLAN payload: unknown "
			        "Ethernet protocol type 0x%04x instead of VLAN\n", eth_proto_type);
			goto error;
		}

		/* deduce VLAN protocol type from the first 4 bits of the VLAN payload */
		switch (ip_version) {
		case 4:
			vlan_uncomp_ptype = RLE_PROTO_TYPE_IPV4_UNCOMP;
			break;
		case 6:
			vlan_uncomp_ptype = RLE_PROTO_TYPE_IPV6_UNCOMP;
			break;
		default:
			RLE_ERR("failed to deduce VLAN protocol type from VLAN payload: "
			        "unknown IP version %u\n", ip_version);
			goto error;
		}
		RLE_DEBUG("IP version %u detected in VLAN payload", ip_version);
	}

	reassembled_sdu->size = sdu_frag_len + sizeof(uint16_t);
	reassembled_sdu->protocol_type = RLE_PROTO_TYPE_VLAN_UNCOMP;

	/* copy the Ethernet header and the first part of the VLAN header */
	memcpy(reassembled_sdu->buffer, sdu_frag, comp_eth_vlan_len);

	/* insert the protocol type field in the VLAN header */
	{
		struct ether_header *const eth_hdr_new =
			(struct ether_header *)reassembled_sdu->buffer;
		struct vlan_hdr *const vlan_hdr_new = (struct vlan_hdr *)(eth_hdr_new + 1);
		vlan_hdr_new->tpid = htons(vlan_uncomp_ptype);
	}

	/* copy the VLAN payload */
	memcpy(reassembled_sdu->buffer + sizeof(struct ether_header) + sizeof(struct vlan_hdr),
	       sdu_frag + comp_eth_vlan_len, sdu_frag_len - comp_eth_vlan_len);

	return true;

error:
	return false;
}


/*------------------------------------------------------------------------------------------------*/
/*------------------------------------ PUBLIC FUNCTIONS CODE -------------------------------------*/
/*------------------------------------------------------------------------------------------------*/

int reassembly_comp_ppdu(struct rle_receiver *_this,
                         unsigned char *const ppdu,
                         const size_t ppdu_length,
                         struct rle_sdu *const reassembled_sdu)
{
	int ret = C_ERROR;
	unsigned char *alpdu_frag;
	size_t alpdu_frag_len;
	const unsigned char *sdu_frag;
	size_t sdu_frag_len;
	uint16_t ptype;
	uint8_t comp_ptype;
	rle_ppdu_hdr_comp_t *const header = (rle_ppdu_hdr_comp_t *)ppdu;

#ifdef TIME_DEBUG
	struct timeval tv_start = { .tv_sec = 0L, .tv_usec = 0L };
	struct timeval tv_end = { .tv_sec = 0L, .tv_usec = 0L };
	struct timeval tv_delta;
	gettimeofday(&tv_start, NULL);
#endif

	RLE_DEBUG("handle PPDU COMP");

	comp_ppdu_extract_alpdu_frag(ppdu, ppdu_length, &alpdu_frag, &alpdu_frag_len);

	if (alpdu_frag_len == 0) {
		RLE_WARN("warning: 0-byte ALPDU in Complete PPDU");
	}

	if (rle_comp_ppdu_hdr_get_is_suppressed(header)) {
		/* protocol type is suppressed */
		if (rle_comp_ppdu_hdr_get_is_signal(header)) {
			/* ALPDU label type 3 means that the implicit protocol type is L2S */
			ret = signal_alpdu_extract_sdu_frag(alpdu_frag, alpdu_frag_len,
			                                    &ptype, &comp_ptype,
			                                    &sdu_frag, &sdu_frag_len);
		} else {
			/* ALPDU label type 0, 1 or 2 mean that the implicit protocol type
			 * is given by the configuration */
			ret = suppr_alpdu_extract_sdu_frag(alpdu_frag, alpdu_frag_len,
			                                   &ptype, &comp_ptype,
			                                   &sdu_frag, &sdu_frag_len,
			                                   &_this->conf);
		}
	} else if (_this->conf.use_compressed_ptype) {
		/* protocol type is not suppressed, but compressed */
		ret = comp_alpdu_extract_sdu_frag(alpdu_frag, alpdu_frag_len,
		                                  &ptype, &comp_ptype,
		                                  &sdu_frag, &sdu_frag_len, NULL);
	} else {
		/* protocol type is not suppressed nor compressed */
		ret = uncomp_alpdu_extract_sdu_frag(alpdu_frag, alpdu_frag_len,
		                                    &ptype, &comp_ptype,
		                                    &sdu_frag, &sdu_frag_len);
	}

	if (ret) {
		ret = C_ERROR;
		goto out;
	}

	if (comp_ptype != RLE_PROTO_TYPE_VLAN_COMP_WO_PTYPE_FIELD) {
		/* SDU is complete */
		reassembled_sdu->size = sdu_frag_len;
		reassembled_sdu->protocol_type = ptype;
		memcpy(reassembled_sdu->buffer, sdu_frag, sdu_frag_len);
	} else {
		assert(ptype == RLE_PROTO_TYPE_VLAN_UNCOMP);

		/* special case for VLAN with embedded IPv4/IPv6: the protocol field of the VLAN
		 * header is suppressed by the RLE transmitter and shall be rebuilt by the RLE
		 * receiver according to the first 4 bits of the IP payload */
		if (!reassembly_insert_vlan_ptype(sdu_frag, sdu_frag_len, reassembled_sdu)) {
			RLE_ERR("failed to insert VLAN protocol type in Ethernet/VLAN/IP headers");
			ret = C_ERROR;
			goto out;
		}
	}

	ret = C_REASSEMBLY_OK;

out:
#ifdef TIME_DEBUG
	gettimeofday(&tv_end, NULL);
	tv_delta.tv_sec = tv_end.tv_sec - tv_start.tv_sec;
	tv_delta.tv_usec = tv_end.tv_usec - tv_start.tv_usec;
	RLE_DEBUG("duration [%04ld.%06ld].", tv_delta.tv_sec, tv_delta.tv_usec);
#endif

	return ret;
}

int reassembly_start_ppdu(struct rle_receiver *_this,
                          unsigned char ppdu[],
                          const size_t ppdu_length,
                          int *const index_ctx)
{
	int ret = C_ERROR;
	unsigned char *alpdu_frag;
	size_t alpdu_frag_len;
	const unsigned char *sdu_frag;
	size_t sdu_frag_len;
	uint16_t ptype;
	uint8_t comp_ptype;
	rle_rasm_buf_t *rasm_buf;
	size_t alpdu_total_len;
	const rle_ppdu_hdr_start_t *header;
	struct rle_ctx_mngt *rle_ctx;
	size_t sdu_total_len;
	int is_crc_used;
	size_t alpdu_hdr_len;
	size_t alpdu_trailer_len;
	int ret_extract;

#ifdef TIME_DEBUG
	struct timeval tv_start = { .tv_sec = 0L, .tv_usec = 0L };
	struct timeval tv_end = { .tv_sec = 0L, .tv_usec = 0L };
	struct timeval tv_delta;
	gettimeofday(&tv_start, NULL);
#endif

	*index_ctx = rle_start_ppdu_hdr_get_frag_id((rle_ppdu_hdr_start_t *)ppdu);
	RLE_DEBUG("START: fragment_id 0x%0x", *index_ctx);
	RLE_DEBUG("handle PPDU START for context with ID %d", *index_ctx);
	assert((*index_ctx) >= 0 && (*index_ctx) <= RLE_MAX_FRAG_ID);

	rle_ctx = &_this->rle_ctx_man[*index_ctx];
	rle_ctx->current_counter = ppdu_length;
	rasm_buf = (rle_rasm_buf_t *)_this->rle_ctx_man[*index_ctx].buff;

	rle_ctx_incr_counter_in(rle_ctx);
	rle_ctx_incr_counter_bytes_in(rle_ctx, ppdu_length);

	if (is_context_free(_this, *index_ctx) == false) {
		RLE_ERR("invalid Start on context not free, frag id [%d].", *index_ctx);
		/* Context is not free, whereas it must be. an error must have occured. */
		/* Freeing context, updating stats, and restarting receiving. */
		goto out;
	}

	set_nonfree_frag_ctx(_this, *index_ctx);

	start_ppdu_extract_alpdu_frag(ppdu, ppdu_length, &alpdu_frag, &alpdu_frag_len,
	                              &alpdu_total_len, &is_crc_used);
	header = (const rle_ppdu_hdr_start_t *)ppdu;

	sdu_total_len = alpdu_total_len;

	if (rle_start_ppdu_hdr_get_is_suppressed(header)) {
		/* protocol type is suppressed */
		if (rle_start_ppdu_hdr_get_is_signal(header)) {
			/* ALPDU label type 3 means that the implicit protocol type is L2S */
			ret_extract =
				signal_alpdu_extract_sdu_frag(alpdu_frag, alpdu_frag_len,
				                              &ptype, &comp_ptype,
				                              &sdu_frag, &sdu_frag_len);
		} else {
			ret_extract =
				suppr_alpdu_extract_sdu_frag(alpdu_frag, alpdu_frag_len,
				                             &ptype, &comp_ptype,
				                             &sdu_frag, &sdu_frag_len,
				                             &_this->conf);
		}
		alpdu_hdr_len = 0;
	} else if (_this->conf.use_compressed_ptype) {
		/* protocol type is not suppressed, but compressed */
		ret_extract =
			comp_alpdu_extract_sdu_frag(alpdu_frag, alpdu_frag_len,
			                            &ptype, &comp_ptype,
			                            &sdu_frag, &sdu_frag_len,
			                            &alpdu_hdr_len);
	} else {
		/* protocol type is not suppressed nor compressed */
		ret_extract =
			uncomp_alpdu_extract_sdu_frag(alpdu_frag, alpdu_frag_len,
			                              &ptype, &comp_ptype,
			                              &sdu_frag, &sdu_frag_len);
		alpdu_hdr_len = sizeof(rle_alpdu_hdr_uncomp_t);
	}
	if (ret_extract) {
		goto out;
	}

	if (sdu_frag_len > sdu_total_len) {
		RLE_ERR("PPDU START with frag id %d contains more SDU bytes than expected in total "
		        "(%zu bytes in fragment, %zu bytes expected in total)", *index_ctx,
		        sdu_frag_len, sdu_total_len);
		goto out;
	}
	sdu_total_len -= alpdu_hdr_len;

	if (is_crc_used) {
		RLE_DEBUG("ALPDU trailer is CRC");
		alpdu_trailer_len = sizeof(rle_alpdu_crc_trailer_t);
	} else {
		RLE_DEBUG("ALPDU trailer is seqnum");
		alpdu_trailer_len = sizeof(rle_alpdu_seqno_trailer_t);
	}
	if (alpdu_trailer_len > sdu_total_len) {
		RLE_ERR("PPDU START with frag id %d contains too few bytes for the ALPDU trailer "
		        "(at least %zu bytes needed, but only %zu bytes available", *index_ctx,
		        alpdu_trailer_len, sdu_total_len);
		goto out;
	}
	sdu_total_len -= alpdu_trailer_len;

	rle_ctx_set_use_crc(rle_ctx, is_crc_used);
	RLE_DEBUG("ALPDU trailer is %s",
	          rle_ctx_get_use_crc(rle_ctx) ? "CRC" : "seqnum");

	if (sdu_frag_len > sdu_total_len) {
		RLE_ERR("PPDU START with frag id %d contains more SDU bytes than expected in total "
		        "(%zu bytes in fragment, %zu bytes expected in total)", *index_ctx,
		        sdu_frag_len, sdu_total_len);
		goto out;
	}
	rasm_buf_init(rasm_buf);
	rasm_buf_sdu_put(rasm_buf, sdu_total_len);
	rasm_buf_sdu_frag_put(rasm_buf, sdu_frag_len);
	rasm_buf->sdu_info.protocol_type = ptype;
	rasm_buf->comp_protocol_type = comp_ptype;
	rasm_buf->sdu_info.size = sdu_total_len;
	rasm_buf_cpy_sdu_frag(rasm_buf, sdu_frag);

	ret = C_OK;

out:

	if (ret != C_OK) {
		rle_ctx_incr_counter_dropped(rle_ctx);
		rle_ctx_incr_counter_lost(rle_ctx, 1);
		rle_ctx_incr_counter_bytes_dropped(rle_ctx, rle_ctx->current_counter);
		rle_receiver_free_context(_this, *index_ctx);
	}

#ifdef TIME_DEBUG
	gettimeofday(&tv_end, NULL);
	tv_delta.tv_sec = tv_end.tv_sec - tv_start.tv_sec;
	tv_delta.tv_usec = tv_end.tv_usec - tv_start.tv_usec;
	RLE_DEBUG("duration [%04ld.%06ld].", tv_delta.tv_sec, tv_delta.tv_usec);
#endif

	return ret;
}

int reassembly_cont_ppdu(struct rle_receiver *_this,
                         const unsigned char ppdu[],
                         const size_t ppdu_length,
                         int *const index_ctx)
{
	int ret = C_ERROR;
	const unsigned char *alpdu_frag;
	size_t alpdu_frag_len;
	const unsigned char *sdu_frag;
	size_t sdu_frag_len;
	rle_rasm_buf_t *rasm_buf;
	struct rle_ctx_mngt *rle_ctx;

#ifdef TIME_DEBUG
	struct timeval tv_start = { .tv_sec = 0L, .tv_usec = 0L };
	struct timeval tv_end = { .tv_sec = 0L, .tv_usec = 0L };
	struct timeval tv_delta;
	gettimeofday(&tv_start, NULL);
#endif

	*index_ctx = rle_cont_end_ppdu_hdr_get_frag_id((rle_ppdu_hdr_cont_end_t *)ppdu);
	RLE_DEBUG("CONT: fragment_id 0x%0x", *index_ctx);
	RLE_DEBUG("handle PPDU CONT for context with ID %d", *index_ctx);
	assert((*index_ctx >= 0) && (*index_ctx <= RLE_MAX_FRAG_ID));

	rle_ctx = &_this->rle_ctx_man[*index_ctx];
	rle_ctx->current_counter += ppdu_length;
	rasm_buf = (rle_rasm_buf_t *)_this->rle_ctx_man[*index_ctx].buff;

	rle_ctx_incr_counter_bytes_in(rle_ctx, ppdu_length);

	if (is_context_free(_this, *index_ctx) == true) {
		RLE_ERR("invalid Cont on context free, frag id [%d].", *index_ctx);
		/* Context is free, whereas it must not. an error must have occured. */
		/* Freeing context and updating stats. At least one packet is partialy lost.*/
		goto out;
	}

	RLE_DEBUG("ALPDU trailer is %s",
	          rle_ctx_get_use_crc(rle_ctx) ? "CRC" : "seqnum");

	cont_end_ppdu_extract_alpdu_frag(ppdu, ppdu_length, &alpdu_frag,
	                                 &alpdu_frag_len);

	if (alpdu_frag_len == 0) {
		RLE_WARN("warning: 0-byte ALPDU in PPDU CONT");
	}

	sdu_frag = alpdu_frag;
	sdu_frag_len = alpdu_frag_len;

	if (rasm_buf_get_reassembled_sdu_len(rasm_buf) + sdu_frag_len >
	    rasm_buf_get_sdu_len(rasm_buf)) {
		RLE_ERR("PPDU CONT with frag id %d contains more SDU bytes than expected in total "
		        "(%zu bytes already received, %zu bytes in fragment, %zu bytes expected "
		        "in total)", *index_ctx, rasm_buf_get_reassembled_sdu_len(rasm_buf),
		        sdu_frag_len, rasm_buf_get_sdu_len(rasm_buf));
		goto out;
	}
	rasm_buf_init_sdu_frag(rasm_buf);
	rasm_buf_sdu_frag_put(rasm_buf, sdu_frag_len);
	rasm_buf_cpy_sdu_frag(rasm_buf, sdu_frag);

	ret = C_OK;

out:

	if (ret != C_OK) {
		rle_ctx_incr_counter_dropped(rle_ctx);
		rle_ctx_incr_counter_lost(rle_ctx, 1);
		rle_ctx_incr_counter_bytes_dropped(rle_ctx, rle_ctx->current_counter);
		rle_receiver_free_context(_this, *index_ctx);
	}

#ifdef TIME_DEBUG
	gettimeofday(&tv_end, NULL);
	tv_delta.tv_sec = tv_end.tv_sec - tv_start.tv_sec;
	tv_delta.tv_usec = tv_end.tv_usec - tv_start.tv_usec;
	RLE_DEBUG("duration [%04ld.%06ld].", tv_delta.tv_sec, tv_delta.tv_usec);
#endif

	return ret;
}

int reassembly_end_ppdu(struct rle_receiver *_this,
                        const unsigned char ppdu[],
                        const size_t ppdu_length,
                        int *const index_ctx,
                        struct rle_sdu *const reassembled_sdu)
{
	int ret = C_ERROR;
	const unsigned char *alpdu_frag;
	size_t alpdu_frag_len;
	const unsigned char *sdu_frag;
	size_t sdu_frag_len;
	rle_rasm_buf_t *rasm_buf;
	struct rle_ctx_mngt *rle_ctx;
	const rle_alpdu_trailer_t *rle_trailer = NULL;
	size_t rle_trailer_len;
	size_t lost_packets = 0;

#ifdef TIME_DEBUG
	struct timeval tv_start = { .tv_sec = 0L, .tv_usec = 0L };
	struct timeval tv_end = { .tv_sec = 0L, .tv_usec = 0L };
	struct timeval tv_delta;
	gettimeofday(&tv_start, NULL);
#endif

	*index_ctx = rle_cont_end_ppdu_hdr_get_frag_id((rle_ppdu_hdr_cont_end_t *)ppdu);
	RLE_DEBUG("END: fragment_id 0x%0x", *index_ctx);
	RLE_DEBUG("handle PPDU END for context with ID %d", *index_ctx);
	assert((*index_ctx >= 0) && (*index_ctx <= RLE_MAX_FRAG_ID));

	rle_ctx = &_this->rle_ctx_man[*index_ctx];
	rle_ctx->current_counter += ppdu_length;
	rasm_buf = (rle_rasm_buf_t *)_this->rle_ctx_man[*index_ctx].buff;

	rle_ctx_incr_counter_bytes_in(rle_ctx, ppdu_length);

	if (is_context_free(_this, *index_ctx) == true) {
		RLE_ERR("invalid End on context free, frag id [%d].", *index_ctx);
		/* Context is free, whereas it must not. an error must have occured. */
		/* Freeing context and updating stats. At least one packet is partialy lost.*/
		rle_ctx_incr_counter_dropped(rle_ctx);
		rle_ctx_incr_counter_lost(rle_ctx, 1);
		rle_ctx_incr_counter_bytes_dropped(rle_ctx, rle_ctx->current_counter);
		rle_receiver_free_context(_this, *index_ctx);

		goto out;
	}

	cont_end_ppdu_extract_alpdu_frag(ppdu, ppdu_length, &alpdu_frag, &alpdu_frag_len);

	if (rle_ctx_get_use_crc(rle_ctx)) {
		RLE_DEBUG("ALPDU trailer is CRC");
		rle_trailer_len = sizeof(rle_alpdu_crc_trailer_t);
	} else {
		RLE_DEBUG("ALPDU trailer is seqnum");
		rle_trailer_len = sizeof(rle_alpdu_seqno_trailer_t);
	}
	if (alpdu_frag_len < rle_trailer_len) {
		RLE_ERR("PPDU END does not contain enough bytes for the trailer: %zu bytes "
		        "available while at least %zu bytes required", alpdu_frag_len,
		        rle_trailer_len);
		goto out;
	}
	sdu_frag = alpdu_frag;
	sdu_frag_len = alpdu_frag_len - rle_trailer_len;
	rle_trailer = (rle_alpdu_trailer_t *)(sdu_frag + sdu_frag_len);

	assert(rasm_buf->sdu_info.size == rasm_buf_get_sdu_len(rasm_buf));

	if (rasm_buf_get_reassembled_sdu_len(rasm_buf) + sdu_frag_len >
	    rasm_buf_get_sdu_len(rasm_buf)) {
		RLE_ERR("PPDU END with frag id %d contains more SDU bytes than expected in total "
		        "(%zu bytes already received, %zu bytes in fragment, %zu bytes expected "
		        "in total)", *index_ctx, rasm_buf_get_reassembled_sdu_len(rasm_buf),
		        sdu_frag_len, rasm_buf_get_sdu_len(rasm_buf));
		goto out;
	}
	rasm_buf_init_sdu_frag(rasm_buf);
	rasm_buf_sdu_frag_put(rasm_buf, sdu_frag_len);
	rasm_buf_cpy_sdu_frag(rasm_buf, sdu_frag);

	if (rasm_buf_get_sdu_len(rasm_buf) > rasm_buf_get_reassembled_sdu_len(rasm_buf)) {
		RLE_ERR("END PPDU received but %zu bytes still missing (%zu-byte SDU expected, "
		        "but only %zu bytes received)",
		        rasm_buf_get_sdu_len(rasm_buf) - rasm_buf_get_reassembled_sdu_len(rasm_buf),
		        rasm_buf_get_sdu_len(rasm_buf), rasm_buf_get_reassembled_sdu_len(rasm_buf));
		goto out;
	}

	if (rasm_buf->comp_protocol_type != RLE_PROTO_TYPE_VLAN_COMP_WO_PTYPE_FIELD) {
		/* SDU is complete */
		reassembled_sdu->size = rasm_buf->sdu_info.size;
		reassembled_sdu->protocol_type = rasm_buf->sdu_info.protocol_type;
		memcpy(reassembled_sdu->buffer, rasm_buf->sdu_info.buffer, reassembled_sdu->size);
		RLE_DEBUG("%zu-byte SDU with protocol 0x%04x is complete",
		          reassembled_sdu->size, reassembled_sdu->protocol_type);
	} else {
		assert(rasm_buf->sdu_info.protocol_type == RLE_PROTO_TYPE_VLAN_UNCOMP);

		RLE_DEBUG("%zu-byte SDU with protocol 0x%04x shall be modified to insert "
		          "the Protocol Type field in the VLAN header",
		          rasm_buf->sdu_info.size, rasm_buf->sdu_info.protocol_type);

		/* special case for VLAN with embedded IPv4/IPv6: the protocol field of the VLAN
		 * header is suppressed by the RLE transmitter and shall be rebuilt by the RLE
		 * receiver according to the first 4 bits of the IP payload */
		if (!reassembly_insert_vlan_ptype(rasm_buf->sdu.start, rasm_buf->sdu_info.size,
		                                  reassembled_sdu)) {
			RLE_ERR("failed to insert VLAN protocol type in Ethernet/VLAN/IP headers");
			goto out;
		}
	}

	if (check_alpdu_trailer(rle_trailer, reassembled_sdu, rle_ctx,
	                        &(_this->is_ctx_seqnum_init[*index_ctx]), &lost_packets) != 0) {
		RLE_ERR("Wrong RLE trailer.");
		goto out;
	}

	/* update link status */
	rle_ctx_incr_counter_bytes_ok(rle_ctx, reassembled_sdu->size);
	rle_ctx_incr_counter_ok(rle_ctx);

	ret = C_REASSEMBLY_OK;

out:

	if (ret != C_REASSEMBLY_OK) {
		rle_ctx_incr_counter_dropped(rle_ctx);
		rle_ctx_incr_counter_lost(rle_ctx, lost_packets);
		rle_ctx_incr_counter_bytes_dropped(rle_ctx, rle_ctx->current_counter);
	}

	rle_receiver_free_context(_this, *index_ctx);

#ifdef TIME_DEBUG
	gettimeofday(&tv_end, NULL);
	tv_delta.tv_sec = tv_end.tv_sec - tv_start.tv_sec;
	tv_delta.tv_usec = tv_end.tv_usec - tv_start.tv_usec;
	RLE_DEBUG("duration [%04ld.%06ld].", tv_delta.tv_sec, tv_delta.tv_usec);
#endif

	return ret;
}
