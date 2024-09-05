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

enum FEC_TYPE {
	FEC_TYPE_1_1,
	FEC_TYPE_1_2,
	FEC_TYPE_1_3,
	FEC_TYPE_2_3,
	FEC_TYPE_COUNT,
};


_Static_assert(FEC_TYPE_COUNT <= (1 << FTY_NBITS));

struct fec_counts_t {
	IUINT8 original_count, recovery_count;
};

static const struct fec_counts_t FEC_COUNTS[] = {
	{1, 0},
	{1, 1},
	{1, 2},
	{2, 1},
};

_Static_assert(sizeof(FEC_COUNTS) / sizeof(*FEC_COUNTS) == FEC_TYPE_COUNT);



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
	} else {
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
	kcp->input_cid = kcp->cid = cid & ((1 << CID_NBITS) - 1);
	kcp->user = user;

	kcp->input_fid = kcp->recv_fid = kcp->input_pid = kcp->recv_pid = (IUINT16)-1 & ((1 << PID_NBITS) - 1);

	kcp->segs = ikcp_malloc((1 << PID_NBITS) * sizeof(*kcp->segs));
	if (!kcp->segs) {
		goto error_kcp;
	}
	memset(kcp->segs, 0, (1 << PID_NBITS) * sizeof(*kcp->segs));

	kcp->fecs = ikcp_malloc((1 << FID_NBITS) * sizeof(*kcp->fecs));
	if (!kcp->fecs) {
		goto error_segs;
	}
	memset(kcp->fecs, 0, (1 << FID_NBITS) * sizeof(*kcp->fecs));

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
			for (int i = 0; i < (1 << FID_NBITS); ++i) {
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
			for (int i = 0; i < (1 << PID_NBITS); ++i) {
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

	IUINT16 recv_pid = (kcp->recv_pid + 1) & ((1 << PID_NBITS) - 1);

	if (!kcp->segs[recv_pid].data) {
		return 0;
	}

	memcpy(buffer, kcp->segs[recv_pid].data, kcp_seg_data_len);
	ikcp_remove_original(kcp, kcp->recv_pid);
	kcp->recv_pid = recv_pid;
	__atomic_add_fetch(&kcp_recv_pid_count, 1, __ATOMIC_RELAXED);
	return kcp_seg_data_len;
}


//---------------------------------------------------------------------
// input data
//---------------------------------------------------------------------
// FIXME:
// Currently works on little endian only
static int ikcp_add_original(ikcpcb *kcp, const char *data, IUINT32 size, IUINT16 gid)
{
	IUINT16 hdr = *(IUINT16 *)data;
	IUINT16 pid = hdr & ((1 << PID_NBITS) - 1);
	IUINT16 cid = (hdr >> PID_NBITS) & ((1 << CID_NBITS) - 1);

#if 0
	const int data_counter_loc = sizeof(IUINT16);
	if (data_counter_loc < size) {
		const char data_counter = data[data_counter_loc];
		for (int i = data_counter_loc + 1; i < size; ++i) {
			if (data[i] != data_counter) {
				err_log("[%d] = %d, %d\n", (int)i, (int)data[i], (int)data_counter);
				return -3;
			}
		}
	}
#endif

	kcp->input_cid = cid;
	if (cid != kcp->cid) {
		err_log("kcp->cid %d, cid %d\n", (int)kcp->cid, (int)cid);
		kcp->should_reset = true;
		return -1;
	}

	// err_log("kcp->recv_pid %d, pid %d, kcp->input_pid %d\n", (int)kcp->recv_pid, (int)pid, (int)kcp->input_pid);
	if (((pid - kcp->recv_pid) & ((1 << PID_NBITS) - 1)) <= ((kcp->input_pid - kcp->recv_pid) & ((1 << PID_NBITS) - 1))) {
		if (kcp->segs[pid].data) {
			if (memcmp(kcp->segs[pid].data, data, size) != 0) {
				err_log("mismatch kcp->recv_pid %d, pid %d, kcp->input_pid %d\n", (int)kcp->recv_pid, (int)pid, (int)kcp->input_pid);
				return -2;
			}
		} else {
			kcp->segs[pid].data = ikcp_malloc(size);
			memcpy(kcp->segs[pid].data, data, size);
			__atomic_add_fetch(&kcp_input_pid_count, 1, __ATOMIC_RELAXED);
		}
	} else if (
		((pid - kcp->input_pid) & ((1 << PID_NBITS) - 1)) < (1 << (PID_NBITS - 1))
		// && ((pid - kcp->recv_pid) & ((1 << PID_NBITS) - 1)) < (1 << (PID_NBITS - 1))
	) {
		for (IUINT16 i = pid; i != kcp->input_pid; --i, i &= ((1 << PID_NBITS) - 1)) {
			ikcp_remove_original(kcp, i);
		}
		kcp->segs[pid].data = ikcp_malloc(size);
		memcpy(kcp->segs[pid].data, data, size);
		__atomic_add_fetch(&kcp_input_pid_count, 1, __ATOMIC_RELAXED);
		kcp->input_pid = pid;
	}
	else if (((kcp->recv_pid - pid) & ((1 << PID_NBITS) - 1)) > (1 << (PID_NBITS - 2))) {
		err_log("bad kcp->recv_pid %d, pid %d, kcp->input_pid %d\n", (int)kcp->recv_pid, (int)pid, (int)kcp->input_pid);
		return -3;
	}

	return 0;
}

static void ikcp_remove_fec(ikcpcb *kcp, IUINT16 fid) {
	struct IKCPFEC *fec = &kcp->fecs[fid];
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

static int ikcp_remove_fec_for(ikcpcb *kcp, IUINT16 fid)
{
	if (
		((fid - kcp->recv_fid) & ((1 << FID_NBITS) - 1)) > ((kcp->input_fid - kcp->recv_fid) & ((1 << FID_NBITS) - 1))
	) {
		if (((fid - kcp->input_fid) & ((1 << FID_NBITS) - 1)) >= (1 << (FID_NBITS - 1))) {
			err_log("bad kcp->recv_fid %d, fid %d, kcp->input_fid %d\n", (int)kcp->recv_fid, (int)fid, (int)kcp->input_fid);
			return -1;
		}

		for (IUINT16 i = kcp->input_fid; i != fid;) {
			++i, i &= ((1 << FID_NBITS) - 1);
			ikcp_remove_fec(kcp, i);
		}
		kcp->input_fid = fid;
		if (((kcp->input_fid - kcp->recv_fid) & ((1 << FID_NBITS) - 1)) > (1 << (FID_NBITS - 1))) {
			fid = (kcp->input_fid - (1 << (FID_NBITS - 1))) & ((1 << FID_NBITS) - 1);
			for (IUINT16 i = kcp->recv_fid; i != fid; ++i, i &= ((1 << FID_NBITS) - 1)) {
				ikcp_remove_fec(kcp, i);
			}
			kcp->recv_fid = fid;
		}
	}

	return 0;
}


// FIXME:
// Currently works on little endian only
int ikcp_input(ikcpcb *kcp, const char *data, long size)
{
	if (size < sizeof(IUINT16)) {
		return -10;
	}

	IUINT16 hdr = *(IUINT16 *)data;
	data += sizeof(IUINT16);
	size -= sizeof(IUINT16);

	IUINT16 fid = (hdr >> (GID_NBITS + FTY_NBITS)) & ((1 << FID_NBITS) - 1);
	IUINT16 gid = (hdr >> FTY_NBITS) & ((1 << GID_NBITS) - 1);
	IUINT16 fty = hdr & ((1 << FTY_NBITS) - 1);

	if (size == 0) {
		if (kcp->session_data_received) {
			return 12;
		}
		if (fty == 0 && gid == ((IUINT16)-1 & ((1 << GID_NBITS) - 1)) && (fid & ~((1 << CID_NBITS) - 1)) == 0) {
			IUINT16 cid = fid;

			if (cid != kcp->cid) {
				kcp->input_cid = cid;
				kcp->should_reset = true;
				return -8;
			}

			IUINT16 hdr =
				((0 & ((1 << FID_NBITS) - 1)) << (GID_NBITS + CID_NBITS + 1)) |
				(((IUINT16)-1 & ((1 << GID_NBITS) - 1)) << (CID_NBITS + 1)) |
				((kcp->cid & ((1 << CID_NBITS) - 1)) << 1);

			int ret = kcp->output((char *)&hdr, sizeof(IUINT16), kcp, kcp->user);
			if (ret < 0) {
				return ret * 0x1000 - 9;
			}

			kcp->session_just_established = true;
			return 0;
		} else {
			return -9;
		}
	}

	if (!kcp->session_established) {
		return 11;
	}

	if (size != kcp->mtu - sizeof(IUINT16)) {
		return -1;
	}

	kcp->session_data_received = true;

	kcp->fid = fid;
	kcp->gid = gid;

	int ret = ikcp_remove_fec_for(kcp, fid);
	if (ret < 0) {
		return -7;
	} else if (ret > 0) {
		return 3;
	}

	__atomic_add_fetch(&kcp_input_count, 1, __ATOMIC_RELAXED);

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
		// err_log("fid %d fty %d\n", (int)fid, (int)fty);

		fec->data_ptrs = ikcp_malloc(count * sizeof(*fec->data_ptrs));
		memset(fec->data_ptrs, 0, count * sizeof(*fec->data_ptrs));
		fec->data_ptrs_count = count;
		fec->fty = fty;
		__atomic_add_fetch(&kcp_input_fid_count, 1, __ATOMIC_RELAXED);
	}

	if (fec->data_ptrs[gid]) {
		if (memcmp(fec->data_ptrs[gid], data, size) != 0) {
			err_log("mismatch fid %d fty %d gid %d\n", (int)fid, (int)fty, (int)gid);
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
				if ((ret = ikcp_add_original(kcp, data, size, 0)) != 0) {
					err_log("original fid %d, fty %d\n", (int)fid, (int)fty);
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

		void *recovered_data[counts.recovery_count] = {};
		int recovered_data_count = 0;

		for (int i = 0; i < counts.original_count; ++i) {
			char *data = fec->data_ptrs[i];
			if (data) {
				if ((ret = ikcp_add_original(kcp, data, size, i)) != 0) {
					err_log("decoder original fid %d, fty %d\n", (int)fid, (int)fty);
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
				recovered_data[recovered_data_count] = ikcp_malloc(size);
				if (!recovered_data[recovered_data_count]) {
					ret = -10;
					goto fail_decoder;
				}
				memcpy(recovered_data[recovered_data_count], data, size);
				FecalSymbol recovery;
				recovery.Data = recovered_data[recovered_data_count];
				recovery.Bytes = size;
				recovery.Index = i;
				++recovered_data_count;
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
			for (int i = 0; i < recovered_data_count; ++i) {
				ikcp_free(recovered_data[i]);
			}
			return 0;
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
				if ((ret = ikcp_add_original(kcp, recovered.Symbols[i].Data, recovered.Symbols[i].Bytes, recovered.Symbols[i].Index)) != 0) {
					err_log("decoder gid %d recovered fid %d, fty %d\n", (int)recovered.Symbols[i].Index, (int)fid, (int)fty);
					ret = ret * 0x10 - 1;
					goto fail_decoder;
				}
			}
		}

		fecal_free(decoder);
		for (int i = 0; i < recovered_data_count; ++i) {
			ikcp_free(recovered_data[i]);
		}
		return 0;

fail_decoder:
		fecal_free(decoder);
		for (int i = 0; i < recovered_data_count; ++i) {
			ikcp_free(recovered_data[i]);
		}
		return ret * 0x10 - 6;
	} else {
		return 0;
	}
}


// FIXME:
// Currently works on little endian only
int ikcp_reset(ikcpcb *kcp, IUINT16 cid)
{
	IUINT16 hdr = ((kcp->fid & ((1 << FID_NBITS) - 1)) << (GID_NBITS + CID_NBITS + 1)) | ((kcp->gid & ((1 << GID_NBITS) - 1)) << (CID_NBITS + 1)) | ((cid & ((1 << CID_NBITS) - 1)) << 1) | 1;
	return kcp->output((const char *)&hdr, sizeof(hdr), kcp, kcp->user);
}


// FIXME:
// Currently works on little endian only

#define count_nbits (sizeof(IUINT16) * 8 - PID_NBITS)
int ikcp_reply(ikcpcb *kcp)
{
	char buf[kcp->mtu];
	IUINT16 hdr = ((kcp->fid & ((1 << FID_NBITS) - 1)) << (GID_NBITS + CID_NBITS + 1)) | ((kcp->gid & ((1 << GID_NBITS) - 1)) << (CID_NBITS + 1)) | ((kcp->cid & ((1 << CID_NBITS) - 1)) << 1);
	char *ptr = buf;
	int size = 0;
	*(IUINT16 *)ptr = hdr;
	ptr += sizeof(IUINT16);
	size += sizeof(IUINT16);

	IUINT16 pid = kcp->recv_pid;
	pid &= ((1 << PID_NBITS) - 1);
	while (pid != kcp->input_pid) {
		++pid;
		pid &= ((1 << PID_NBITS) - 1);

		if (!kcp->segs[pid].data) {
			int nack_start = pid;
			int nack_count_0 = 0;

			while (1) {
				++pid;
				pid &= ((1 << PID_NBITS) - 1);

				if (pid == kcp->input_pid) {
					break;
				}

				if (kcp->segs[pid].data) {
					break;
				}

				++nack_count_0;

				if (nack_count_0 == (1 << count_nbits)) {
					// err_log("%d %d\n", nack_start, nack_count_0);
					IUINT16 nack = ((nack_start & ((1 << PID_NBITS) - 1)) << count_nbits) | ((1 << count_nbits) - 1);

					*(IUINT16 *)ptr = nack;
					ptr += sizeof(IUINT16);
					size += sizeof(IUINT16);
					if (size > kcp->mtu) {
						return -1;
					}

					nack_start = pid;
					nack_count_0 = 0;
				}
			}

			// err_log("%d %d\n", nack_start, nack_count_0);
			IUINT16 nack = ((nack_start & ((1 << PID_NBITS) - 1)) << count_nbits) | (nack_count_0 & ((1 << count_nbits) - 1));

			*(IUINT16 *)ptr = nack;
			ptr += sizeof(IUINT16);
			size += sizeof(IUINT16);
			if (size > kcp->mtu) {
				return -1;
			}
		}
	}

	++pid;
	pid &= ((1 << PID_NBITS) - 1);
	// err_log("%d %d\n", pid, (1 << (PID_NBITS - 2)) - 1);
	*(IUINT16 *)ptr = ((pid & ((1 << PID_NBITS) - 1)) << count_nbits);
	ptr += sizeof(IUINT16);
	size += sizeof(IUINT16);
	if (size > kcp->mtu) {
		return -1;
	}

	return kcp->output(buf, size, kcp, kcp->user);
}


int ikcp_setmtu(ikcpcb *kcp, int mtu)
{
	kcp->mtu = mtu;
	return 0;
}

IUINT16 kcp_input_fid_count, kcp_recv_pid_count, kcp_input_pid_count, kcp_input_count;
