/**
 * @file   encap.c
 * @author Aurelien Castanie
 * @date   Mon Aug  6 14:15:24 CEST 2012
 *
 * @brief  RLE encapsulation functions
 *
 *
 */

#include <netinet/ip.h>
#include <arpa/inet.h>
#include "encap.h"
#include "constants.h"
#include "rle_ctx.h"
#include "zc_buffer.h"

static int create_header(struct rle_ctx_management *rle_ctx,
			void *data_buffer, size_t data_length)
{
	/* map RLE header to the already allocated buffer */
	zc_rle_header_complete *rle_hdr = (zc_rle_header_complete *)rle_ctx->buf;
	rle_hdr->ptrs->start = NULL;
	rle_hdr->ptrs->end = NULL;

	/* fill RLE complete header */
	rle_hdr->header.head.b.start_ind = 1;
	rle_hdr->header.head.b.end_ind = 1;
	rle_hdr->header.head.b.rle_packet_length = data_length;
	rle_hdr->header.head.b.label_type = RLE_LT_IMPLICIT_PROTO_TYPE; // TODO set the good LT from NCC, if pb C_ERROR
	rle_hdr->header.head.b.proto_type_supp = RLE_T_PROTO_TYPE_NO_SUPP;
	rle_hdr->header.proto_type = RLE_PROTO_TYPE_IP; // TODO set the good T from NCC, if pb C_ERROR

	/* set start & end PDU data pointers */
	rle_hdr->ptrs->start = (int *)data_buffer;
	rle_hdr->ptrs->end = (int *)(data_buffer + data_length);

	/* update rle context */
	rle_ctx_set_end_address(rle_ctx,
				(int *)(rle_hdr->ptrs->end + sizeof(int *)));
	rle_ctx_set_is_fragmented(rle_ctx, C_FALSE);
	rle_ctx_set_frag_counter(rle_ctx, 1);
	rle_ctx_set_use_crc(rle_ctx, C_FALSE);
	rle_ctx_set_pdu_length(rle_ctx, data_length);
	rle_ctx_set_remaining_pdu_length(rle_ctx, data_length);
	/* RLE packet length is the sum of packet label, protocol type & payload length */
	// TODO buggy test (payload_length - RLE_PROTO_TYPE_FIELD_SIZE) value !!!!!
	rle_ctx_set_rle_length(rle_ctx,
				(data_length + RLE_PROTO_TYPE_FIELD_SIZE));
	rle_ctx_set_proto_type(rle_ctx, rle_hdr->header.proto_type); // TODO
	rle_ctx_set_label_type(rle_ctx, rle_hdr->header.head.b.label_type); // TODO
	rle_ctx_set_qos_tag(rle_ctx, 0); // TODO

	return C_OK;
}

int encap_encapsulate_pdu(struct rle_ctx_management *rle_ctx,
		void *data_buffer, size_t data_length)
{
	if (encap_check_pdu_validity(data_buffer) == C_ERROR)
		return C_ERROR;

	if (create_header(rle_ctx, data_buffer, data_length) == C_ERROR)
		return C_ERROR;

	return C_OK;
}

int encap_check_pdu_validity(void *data_buffer)
{
	struct iphdr *ip_hdr = (struct iphdr *)data_buf;

	int ip_len = ntohs(ip_hdr->tot_len); // TODO use data_length ?

	/* check PDU size */
	if (ip_len > RLE_MAX_PDU_SIZE) {
		printf("ERROR %s:%s:%d: PDU too large for RLE encapsulation, size [%d]\n",
				__FILE__, __func__, __LINE__, ip_hdr->tot_len);
		return C_ERROR;
	}

	/* check ip version validity */
	if ((ip_hdr->version != IP_VERSION_4) && (ip_hdr->version != IP_VERSION_6)) {
		printf("ERROR %s:%s:%d: expecting IP version 4 or 6, version [%d] not supported\n",
				__FILE__, __func__, __LINE__, ip_hdr->version);
		return C_ERROR;
	}

	return C_OK;
}

