#include <errno.h>
#include <string.h>
#include <twz/name.h>
#include <twz/sys.h>
#include <twz/thread.h>
#include <twz/view.h>

#include "syscalls.h"

struct process {
	struct thread thrd;
	int pid;
};

#define MAX_PID 1024
static struct process pds[MAX_PID];

struct elf64_header {
	uint8_t e_ident[16];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

extern int *__errno_location();
long linux_sys_execve(const char *path, const char *const *argv, char *const *env)
{
	objid_t id = 0;
	int r = twz_name_resolve(NULL, path, NULL, 0, &id);
	if(r) {
		return r;
	}

	objid_t vid;
	struct object view;
	if((r = twz_exec_create_view(&view, id, &vid)) < 0) {
		return r;
	}

	twix_copy_fds(&view);

	struct object exe;
	twz_object_open(&exe, id, FE_READ);
	struct elf64_header *hdr = twz_obj_base(&exe);

	return twz_exec_view(&view, vid, hdr->e_entry, argv, env);
}

asm(".global __return_from_clone\n"
    "__return_from_clone:"
    "popq %r15;"
    "popq %r14;"
    "popq %r13;"
    "popq %r12;"
    "popq %r11;"
    "popq %r10;"
    "popq %r9;"
    "popq %r8;"
    "popq %rbp;"
    "popq %rsi;"
    "popq %rdi;"
    "popq %rdx;"
    "popq %rbx;"
    "popq %rax;" /* ignore the old rsp */
    "movq $0, %rax;"
    "ret;");

asm(".global __return_from_fork\n"
    "__return_from_fork:"
    "popq %r15;"
    "popq %r14;"
    "popq %r13;"
    "popq %r12;"
    "popq %r11;"
    "popq %r10;"
    "popq %r9;"
    "popq %r8;"
    "popq %rbp;"
    "popq %rsi;"
    "popq %rdi;"
    "popq %rdx;"
    "popq %rbx;"
    "popq %rsp;"
    "movq $0, %rax;"
    "ret;");

extern uint64_t __return_from_clone(void);
extern uint64_t __return_from_fork(void);
long linux_sys_clone(struct twix_register_frame *frame,
  unsigned long flags,
  void *child_stack,
  int *ptid,
  int *ctid,
  unsigned long newtls)
{
	if(flags != 0x7d0f00) {
		return -ENOSYS;
	}

	memcpy((void *)((uintptr_t)child_stack - sizeof(struct twix_register_frame)),
	  frame,
	  sizeof(struct twix_register_frame));
	child_stack = (void *)((uintptr_t)child_stack - sizeof(struct twix_register_frame));
	struct thread thr;
	int r;
	if((r = twz_thread_spawn(&thr,
	      &(struct thrd_spawn_args){ .start_func = (void *)__return_from_clone,
	        .arg = NULL,
	        .stack_base = child_stack,
	        .stack_size = 8,
	        .tls_base = (char *)newtls }))) {
		return r;
	}

	/* TODO */
	static _Atomic int __static_thrid = 0;
	return ++__static_thrid;
}

#include <sys/mman.h>
long linux_sys_mmap(void *addr, size_t len, int prot, int flags, int fd, size_t off)
{
	if(addr != NULL && (flags & MAP_FIXED)) {
		return -ENOTSUP;
	}
	if(fd >= 0) {
		return -ENOTSUP;
	}
	if(!(flags & MAP_ANON)) {
		return -ENOTSUP;
	}

	/* TODO: fix all this up so its better */
	size_t slot = 0x10006ul;
	objid_t o;
	uint32_t fl;
	twz_view_get(NULL, slot, &o, &fl);
	if(!(fl & VE_VALID)) {
		objid_t nid = 0;
		twz_object_create(TWZ_OC_DFL_READ | TWZ_OC_DFL_WRITE, 0, 0, &nid);
		twz_view_set(NULL, slot, nid, FE_READ | FE_WRITE);
	}

	void *base = (void *)(slot * 1024 * 1024 * 1024 + 0x1000);
	struct metainfo *mi = (void *)((slot + 1) * 1024 * 1024 * 1024 - 0x1000);
	uint32_t *next = (uint32_t *)((char *)mi + mi->milen);
	if(*next + len > (1024 * 1024 * 1024 - 0x2000)) {
		return -1; // TODO allocate a new object
	}

	long ret = (long)(base + *next);
	*next += len;
	return ret;
}

#include <twz/thread.h>
long linux_sys_exit(int code)
{
	/* TODO: code */
	twz_thread_exit();
	return 0;
}

long linux_sys_set_tid_address()
{
	/* TODO: NI */
	return 0;
}

long linux_sys_fork(struct twix_register_frame *frame)
{
	int r;
	objid_t vid;
	if((r = twz_object_create(TWZ_OC_DFL_READ | TWZ_OC_DFL_WRITE, 0, 0, &vid))) {
		return r;
	}

	struct object view;
	if((r = twz_object_open(&view, vid, FE_READ | FE_WRITE))) {
		return r;
	}

	int pid = 0;
	for(int i = 1; i < MAX_PID; i++) {
		if(pds[i].pid == 0) {
			pid = i;
			break;
		}
	}

	if(pid == 0) {
		return -1;
	}
	pds[pid].pid = pid;
	twz_thread_create(&pds[pid].thrd);

	struct twzthread_repr *newrepr = twz_obj_base(&pds[pid].thrd.obj);

	objid_t sid;
	for(size_t i = 0; i <= TWZSLOT_MAX_SLOT; i++) {
		objid_t id;
		uint32_t flags;
		twz_view_get(NULL, i, &id, &flags);
		if(!(flags & VE_VALID)) {
			continue;
		}
		if(i == TWZSLOT_THRD) {
			if(flags & VE_FIXED)
				twz_view_fixedset(&pds[pid].thrd.obj, i, pds[pid].thrd.tid, flags);
			else
				twz_view_set(&view, i, pds[pid].thrd.tid, flags);
		} else if(i == TWZSLOT_CVIEW) {
			twz_view_set(&view, i, vid, VE_READ | VE_WRITE);
		} else if(i >= TWZSLOT_FILES_BASE
		          || !(flags & TWZ_OC_DFL_WRITE) /* TODO: this probably isn't safe */) {
			/* Copy directly */
			twz_view_set(&view, i, id, flags);
		} else {
			/* Copy-derive */
			objid_t nid;
			if((r = twz_object_create(
			      TWZ_OC_DFL_READ | TWZ_OC_DFL_WRITE | TWZ_OC_DFL_EXEC /* TODO */, 0, id, &nid))) {
				/* TODO: cleanup */
				return r;
			}
			if(flags & VE_FIXED)
				twz_view_fixedset(&pds[pid].thrd.obj, i, nid, flags);
			else
				twz_view_set(&view, i, nid, flags);
			if(i == TWZSLOT_STACK)
				sid = nid;
		}
	}

	if(!sid) /* TODO */
		return -EIO;

	struct object stack;
	twz_object_open(&stack, sid, FE_READ | FE_WRITE);

	size_t soff = (uint64_t)twz_ptr_local(frame) - 1024;
	void *childstack = twz_ptr_lea(&stack, (void *)soff);

	memcpy(childstack, frame, sizeof(struct twix_register_frame));

	uint64_t fs;
	asm volatile("rdfsbase %%rax" : "=a"(fs));

	struct sys_thrd_spawn_args sa = {
		.target_view = vid,
		.start_func = (void *)__return_from_fork,
		.arg = NULL,
		.stack_base = (void *)twz_ptr_rebase(TWZSLOT_STACK, soff),
		.stack_size = 8,
		.tls_base = (void *)fs,
		.thrd_ctrl = TWZSLOT_THRD,
	};

	if((r = sys_thrd_spawn(pds[pid].thrd.tid, &sa, 0))) {
		return r;
	}

	return pid;
}

struct rusage;
long linux_sys_wait4(long pid, int *wstatus, int options, struct rusage *rusage)
{
	// debug_printf("WAIT: %ld %p %x\n", pid, wstatus, options);

	while(1) {
		struct thread *thrd[MAX_PID];
		int sps[MAX_PID];
		long event[MAX_PID] = { 0 };
		uint64_t info[MAX_PID];
		int pids[MAX_PID];
		size_t c = 0;
		for(int i = 0; i < MAX_PID; i++) {
			if(pds[i].pid) {
				sps[c] = THRD_SYNC_EXIT;
				pids[c] = i;
				thrd[c++] = &pds[i].thrd;
			}
		}
		int r = twz_thread_wait(c, thrd, sps, event, info);

		for(unsigned int i = 0; i < c; i++) {
			if(event[i] && pds[pids[i]].pid) {
				if(wstatus) {
					*wstatus = 0; // TODO
				}
				pds[pids[i]].pid = 0;
				return pids[i];
			}
		}
	}
}