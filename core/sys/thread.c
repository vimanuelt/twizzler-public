#include <thread.h>

long syscall_thread_spawn(__int128 foo)
{
	printk("%lx %lx\n", (uint64_t)(foo >> 64), (uint64_t)(foo & 0xFFFFFFFFFFFFFFFF));
	
	for(;;);
	return (uint64_t)(foo);
}
