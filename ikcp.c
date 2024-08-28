//=====================================================================
//
// KCP - A Better ARQ Protocol Implementation
// skywind3000 (at) gmail.com, 2010-2011
//
// Features:
// + Average RTT reduce 30% - 40% vs traditional ARQ like tcp.
// + Maximum RTT reduce three times vs tcp.
// + Lightweight, distributed as a single source file.
//
//=====================================================================
#include "ikcp.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#define err_log(f, ...) fprintf(stderr, "%s:%d:%s " f, __FILE__, __LINE__, __func__, ## __VA_ARGS__)
// #define err_log(f, ...) ((void)0)

//=====================================================================
// KCP BASIC
//=====================================================================


//---------------------------------------------------------------------
// encode / decode
//---------------------------------------------------------------------

/* encode 8 bits unsigned int */
static inline char *ikcp_encode8u(char *p, unsigned char c)
{
	*(unsigned char*)p++ = c;
	return p;
}

/* decode 8 bits unsigned int */
static inline const char *ikcp_decode8u(const char *p, unsigned char *c)
{
	*c = *(unsigned char*)p++;
	return p;
}

/* encode 16 bits unsigned int (lsb) */
static inline char *ikcp_encode16u(char *p, unsigned short w)
{
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
	*(unsigned char*)(p + 0) = (w & 255);
	*(unsigned char*)(p + 1) = (w >> 8);
#else
	memcpy(p, &w, 2);
#endif
	p += 2;
	return p;
}

/* decode 16 bits unsigned int (lsb) */
static inline const char *ikcp_decode16u(const char *p, unsigned short *w)
{
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
	*w = *(const unsigned char*)(p + 1);
	*w = *(const unsigned char*)(p + 0) + (*w << 8);
#else
	memcpy(w, p, 2);
#endif
	p += 2;
	return p;
}

/* encode 32 bits unsigned int (lsb) */
static inline char *ikcp_encode32u(char *p, IUINT32 l)
{
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
	*(unsigned char*)(p + 0) = (unsigned char)((l >>  0) & 0xff);
	*(unsigned char*)(p + 1) = (unsigned char)((l >>  8) & 0xff);
	*(unsigned char*)(p + 2) = (unsigned char)((l >> 16) & 0xff);
	*(unsigned char*)(p + 3) = (unsigned char)((l >> 24) & 0xff);
#else
	memcpy(p, &l, 4);
#endif
	p += 4;
	return p;
}

/* decode 32 bits unsigned int (lsb) */
static inline const char *ikcp_decode32u(const char *p, IUINT32 *l)
{
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
	*l = *(const unsigned char*)(p + 3);
	*l = *(const unsigned char*)(p + 2) + (*l << 8);
	*l = *(const unsigned char*)(p + 1) + (*l << 8);
	*l = *(const unsigned char*)(p + 0) + (*l << 8);
#else
	memcpy(l, p, 4);
#endif
	p += 4;
	return p;
}

static inline IUINT32 _imin_(IUINT32 a, IUINT32 b) {
	return a <= b ? a : b;
}

static inline IUINT32 _imax_(IUINT32 a, IUINT32 b) {
	return a >= b ? a : b;
}

static inline IUINT32 _ibound_(IUINT32 lower, IUINT32 middle, IUINT32 upper)
{
	return _imin_(_imax_(lower, middle), upper);
}

static inline long _itimediff(IUINT32 later, IUINT32 earlier)
{
	return ((IINT32)(later - earlier));
}

//---------------------------------------------------------------------
// manage segment
//---------------------------------------------------------------------
enum FEC_TYPE {
	FEC_TYPE_1_1,
	FEC_TYPE_4_5,
	FEC_TYPE_3_4,
	FEC_TYPE_2_3,
	FEC_TYPE_3_5,
	FEC_TYPE_2_4,
	FEC_TYPE_2_5,
	FEC_TYPE_1_3,
	FEC_TYPE_MAX = FEC_TYPE_1_3,
	// coded as FEC_TYPE_1_1 with top bit in group id set to differentiate
	FEC_TYPE_1_2,
	FEC_TYPE_COUNT,
};

#define FEC_TYPE_BITS_COUNT (3)

_Static_assert(FEC_TYPE_MAX < (1 << FEC_TYPE_BITS_COUNT));

struct fec_counts_t {
	IUINT8 original_count, recovery_count;
};

static const struct fec_counts_t FEC_COUNTS[] = {
	{1, 0},
	{4, 1},
	{3, 1},
	{2, 1},
	{3, 2},
	{2, 2},
	{2, 3},
	{1, 2},
	{1, 1},
};

_Static_assert(sizeof(FEC_COUNTS) / sizeof(*FEC_COUNTS) == FEC_TYPE_COUNT);


static void* (*ikcp_malloc_hook)(size_t) = NULL;
static void (*ikcp_free_hook)(void *) = NULL;

// internal malloc
static void* ikcp_malloc(size_t size) {
	if (ikcp_malloc_hook)
		return ikcp_malloc_hook(size);
	return malloc(size);
}

// internal free
static void ikcp_free(void *ptr) {
	if (ikcp_free_hook) {
		ikcp_free_hook(ptr);
	}	else {
		free(ptr);
	}
}

// redefine allocator
void ikcp_allocator(void* (*new_malloc)(size_t), void (*new_free)(void*))
{
	ikcp_malloc_hook = new_malloc;
	ikcp_free_hook = new_free;
}


//---------------------------------------------------------------------
// create a new kcpcb
//---------------------------------------------------------------------
#include "fecal/fecal.h"

ikcpcb* ikcp_create(IUINT16 cid, void *user)
{
	if (fecal_init() != 0) {
		return 0;
	}

	ikcpcb *kcp = (ikcpcb*)ikcp_malloc(sizeof(struct IKCPCB));
	if (kcp == NULL) return NULL;
	*kcp = (ikcpcb){ 0 };
	kcp->cid = cid;
	kcp->user = user;

	kcp->segs = ikcp_malloc((1 << 10) * sizeof(*kcp->segs));
	if (!kcp->segs) {
		goto error_kcp;
	}
	memset(kcp->segs, 0, (1 << 10) * sizeof(*kcp->segs));

	kcp->fecs = ikcp_malloc((1 << 10) * sizeof(*kcp->fecs));
	if (!kcp->fecs) {
		goto error_segs;
	}
	memset(kcp->fecs, 0, (1 << 10) * sizeof(*kcp->fecs));

	return kcp;

error_segs:
	ikcp_free(kcp->segs);
error_kcp:
	ikcp_free(kcp);
	return 0;
}


//---------------------------------------------------------------------
// release a new kcpcb
//---------------------------------------------------------------------
void ikcp_release(ikcpcb *kcp)
{
	if (kcp) {
		if (kcp->fecs) {
			for (int i = 0; i < (1 << 10); ++i) {
				if (kcp->fecs[i].data_ptrs) {
					for (int j = 0; j < kcp->fecs[i].data_ptrs_count; ++j) {
						if (kcp->fecs[i].data_ptrs[j]) {
							ikcp_free(kcp->fecs[i].data_ptrs[j]);
						}
					}
					ikcp_free(kcp->fecs[i].data_ptrs);
				}
			}
			ikcp_free(kcp->fecs);
		}
		if (kcp->segs) {
			for (int i = 0; i < (1 << 10); ++i) {
				if (kcp->segs[i].data) {
					ikcp_free(kcp->segs[i].data);
				}
			}
			ikcp_free(kcp->segs);
		}
		ikcp_free(kcp);
	}
}


//---------------------------------------------------------------------
// set output callback, which will be invoked by kcp
//---------------------------------------------------------------------
void ikcp_setoutput(ikcpcb *kcp, int (*output)(const char *buf, int len,
	ikcpcb *kcp, void *user))
{
	kcp->output = output;
}


//---------------------------------------------------------------------
// user/upper level recv: returns size, returns below zero for EAGAIN
//---------------------------------------------------------------------
static int ikcp_remove_original(ikcpcb *kcp, IUINT16 pid)
{
	if (kcp->segs[pid].data) {
		ikcp_free(kcp->segs[pid].data);
		kcp->segs[pid].data = 0;
	}
	return 0;
}


int ikcp_recv(ikcpcb *kcp, char *buffer, int len)
{
	const int kcp_seg_data_len = kcp->mtu - sizeof(IUINT16);
	if (len < kcp_seg_data_len) {
		return -1;
	}

	if (!kcp->segs[kcp->pid].data) {
		return 0;
	}

	memcpy(buffer, kcp->segs[kcp->pid].data, kcp_seg_data_len);
	ikcp_remove_original(kcp, kcp->pid);
	++kcp->pid;
	kcp->pid &= ((1 << 10) - 1);
	return kcp_seg_data_len;
}


//---------------------------------------------------------------------
// input data
//---------------------------------------------------------------------
// FIXME:
// Currently works on little endian only
static int ikcp_add_original(ikcpcb *kcp, const char *data, IUINT32 size)
{
	IUINT16 hdr = *(IUINT16 *)data;
	IUINT16 pid = hdr & ((1 << 10) - 1);
	IUINT16 cid = (hdr >> 10) & ((1 << 2) - 1);

	if (cid != kcp->cid) {
		kcp->received_cid = cid;
		kcp->should_reset = true;
		return -1;
	}

	if (((pid - kcp->pid) & ((1 << 10) - 1)) < (1 << 9)) {
		if (kcp->segs[pid].data) {
			if (memcmp(kcp->segs[pid].data, data, size) != 0) {
				return -2;
			}
		} else {
			kcp->segs[pid].data = ikcp_malloc(size);
			memcpy(kcp->segs[pid].data, data, size);

			if (((pid - kcp->pid) & ((1 << 10) - 1)) - ((kcp->received_pid - kcp->pid) & ((1 << 10) - 1)) < (1 << 9)) {
				kcp->received_pid = pid;
			}
		}
	}

	return 0;
}


static int ikcp_remove_fec(ikcpcb *kcp, IUINT16 fid)
{
	if (((fid - kcp->received_fid) & ((1 << 10) - 1)) >= (1 << 9)) {
		return 0;
	}

	for (int j = kcp->received_fid; j != fid; ++j, j &= ((1 << 10) - 1)) {
		struct IKCPFEC *fec = &kcp->fecs[j];
		if (fec->data_ptrs) {
			for (int i = 0; i < fec->data_ptrs_count; ++i) {
				if (fec->data_ptrs[i]) {
					ikcp_free(fec->data_ptrs[i]);
				}
			}
			ikcp_free(fec->data_ptrs);
			fec->data_ptrs = 0;
		}
	}
	kcp->received_fid = fid;

	return 0;
}


// FIXME:
// Currently works on little endian only
int ikcp_input(ikcpcb *kcp, const char *data, long size)
{
	if (size != kcp->mtu) {
		return -1;
	}

	IUINT16 hdr = *(IUINT16 *)data;
	data += sizeof(IUINT16);
	size += sizeof(IUINT16);

	IUINT16 fid = (hdr >> 6) & ((1 << 10) - 1);
	IUINT16 gid = (hdr >> 3) & ((1 << 3) - 1);
	IUINT16 fty = hdr & ((1 << 3) - 1);
	if (fty == 0 && (gid & (1 << 2))) {
		fty = FEC_TYPE_1_2;
		gid &= ((1 << 2) - 1);
	}

	if (((fid - kcp->fid) & ((1 << 10) - 1)) >= (1 << 9)) {
		return -9;
	}

	kcp->fid = fid;
	kcp->gid = gid;

	if (ikcp_remove_fec(kcp, (IUINT16)(fid - (1 << 9)) & ((1 << 10) - 1)) != 0) {
		return -7;
	}

	struct IKCPFEC *fec = &kcp->fecs[fid];

	struct fec_counts_t counts = FEC_COUNTS[fty];
	IUINT32 count = counts.original_count + counts.recovery_count;
	if (gid >= count) {
		return -3;
	}

	if (fec->data_ptrs) {
		if (fec->fty != fty) {
			return -2;
		}
	} else {
		fec->data_ptrs = ikcp_malloc(count * sizeof(*fec->data_ptrs));
		memset(fec->data_ptrs, 0, count * sizeof(*fec->data_ptrs));
		fec->data_ptrs_count = count;
		fec->fty = fty;
	}

	if (fec->data_ptrs[gid]) {
		if (memcmp(fec->data_ptrs[gid], data, size) != 0) {
			return -4;
		}
	} else {
		fec->data_ptrs[gid] = ikcp_malloc(size);
		memcpy(fec->data_ptrs[gid], data, size);
	}

	int has_count = 0;
	for (int i = 0; i < count; ++i) {
		if (fec->data_ptrs[i]) {
			++has_count;
		}
	}
	if (counts.original_count == 1 && has_count >= 1) {
		for (int i = 0; i < count; ++i) {
			char *data = fec->data_ptrs[i];
			if (data) {
				int ret;
				if ((ret = ikcp_add_original(kcp, data, size)) != 0) {
					return ret * 0x10 - 8;
				}
			}
		}
		return 0;
	} else if (has_count >= counts.original_count) {
		int ret = 0;
		FecalDecoder decoder = fecal_decoder_create(counts.original_count, counts.original_count * size);
		if (!decoder) {
			return -5;
		}

		for (int i = 0; i < counts.original_count; ++i) {
			char *data = fec->data_ptrs[i];
			if (data) {
				if ((ret = ikcp_add_original(kcp, data, size)) != 0) {
					ret = ret * 0x10 - 5;
					goto fail_decoder;
				}

				FecalSymbol original;
				original.Data = data;
				original.Bytes = size;
				original.Index = i;
				ret = fecal_decoder_add_original(decoder, &original);
				if (ret) {
					ret = ret * 0x10 - 4;
					goto fail_decoder;
				}
			}
		}

		for (int i = 0; i < counts.recovery_count; ++i) {
			char *data = fec->data_ptrs[counts.original_count + i];
			if (data) {
				FecalSymbol recovery;
				recovery.Data = data;
				recovery.Bytes = size;
				recovery.Index = i;
				ret = fecal_decoder_add_recovery(decoder, &recovery);
				if (ret) {
					ret = ret * 0x10 - 3;
					goto fail_decoder;
				}
			}
		}

		RecoveredSymbols recovered;
		ret = fecal_decode(decoder, &recovered);
		if (ret == Fecal_NeedMoreData) {
			fecal_free(decoder);
			return 1;
		} else if (ret) {
			ret = ret * 0x10 - 2;
			goto fail_decoder;
		}

		for (int i = 0; i < recovered.Count; ++i) {
			if (recovered.Symbols[i].Index < counts.original_count) {
				if (recovered.Symbols[i].Bytes != size) {
					ret = -6;
					goto fail_decoder;
				}
				if ((ret = ikcp_add_original(kcp, recovered.Symbols[i].Data, recovered.Symbols[i].Bytes)) != 0) {
					ret = ret * 0x10 - 1;
					goto fail_decoder;
				}
			}
		}
		fecal_free(decoder);
		return 0;

fail_decoder:
		fecal_free(decoder);
		return ret * 0x10 - 6;
	} else {
		return 2;
	}
}


// FIXME:
// Currently works on little endian only
int ikcp_reset(ikcpcb *kcp)
{
	IUINT16 hdr = ((kcp->fid & ((1 << 10) - 1)) << 6) | ((kcp->gid & ((1 << 3) - 1)) << 3) | ((kcp->cid & ((1 << 2) - 1)) << 1) | 1;
	return kcp->output((const char *)&hdr, sizeof(hdr), kcp, kcp->user);
}


// FIXME:
// Currently works on little endian only
int ikcp_reply(ikcpcb *kcp)
{
	char buf[kcp->mtu];
	IUINT16 hdr = ((kcp->fid & ((1 << 10) - 1)) << 6) | ((kcp->gid & ((1 << 3) - 1)) << 3) | ((kcp->cid & ((1 << 2) - 1)) << 1);
	char *ptr = buf;
	int size = 0;
	*(IUINT16 *)ptr = hdr;
	ptr += sizeof(IUINT16);
	size += sizeof(IUINT16);

	IUINT16 pid = kcp->pid - 1;
	pid &= ((1 << 10) - 1);
	while (pid != kcp->received_pid) {
		++pid;
		pid &= ((1 << 10) - 1);

		if (!kcp->segs[pid].data) {
			int nack_start = pid;
			int nack_count_0 = 0;

			while (1) {
				++pid;
				pid &= ((1 << 10) - 1);

				if (pid == kcp->received_pid) {
					break;
				}

				if (kcp->segs[pid].data) {
					break;
				}

				++nack_count_0;

				if (nack_count_0 == (1 << 6)) {
					IUINT16 nack = ((nack_start & ((1 << 10) - 1)) << 6) | ((1 << 6) - 1);

					*(IUINT16 *)ptr = nack;
					ptr += sizeof(IUINT16);
					size += sizeof(IUINT16);

					nack_start = pid;
					nack_count_0 = 0;
				}
			}

			IUINT16 nack = ((nack_start & ((1 << 10) - 1)) << 6) | (nack_count_0 & ((1 << 6) - 1));

			*(IUINT16 *)ptr = nack;
			ptr += sizeof(IUINT16);
			size += sizeof(IUINT16);
		}
	}

	return kcp->output(buf, size, kcp, kcp->user);
}


int ikcp_setmtu(ikcpcb *kcp, int mtu)
{
	kcp->mtu = mtu;
	return 0;
}

