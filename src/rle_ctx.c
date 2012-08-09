/**
 * @file   rle_ctx.c
 * @author Aurelien Castanie
 * @date   Mon Aug  6 16:43:44 CEST 2012
 *
 * @brief  RLE transmitter functions
 *
 *
 */

#include <errno.h>
#include "rle_ctx.h"
#include "constants.h"

static void flush(struct rle_ctx_management *_this)
{
	_this->frag_id			= 0xff;
	_this->next_seq_nb		= 0xff;
	_this->is_fragmented		= C_FALSE;
	_this->frag_counter		= 0;
	_this->qos_tag			= 0xffffffff;
	_this->use_crc			= C_FALSE;
	_this->pdu_length		= 0;
	_this->remaining_pdu_length	= 0;
	_this->rle_length		= 0;
	_this->proto_type		= 0xffff;
	_this->label_type		= 0xff;
	_this->error_nb			= 0;
	_this->error_type		= 0;
	_this->end_addr			= NULL;
}

int rle_ctx_init(struct rle_ctx_management *_this)
{
	if (!_this) {
		printf("ERROR %s:%s:%d: RLE context is NULL\n",
				 __FILE__, __func__, __LINE__);
		return C_ERROR;
	}

	_this->buf = NULL;

	/* set to zero or invalid values
	 * all variables */
	flush(_this);

	/* allocate enough memory space
	 * for the worst case of fragmentation */
	_this->buf = malloc(ZC_BUFFER_MAX_SIZE);
	if (!_this->buf) {
		printf("ERROR %s:%s:%d: allocating ZC buffer failed [%s]\n",
				 __FILE__, __func__, __LINE__,
				strerror(errno));
		return C_ERROR;
	}

	/* set useful data end address pointing
	 * to the buffer start address */
	_this->end_addr = (int *)_this->buf;

	/* set all buffer memory to zero */
	memset(_this->buf, 0, ZC_BUFFER_MAX_SIZE);

	return C_OK;
}

int rle_ctx_destroy(struct rle_ctx_management *_this)
{
	if (!_this) {
		printf("ERROR %s:%s:%d: RLE context is NULL\n",
				 __FILE__, __func__, __LINE__);
		return C_ERROR;
	}

	flush(_this);

	if (_this->buf) {
		free(_this->buf);
		_this->buf = NULL;
	}

	return C_OK;
}

void rle_ctx_set_frag_id(struct rle_ctx_management *_this, uint8_t val)
{
	if (val > RLE_MAX_FRAG_ID) {
		printf("ERROR %s:%s:%d: Invalid fragment id [%d]\n",
				 __FILE__, __func__, __LINE__, val);
		return;
	}

	_this->frag_id = val;
}

uint8_t rle_ctx_get_frag_id(struct rle_ctx_management *_this)
{
	return(_this->frag_id);
}

void rle_ctx_set_seq_nb(struct rle_ctx_management *_this, uint8_t val)
{
	_this->next_seq_nb = val;
}

void rle_ctx_incr_seq_nb(struct rle_ctx_management *_this)
{
	_this->next_seq_nb++;
}

void rle_ctx_set_is_fragmented(struct rle_ctx_management *_this, bool val)
{
	_this->is_fragmented = val;
}

void rle_ctx_set_frag_counter(struct rle_ctx_management *_this, uint8_t val)
{
	_this->frag_counter = val;
}

void rle_ctx_incr_frag_counter(struct rle_ctx_management *_this)
{
	_this->frag_counter++;
}

void rle_ctx_set_qos_tag(struct rle_ctx_management *_this, uint32_t val)
{
	_this->qos_tag = val;
}

void rle_ctx_set_use_crc(struct rle_ctx_management *_this, bool val)
{
	_this->use_crc = val;
}

void rle_ctx_set_pdu_length(struct rle_ctx_management *_this, uint32_t val)
{
	if (val > RLE_MAX_PDU_SIZE) {
		printf("ERROR %s:%s:%d: Invalid PDU length [%d]\n",
				__FILE__, __func__, __LINE__, val);
		return;
	}

	_this->pdu_length = val;
}

uint32_t rle_ctx_get_pdu_length(struct rle_ctx_management *_this)
{
	return(_this->pdu_length);
}

void rle_ctx_set_remaining_pdu_length(struct rle_ctx_management *_this, uint32_t val)
{
	if (val > RLE_MAX_PDU_SIZE) {
		printf("ERROR %s:%s:%d: Invalid remaining RLE length [%d]\n",
				__FILE__, __func__, __LINE__, val);
		return;
	}

	_this->remaining_pdu_length = val;
}

void rle_ctx_set_rle_length(struct rle_ctx_management *_this, uint32_t val)
{
	if (val > RLE_MAX_PDU_SIZE) {
		printf("ERROR %s:%s:%d: Invalid RLE length [%d]\n",
				__FILE__, __func__, __LINE__, val);
		return;
	}

	_this->rle_length = val;
}

void rle_ctx_set_proto_type(struct rle_ctx_management *_this, uint16_t val)
{
	_this->proto_type = val;
}

uint16_t rle_ctx_get_proto_type(struct rle_ctx_management *_this)
{
	return(_this->proto_type);
}


void rle_ctx_set_label_type(struct rle_ctx_management *_this, uint8_t val)
{
	if ((val != RLE_LT_IMPLICIT_PROTO_TYPE) || (val != RLE_LT_PROTO_SIGNAL)) {
		printf("ERROR %s:%s:%d: Invalid Label_type value [%d]\n",
				__FILE__, __func__, __LINE__, val);
		return;
	}

	_this->label_type = val;
}

uint8_t rle_ctx_get_label_type(struct rle_ctx_management *_this)
{
	return(_this->label_type);
}

void rle_ctx_set_end_address(struct rle_ctx_management *_this, int *addr)
{
	if (!addr) {
		printf("ERROR %s:%s:%d: Useful data end address NULL\n",
				__FILE__, __func__, __LINE__);
		return;
	}

	_this->end_address = (int *)addr;
}

int *rle_ctx_get_end_address(struct rle_ctx_management *_this)
{
	return(_this->end_address);
}

