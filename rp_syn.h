#ifndef RP_SYN_H
#define RP_SYN_H

#include <semaphore.h>
#include <pthread.h>

#define NWM_THREAD_WAIT_NS (100000000)

typedef pthread_mutex_t rp_lock_t;
typedef sem_t rp_sem_t;

#define rp_lock_init(n) pthread_mutex_init(&n, 0)
#define rp_lock_wait_try(n) pthread_mutex_trylock(&n)
#define rp_lock_wait(n, to) pthread_mutex_timedlock(&n, to)
#define rp_lock_rel(n) pthread_mutex_unlock(&n)
#define rp_lock_close(n) pthread_mutex_destroy(&n)

#define rp_sem_init(n, i) sem_init(&n, 0, i)
#define rp_sem_wait_try(n) ({ int _ret = sem_trywait(&n); if (_ret) { _ret = errno; if (_ret == EAGAIN) { _ret = ETIMEDOUT; }} _ret;})
#define rp_sem_wait(n, to) ({ int _ret = sem_timedwait(&n, to); if (_ret) { _ret = errno; } _ret;})
#define rp_sem_rel(n) sem_post(&n)
#define rp_sem_close(n) sem_destroy(&n)

struct rp_syn_comp_func_t {
	rp_sem_t sem;
	rp_lock_t mutex;
	unsigned pos_head, pos_tail;
	unsigned count;
	void **pos;
};

int rp_syn_init1(struct rp_syn_comp_func_t *syn1, int init, void *base, unsigned stride, int count, void **pos);
int rp_syn_close1(struct rp_syn_comp_func_t *syn1);
int rp_syn_acq(struct rp_syn_comp_func_t *syn1, unsigned timeout_ns, void **pos);
int rp_syn_rel(struct rp_syn_comp_func_t *syn1, void *pos);
int rp_syn_acq1(struct rp_syn_comp_func_t *syn1, unsigned timeout_ns, void **pos);
int rp_syn_rel1(struct rp_syn_comp_func_t *syn1, void *pos);

#endif
