#pragma once

#include <arch/syscall.h>
#include <object.h>
#include <thread.h>

#include <twz/_sys.h>

long syscall_thread_spawn(uint64_t tidlo,
  uint64_t tidhi,
  struct sys_thrd_spawn_args *tsa,
  int flags);

long syscall_become(uint64_t sclo, uint64_t schi, struct arch_syscall_become_args *ba);

struct timespec {
	uint64_t tv_sec;
	uint64_t tv_nsec;
};

long syscall_thread_sync(size_t count,
  int *operation,
  long **addr,
  long *arg,
  long *res,
  struct timespec **spec);

long syscall_invalidate_kso(struct kso_invl_args *invl, size_t count);

long syscall_attach(uint64_t palo, uint64_t pahi, uint64_t chlo, uint64_t chhi, uint64_t flags);
long syscall_detach(uint64_t palo, uint64_t pahi, uint64_t chlo, uint64_t chhi, uint64_t flags);

long syscall_ocreate(uint64_t kulo,
  uint64_t kuhi,
  uint64_t tlo,
  uint64_t thi,
  uint64_t flags,
  objid_t *);
long syscall_odelete(uint64_t olo, uint64_t ohi, uint64_t flags);

#define THRD_CTL_ARCH_MAX 0xff

int arch_syscall_thrd_ctl(int op, long arg);
long syscall_thrd_ctl(int op, long arg);
