#ifndef RP_SYN_H
#define RP_SYN_H

#ifdef _WIN32
#include <windows.h>
#else
#include <semaphore.h>
#endif

#include <pthread.h>

#include <stdbool.h>

#define NWM_THREAD_WAIT_NS (100000000)

#ifdef _WIN32
typedef union {
	CRITICAL_SECTION cs;
	SRWLOCK srw;
} rp_lock_t;
typedef CONDITION_VARIABLE rp_cond_t;
typedef HANDLE rp_sem_t;
typedef HANDLE rp_e_t;
#else
typedef pthread_mutex_t rp_lock_t;
typedef pthread_cond_t rp_cond_t;
typedef sem_t rp_sem_t;
typedef void *rp_e_t;
#endif

#ifdef _WIN32
extern bool rp_lock_srw;

#define rp_lock_init(n) ({ \
	if (rp_lock_srw) { \
		InitializeSRWLock(&(n).srw); \
	} else { \
		InitializeCriticalSection(&(n).cs); \
	} \
	0; \
})
#define rp_lock_wait(n) ({ \
	if (rp_lock_srw) { \
		AcquireSRWLockExclusive(&(n).srw); \
	} else { \
		EnterCriticalSection(&(n).cs); \
	} \
	0; \
})
#define rp_lock_rel(n) ({ \
	if (rp_lock_srw) { \
		ReleaseSRWLockExclusive(&(n).srw); \
	} else { \
		LeaveCriticalSection(&(n).cs); \
	} \
	0; \
})
#define rp_lock_close(n) ({ \
	if (rp_lock_srw) { \
		rp_lock_init(n); \
	} else { \
		DeleteCriticalSection(&(n).cs); \
	} \
	0; \
})

#define rp_sem_create(n, i, m) ({ \
	HANDLE _res = CreateSemaphoreA(NULL, i, m, NULL); \
	(n) = _res; \
	_res ? 0 : -1; \
})
#define rp_sem_timedwait(n, to_ns, e) ({ \
	int _ret; \
	DWORD to_ms = to_ns / 1000000; \
	HANDLE _h[2] = {n, e}; \
	DWORD _res = e ? WaitForMultipleObjects(2, _h, FALSE, to_ms) : WaitForSingleObject(n, to_ms); \
	if (_res == WAIT_TIMEOUT) { \
		_ret = ETIMEDOUT; \
	} else if (_res == WAIT_OBJECT_0) { \
		_ret = 0; \
	} else if (_res == WAIT_FAILED) { \
		_ret = GetLastError(); \
	} else { \
		ExitThread(0); \
	} \
	_ret; \
})
#define rp_sem_rel(n) (ReleaseSemaphore(n, 1, NULL) ? 0 : GetLastError())
#define rp_sem_close(n) (CloseHandle(n) ? 0 : -1)

#define rp_cond_init(c) InitializeConditionVariable(&c)
#define rp_cond_timedwait(c, m, to_ns) ({ \
	unsigned _to_ms = to_ns / 1000000; \
	BOOL _res; \
	if (rp_lock_srw) { \
		_res = SleepConditionVariableSRW(&(c), &(m).srw, _to_ms, 0); \
	} else { \
		_res = SleepConditionVariableCS(&(c), &(m).cs, _to_ms); \
	} \
	int _ret; \
	if (_res) { \
		_ret = 0; \
	} else { \
		_ret = GetLastError(); \
		if (_ret == ERROR_TIMEOUT) { \
			_ret = ETIMEDOUT; \
		} \
	} \
	_ret; \
})
#define rp_cond_rel(c) WakeConditionVariable(&c)
#define rp_cond_close(c) rp_cond_init(c)

#else

#define rp_lock_init(n) pthread_mutex_init(&(n), 0)
#define rp_lock_wait(n) pthread_mutex_lock(&(n))
#define rp_lock_rel(n) pthread_mutex_unlock(&(n))
#define rp_lock_close(n) pthread_mutex_destroy(&(n))

#define rp_sem_create(n, i, m) rp_sem_init(n, i)
#define rp_sem_init(n, i) sem_init(&(n), 0, i)
#define rp_sem_timedwait(n, to_ns, e) ({ \
	struct timespec _to = clock_monotonic_abs_ns_from_now(to_ns); \
	int _ret = sem_clockwait(&(n), CLOCK_MONOTONIC, &_to); \
	if (_ret) { _ret = errno; } \
	_ret; \
})
#define rp_sem_rel(n) sem_post(&(n))
#define rp_sem_close(n) sem_destroy(&(n))

#define rp_cond_init(c) pthread_cond_init(&(c), &rp_cond_attr)
#define rp_cond_timedwait(c, m, to_ns) ({ \
	struct timespec _to = clock_monotonic_abs_ns_from_now(to_ns); \
	pthread_cond_clockwait(&(c), &(m), CLOCK_MONOTONIC, &_to); \
})
#define rp_cond_rel(c) pthread_cond_signal(&(c));
#define rp_cond_close(c) pthread_cond_destroy(&(c))

static struct timespec clock_monotonic_abs_ns_from_now(long ns) {
  struct timespec to;
  if (clock_gettime(CLOCK_MONOTONIC, &to) != 0) {
    return (struct timespec){ 0, 0 };
  }
  to.tv_nsec += ns;
  to.tv_sec += to.tv_nsec / 1000000000;
  to.tv_nsec %= 1000000000;

  return to;
}

#endif

struct rp_syn_comp_func_t {
	rp_sem_t sem;
	rp_lock_t mutex;
	unsigned pos_head, pos_tail;
	unsigned count;
	void **pos;
};

int rp_syn_init1(struct rp_syn_comp_func_t *syn1, int init, void *base, unsigned stride, int count, void **pos);
int rp_syn_close1(struct rp_syn_comp_func_t *syn1);
int rp_syn_acq(struct rp_syn_comp_func_t *syn1, unsigned timeout_ns, void **pos, rp_e_t e);
int rp_syn_rel(struct rp_syn_comp_func_t *syn1, void *pos);
int rp_syn_acq1(struct rp_syn_comp_func_t *syn1, unsigned timeout_ns, void **pos, rp_e_t e);
int rp_syn_rel1(struct rp_syn_comp_func_t *syn1, void *pos);

#endif
