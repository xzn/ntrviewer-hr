#include <errno.h>
#include <stdio.h>
#include <time.h>
#include "rp_syn.h"

#ifdef _WIN32
bool rp_lock_srw;
#else
pthread_condattr_t rp_cond_attr;

void rp_syn_startup(void) {
	pthread_condattr_init(&rp_cond_attr);
	pthread_condattr_setclock(&rp_cond_attr, CLOCK_MONOTONIC);
}

#endif

unsigned rp_atomic_fetch_addb_wrap(unsigned *p, unsigned a, unsigned factor) {
	unsigned v, v_new;
	do {
		v = __atomic_load_n(p, __ATOMIC_ACQUIRE);
		v_new = (v + a) % factor;
	} while (!__atomic_compare_exchange_n(p, &v, v_new, 1, __ATOMIC_RELEASE, __ATOMIC_RELAXED));
	return v;
}

int rp_syn_init1(struct rp_syn_comp_func_t *syn1, int init, void *base, unsigned stride, int count, void **pos) {
	int res;
	if ((res = rp_sem_create(syn1->sem, init ? count : 0, count)))
		return res;
	if ((res = rp_lock_init(syn1->mutex)))
		return res;

	syn1->pos_head = syn1->pos_tail = 0;
	syn1->count = count;
	syn1->pos = pos;

	for (int i = 0; i < count; ++i) {
		syn1->pos[i] = init ? ((char *)base) + i * stride : 0;
	}
	return 0;
}

int rp_syn_close1(struct rp_syn_comp_func_t *syn1) {
	int res;
	int ret = 0;
	if ((res = rp_sem_close(syn1->sem)))
		ret = res;
	if ((res = rp_lock_close(syn1->mutex)))
		ret = res;
	return ret;
}

int rp_syn_acq(struct rp_syn_comp_func_t *syn1, unsigned timeout_ns, void **pos, rp_e_t e) {
	int res;
	if ((res = rp_sem_timedwait(syn1->sem, NWM_THREAD_WAIT_NS, e)) != 0) {
		if (res != ETIMEDOUT)
			fprintf(stderr, "rp_syn_acq wait sem error\n");
		return res;
	}
	unsigned pos_tail = syn1->pos_tail;
	*pos = syn1->pos[pos_tail];
	syn1->pos[pos_tail] = 0;
	syn1->pos_tail = (pos_tail + 1) % syn1->count;
	// fprintf(stderr, "acq pos %d\n", pos_tail);
	if (!*pos) {
		fprintf(stderr, "error rp_syn_acq at pos %d\n", pos_tail);
		return -1;
	}
	return 0;
}

int rp_syn_rel(struct rp_syn_comp_func_t *syn1, void *pos) {
	if (!pos) {
		fprintf(stderr, "null data rp_syn_rel at pos %d\n", syn1->pos_head);
		return -1;
	}
	unsigned pos_head = syn1->pos_head;
	syn1->pos[pos_head] = pos;
	syn1->pos_head = (pos_head + 1) % syn1->count;
	// fprintf(stderr, "rel pos %d\n", pos_head);
	int res;
	if ((res = rp_sem_rel(syn1->sem))) {
		fprintf(stderr, "rp_syn_rel rel sem error");
	}
	return res;
}

int rp_syn_acq1(struct rp_syn_comp_func_t *syn1, unsigned timeout_ns, void **pos, rp_e_t e) {
	int res;
	if ((res = rp_sem_timedwait(syn1->sem, NWM_THREAD_WAIT_NS, e)) != 0) {
		if (res != ETIMEDOUT)
			fprintf(stderr, "rp_syn_acq1 wait sem error\n");
		return res;
	}
	unsigned pos_tail = rp_atomic_fetch_addb_wrap(&syn1->pos_tail, 1, syn1->count);
	*pos = syn1->pos[pos_tail];
	syn1->pos[pos_tail] = 0;
	if (!*pos) {
		fprintf(stderr, "error rp_syn_acq1 at pos %d\n", pos_tail);
		return -1;
	}
	return 0;
}

int rp_syn_rel1(struct rp_syn_comp_func_t *syn1, void *pos) {
	if (!pos) {
		fprintf(stderr, "null data rp_syn_rel1 at pos %d\n", syn1->pos_head);
		return -1;
	}
	int res;
	if ((res = rp_lock_wait(syn1->mutex))) {
		fprintf(stderr, "rp_syn_rel1 wait mutex error\n");
		return res;
	}

	unsigned pos_head = syn1->pos_head;
	syn1->pos[pos_head] = pos;
	syn1->pos_head = (pos_head + 1) % syn1->count;
	if ((res = rp_lock_rel(syn1->mutex))) {
		fprintf(stderr, "rp_syn_rel1 rel mutex error");
		return res;
	}
	if ((res = rp_sem_rel(syn1->sem))) {
		fprintf(stderr, "rp_syn_rel1 rel sem error");
		return res;
	}
	return 0;
}
