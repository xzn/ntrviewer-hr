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
#ifdef __APPLE__
#define PTHREAD_SEM_NON_MONOTONIC_CLOCK
#endif
#ifdef PTHREAD_SEM_NON_MONOTONIC_CLOCK
typedef struct rp_sem_t {
	pthread_mutex_t mutex;
	pthread_cond_t  cond;
	unsigned n, m;
} rp_sem_t;
#else
typedef sem_t rp_sem_t;
#endif
typedef void *rp_e_t;
#endif

#ifdef _WIN32

extern bool rp_lock_srw;

#define rp_lock_init(n) ({ \
	if (rp_lock_srw) { \
		InitializeSRWLock(&(s).srw); \
	} else { \
		InitializeCriticalSection(&(s).cs); \
	} \
	0; \
})
#define rp_lock_wait(n) ({ \
	if (rp_lock_srw) { \
		AcquireSRWLockExclusive(&(s).srw); \
	} else { \
		EnterCriticalSection(&(s).cs); \
	} \
	0; \
})
#define rp_lock_rel(n) ({ \
	if (rp_lock_srw) { \
		ReleaseSRWLockExclusive(&(s).srw); \
	} else { \
		LeaveCriticalSection(&(s).cs); \
	} \
	0; \
})
#define rp_lock_close(n) ({ \
	if (rp_lock_srw) { \
		rp_lock_init(n); \
	} else { \
		DeleteCriticalSection(&(s).cs); \
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
	DWORD _to_ms = (to_ns) / 1000000; \
	HANDLE _h[2] = {n, e}; \
	DWORD _res = e ? WaitForMultipleObjects(2, _h, FALSE, _to_ms) : WaitForSingleObject(n, _to_ms); \
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
	unsigned _to_ms = (to_ns) / 1000000; \
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

extern pthread_condattr_t rp_cond_attr;

#define rp_lock_init(n) pthread_mutex_init(&(n), 0)
#define rp_lock_wait(n) pthread_mutex_lock(&(n))
#define rp_lock_rel(n) pthread_mutex_unlock(&(n))
#define rp_lock_close(n) pthread_mutex_destroy(&(n))

#ifdef PTHREAD_SEM_NON_MONOTONIC_CLOCK
#define rp_sem_create(s, _n, _m) ({ \
	int _ret; \
	_ret = pthread_mutex_init(&(s).mutex, NULL); \
	if (_ret == 0) { \
		_ret = pthread_cond_init(&(s).cond, &rp_cond_attr); \
	} \
	if (_ret == 0) { \
		(s).n = _n; \
		(s).m = _m; \
	} \
	_ret; \
})
#define rp_sem_timedwait(s, to_ns, e) ({ \
	int _ret = pthread_mutex_lock(&(s).mutex); \
	int _cret = 0; \
	if (_ret == 0) { \
		if ((s).n == 0) { \
			struct timespec _to = clock_abs_ns_from_now(to_ns); \
			_cret = pthread_cond_timedwait(&(s).cond, &(s).mutex, &_to); \
			if (_cret == 0) { \
				if ((s).n > 0) { \
					--(s).n; \
				} else { \
					_cret = -1; \
				} \
			} \
		} else { \
			--(s).n; \
		} \
		_ret = pthread_mutex_unlock(&(s).mutex); \
	} \
	if (_ret == 0) { \
		_ret = _cret; \
	} \
	_ret; \
})
#define rp_sem_rel(s) ({ \
	int _ret = pthread_mutex_lock(&(s).mutex); \
	int _cret = 0; \
	if (_ret == 0) \
	{ \
		if ((s).n < (s).m) { \
			++(s).n; \
			_cret = pthread_cond_signal(&(s).cond); \
		} else { \
			_cret = -1; \
		} \
		_ret = pthread_mutex_unlock(&(s).mutex); \
		if (_ret == 0) { \
			_ret = _cret; \
		} \
	} \
	_ret; \
})
#define rp_sem_close(s) ({ \
	pthread_mutex_destroy(&(s).mutex); \
    pthread_cond_destroy(&(s).cond); \
})
#else
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
#endif

#define rp_cond_init(c) pthread_cond_init(&(c), &rp_cond_attr)
#define rp_cond_timedwait(c, m, to_ns) ({ \
	struct timespec _to = clock_monotonic_abs_ns_from_now(to_ns); \
	pthread_cond_timedwait(&(c), &(m), &_to); \
})
#define rp_cond_rel(c) pthread_cond_signal(&(c));
#define rp_cond_close(c) pthread_cond_destroy(&(c))

#ifdef PTHREAD_SEM_NON_MONOTONIC_CLOCK
static struct timespec clock_abs_ns_from_now(long ns) {
	struct timespec to;
	if (clock_gettime(CLOCK_REALTIME, &to) != 0) {
		return (struct timespec){ 0, 0 };
	}
	to.tv_nsec += ns;
	to.tv_sec += to.tv_nsec / 1000000000;
	to.tv_nsec %= 1000000000;

	return to;
}
#else
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

#endif

void rp_syn_startup(void);

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
