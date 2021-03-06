#include <clksrc.h>
#include <debug.h>
#include <kalloc.h>
#include <lib/iter.h>
#include <object.h>
#include <page.h>
#include <pager.h>
#include <processor.h>
#include <thread.h>
#include <time.h>

#define TIMESLICE_MIN 50000000
#define TIMESLICE_GIVEUP 10000
#define TIMESLICE_SCALE 20000

#include <arch/x86_64-msr.h>

static void thread_resume(struct thread *thr, uint64_t timeout)
{
	arch_thread_resume(thr, timeout);
}

#define min(a, b) ({ ((a) < (b) ? (a) : (b)); })

__noinstrument void thread_schedule_resume_proc(struct processor *proc)
{
	uint64_t ji = clksrc_get_nanoseconds();
	mm_update_stats();

	if(0 && ++proc->ctr % 10 == 0) {
		uint32_t lom, him, loa, hia;
		x86_64_rdmsr(0xe7, &lom, &him);
		x86_64_rdmsr(0xe8, &loa, &hia);
		uint64_t mperf = (long)him << 32 | (long)lom;
		uint64_t aperf = (long)hia << 32 | (long)loa;
		printk(":: %lx %lx %lx\n", aperf, mperf, (aperf * 1000) / mperf);
		x86_64_wrmsr(0xe7, 0, 0);
		x86_64_wrmsr(0xe8, 0, 0);
	}

	if(current_thread && current_thread->state == THREADSTATE_EXITED) {
		arch_processor_reset_current_thread(proc);
	}

	while(true) {
		pager_idle_task();
		workqueue_dowork(&proc->wq);
		uint64_t rem_time = timer_check_timers();
		spinlock_acquire(&proc->sched_lock);

		if(current_thread && current_thread->timeslice_expire > (ji + TIMESLICE_GIVEUP)
		   && current_thread->state == THREADSTATE_RUNNING) {
#if 0
			printk("resuming current: %ld (%ld)\n",
			  current_thread->id,
			  current_thread->timeslice_expire - ji);
#endif
			spinlock_release(&proc->sched_lock, 0);
			// clksrc_set_interrupt_countdown(current_thread->timeslice_expire - ji, false);

			uint64_t timeout = current_thread->timeslice_expire - ji;
			timeout = min(timeout, rem_time);
			thread_resume(current_thread, timeout);
		}
		if(current_thread && current_thread->timeslice_expire <= ji) {
#if 0
			printk("%ld ::: %d [%d;%lx;%ld]\n",
			  current_thread->id,
			  current_thread->priority,
			  current_thread->arch.was_syscall,
			  current_thread->arch.was_syscall ? current_thread->arch.syscall.rcx
			                                   : current_thread->arch.exception.rip,
			  current_thread->arch.was_syscall ? current_thread->arch.syscall.rax
			                                   : current_thread->arch.exception.int_no);
#endif
			if(current_thread->priority > 1)
				current_thread->priority--;
		}

		//	if(current_thread && current_thread->state == THREADSTATE_RUNNING) {
		//		list_insert(&proc->runqueue, &current_thread->rq_entry);
		//	} else if(current_thread && current_thread->state == THREADSTATE_BLOCKING) {
		//		current_thread->state = THREADSTATE_BLOCKED;
		//	}
		struct list *ent = list_dequeue(&proc->runqueue);
		if(ent) {
			bool empty = list_empty(&proc->runqueue);
			struct thread *next = list_entry(ent, struct thread, rq_entry);
			list_insert(&proc->runqueue, &next->rq_entry);
			spinlock_release(&proc->sched_lock, 0);

			if(next->timeslice_expire < ji)
				next->timeslice_expire = ji + TIMESLICE_MIN + next->priority * TIMESLICE_SCALE;
			else if(next->timeslice_expire < (ji + TIMESLICE_GIVEUP))
				next->timeslice_expire += TIMESLICE_GIVEUP;

#if 0
			printk("%ld: %ld/%ld (%d) %ld\n",
			  next->id,
			  ji,
			  next->timeslice_expire,
			  empty,
			  current_thread ? current_thread->arch.exception.int_no : 0);
#endif
			if(!empty) {
#if 0
				printk("  set countdown %ld (%ld)\n",
				  next->timeslice_expire - ji,
				  clksrc_get_interrupt_countdown());
#endif
				//			clksrc_set_interrupt_countdown(next->timeslice_expire - ji, false);
			}
			assertmsg(next->state == THREADSTATE_RUNNING,
			  "%ld threadstate is %d (%d)",
			  next->id,
			  next->state,
			  empty);
			if(next != current_thread) {
				proc->stats.thr_switch++;
			}

			uint64_t timeout = next->timeslice_expire - ji;
			timeout = min(timeout, rem_time);
			thread_resume(next, empty ? rem_time : timeout);
		} else {
			proc->flags &= ~PROCESSOR_HASWORK;
			spinlock_release(&proc->sched_lock, 1);
			/* we're halting here, but the arch_processor_halt function will return
			 * after an interrupt is fired. Since we're in kernel-space, any interrupt
			 * we get will not invoke the scheduler. */
			if(pager_idle_task()) {
				proc->flags |= PROCESSOR_HASWORK;
			}
			page_idle_zero();
			rem_time = timer_check_timers();
			spinlock_acquire(&proc->sched_lock);
			if(!processor_has_threads(proc) && !(proc->flags & PROCESSOR_HASWORK)) {
				spinlock_release(&proc->sched_lock, 0);
				if(rem_time > 0) {
					clksrc_set_interrupt_countdown(rem_time, false);
				}
				arch_processor_halt(proc);
			} else {
				spinlock_release(&proc->sched_lock, 1);
			}
		}
	}
}

static void __schedule_timer_handler(int v, struct interrupt_handler *hdl)
{
	(void)v;
	(void)hdl;
	if(current_thread) {
#if 0
		printk("%ld TIMER: %ld\n", current_thread->id, clksrc_get_interrupt_countdown());
#endif
		// current_thread->timeslice = clksrc_get_interrupt_countdown();
	}
}

struct interrupt_handler _timer_handler = {
	.fn = __schedule_timer_handler,
};

__initializer static void __init_int_timer(void)
{
	/* TODO: arch-dep */
	interrupt_register_handler(32, &_timer_handler);
}

#include <slab.h>

static _Atomic unsigned long _internal_tid_counter = 0;

static void _thread_ctor(void *_u __unused, void *ptr)
{
	struct thread *thr = ptr;
	thr->id = ++_internal_tid_counter;
	thr->sc_lock = SPINLOCK_INIT;
	thr->lock = SPINLOCK_INIT;
	thr->state = THREADSTATE_INITING;
	if(!thr->sctx_entries) {
		thr->sctx_entries = kcalloc(MAX_SC, sizeof(struct thread_sctx_entry));
	} else {
		memset(thr->sctx_entries, 0, sizeof(struct thread_sctx_entry) * MAX_SC);
	}
}

static DECLARE_SLABCACHE(_sc_thread, sizeof(struct thread), _thread_ctor, NULL, NULL);

__noinstrument void thread_schedule_resume(void)
{
	assert(current_thread != NULL);
	thread_schedule_resume_proc(current_processor);
}

void thread_sleep(struct thread *t, int flags)
{
	(void)flags;
	t->priority *= 20;
	if(t->priority > 1000) {
		t->priority = 1000;
	}
	spinlock_acquire_save(&t->processor->sched_lock);
	if(t->state != THREADSTATE_BLOCKED) {
		t->state = THREADSTATE_BLOCKED;
		list_remove(&t->rq_entry);
		t->processor->stats.running--;
	}
	spinlock_release_restore(&t->processor->sched_lock);
}

void thread_wake(struct thread *t)
{
	spinlock_acquire_save(&t->processor->sched_lock);
	int old = atomic_exchange(&t->state, THREADSTATE_RUNNING);
	if(old == THREADSTATE_BLOCKED) {
		list_insert(&t->processor->runqueue, &t->rq_entry);
		t->processor->stats.running++;
		t->processor->flags |= PROCESSOR_HASWORK;
		if(t->processor != current_processor) {
			spinlock_release_restore(&t->processor->sched_lock);
			arch_processor_scheduler_wakeup(t->processor);
			return;
		}
	}
	spinlock_release_restore(&t->processor->sched_lock);
}
static DECLARE_LIST(allthreads);
static struct spinlock allthreads_lock = SPINLOCK_INIT;

static void __thread_finish_cleanup2(void *_t)
{
	struct thread *t = _t;
	vm_context_put(t->ctx);
	t->ctx = NULL;
	arch_thread_destroy(t);
	thread_sync_uninit_thread(t);
	void *back = t->sctx_entries;
	memset(t, 0, sizeof(*t));
	t->sctx_entries = back;
	// memset(&t->arch, 0, sizeof(t->arch));
	_thread_ctor(NULL, t);
	slabcache_free(&_sc_thread, t);
}

static void __thread_finish_cleanup(void *_t)
{
	struct thread *t = _t;

	// printk("THREAD DESTROY %ld\n", t->id);
	workqueue_insert(&current_processor->wq, &t->free_task, __thread_finish_cleanup2, t);
}

void thread_exit(void)
{
	timer_remove(&current_thread->sleep_timer);
	list_remove(&current_thread->rq_entry);
	current_thread->processor->stats.running--;
	current_thread->state = THREADSTATE_EXITED;
	assert(current_processor->load > 0);
	current_processor->load--;

	spinlock_acquire_save(&allthreads_lock);
	list_remove(&current_thread->all_entry);
	spinlock_release_restore(&allthreads_lock);

	kso_root_detach(current_thread->kso_attachment_num);

	if(current_thread->pending_fault_info) {
		kfree(current_thread->pending_fault_info);
		current_thread->pending_fault_info = NULL;
	}

	struct object *obj = kso_get_obj(current_thread->throbj, thr);
	obj_free_kaddr(obj);
	obj_put(obj); /* one for kso, one for this ref. TODO: clean this up */
	obj_put(obj);

	workqueue_insert(
	  &current_processor->wq, &current_thread->free_task, __thread_finish_cleanup, current_thread);
}

void thread_print_all_threads(void)
{
	spinlock_acquire_save(&allthreads_lock);
	foreach(e, list, &allthreads) {
		struct thread *t = list_entry(e, struct thread, all_entry);

		spinlock_acquire_save(&t->lock);
		printk("thread %ld\n", t->id);
		printk("  CPU: %d\n", t->processor ? (int)t->processor->id : -1);
		printk("  state: %d\n", t->state);
		spinlock_release_restore(&t->lock);
	}
	spinlock_release_restore(&allthreads_lock);
}

struct thread *thread_create(void)
{
	struct thread *t = slabcache_alloc(&_sc_thread);
	// printk("THREAD_CREATE: %ld: %p\n", t->id, t);
	// krc_init(&t->refs);
	t->priority = 10;
	spinlock_acquire_save(&allthreads_lock);
	list_insert(&allthreads, &t->all_entry);
	spinlock_release_restore(&allthreads_lock);
	return t;
}

#include <debug.h>
#include <twz/_thrd.h>
static void __print_fault_info(struct thread *t, int fault, void *info)
{
	printk("unhandled fault: %ld: %d\n", t ? (long)t->id : -1, fault);
	struct object *to = kso_get_obj(t->throbj, thr);
	struct kso_hdr *kh = obj_get_kbase(to);
	printk("   %s\n", kh->name);
	obj_put(to);
	// debug_print_backtrace();
	switch(fault) {
		struct fault_object_info *foi;
		struct fault_null_info *fni;
		struct fault_exception_info *fei;
		struct fault_sctx_info *fsi;
		case FAULT_OBJECT:
			foi = info;
			printk("foi->objid " IDFMT "\n", IDPR(foi->objid));
			printk("foi->ip    %p\n", foi->ip);
			printk("foi->addr  %p\n", foi->addr);
			printk("foi->flags %lx\n", foi->flags);
			break;
		case FAULT_NULL:
			fni = info;
			printk("fni->ip    %p\n", fni->ip);
			printk("fni->addr  %p\n", fni->addr);
			break;
		case FAULT_EXCEPTION:
			fei = info;
			printk("fei->ip    %p\n", fei->ip);
			printk("fei->code  %lx\n", fei->code);
			printk("fei->arg0  %lx\n", fei->arg0);
			/*unsigned char *c = fei->ip;
			for(int i = 0; i < 16; i++)
			    printk("%x ", c[i]);
			printk("\n");*/
			break;
		case FAULT_SCTX:
			fsi = info;
			printk("fsi->target " IDFMT "\n", IDPR(fsi->target));
			printk("fsi->ip     %p\n", fsi->ip);
			printk("fsi->addr   %p\n", fsi->addr);
			printk("fsi->pneed  %x\n", fsi->pneed);
			break;
	}
}

static void *__failed_addr(int f, void *info)
{
	switch(f) {
		struct fault_object_info *foi;
		struct fault_null_info *fni;
		struct fault_exception_info *fei;
		struct fault_sctx_info *fsi;
		case FAULT_OBJECT:
			foi = info;
			return foi->addr;
			break;
		case FAULT_NULL:
			fni = info;
			return fni->addr;
			break;
		case FAULT_EXCEPTION:
			fei = info;
			return fei->ip;
			break;
		case FAULT_SCTX:
			fsi = info;
			return fsi->addr;
			break;
	}
	return 0;
}

/* TODO: handle failure when no data object for executable exists (fault before default fault
 * handler can be set */
void thread_raise_fault(struct thread *t, int fault, void *info, size_t infolen)
{
	if(!t) {
		__print_fault_info(NULL, fault, info);
		panic("thread fault occurred before threading");
	}
	struct object *to = kso_get_obj(t->ctx->view, view);
	if(!to) {
		panic("No repr");
	}
	void *handler;
	obj_read_data(to, __VE_FAULT_HANDLER_OFFSET, sizeof(handler), &handler);
	//__print_fault_info(t, fault, info);
	if(handler) {
		obj_put(to);
		if(__failed_addr(fault, info) == handler) {
			/* probably a double-fault. Just die */
			__print_fault_info(t, fault, info);
			thread_exit();
			return;
		}
		arch_thread_raise_call(t, handler, fault, info, infolen);
	} else {
		obj_read_data(to, __VE_DBL_FAULT_HANDLER_OFFSET, sizeof(handler), &handler);
		obj_put(to);
		if(handler) {
			if((void *)__failed_addr(fault, info) == handler) {
				/* probably a double-fault. Just die */
				__print_fault_info(t, fault, info);
				thread_exit();
				return;
			}

			/* thread can catch an unhandled fault */
			struct fault_fault_info ffi = twz_fault_build_fault_info(fault, 0, infolen);
			size_t nl = infolen + sizeof(ffi);
			nl = ((nl - 1) & ~0xf) + 0x10; /* infolen must be 16 aligned */
			char tmp[nl];
			memcpy(tmp, &ffi, sizeof(ffi));
			memcpy(tmp + sizeof(ffi), info, infolen);
			arch_thread_raise_call(t, handler, FAULT_FAULT, tmp, nl);
		} else {
			__print_fault_info(t, fault, info);
			thread_exit();
		}
	}
}

void thread_queue_fault(struct thread *thr, int fault, void *info, size_t infolen)
{
	void *ptr = kalloc(infolen);
	memcpy(ptr, info, infolen);
	spinlock_acquire_save(&thr->lock);
	if(thr->pending_fault_info) {
		spinlock_release_restore(&thr->lock);
		printk("warning - unable to queue fault to thread %ld\n", thr->id);
		kfree(ptr);
		return;
	}

	thr->pending_fault_info = ptr;
	thr->pending_fault = fault;
	thr->pending_fault_infolen = infolen;

	spinlock_release_restore(&thr->lock);
}
