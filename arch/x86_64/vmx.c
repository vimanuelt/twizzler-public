#include <memory.h>
#include <arch/x86_64-msr.h>
#include <processor.h>
#define VMCS_SIZE 0x1000
static uint32_t revision_id;

enum vmcs_fields {
	VMCS_VM_INSTRUCTION_ERROR = 0x4400,
};

static const char *vm_errs[] = {
	"Success",
	"VMCALL in vmx-root",
	"VMCLEAR with invalid address",
	"VMCLEAR with VMXON ptr",
	"VMLAUNCH with non-clear VMCS",
	"VMRESUME non-launched VMCS",
	"VMRESUME after VMXOFF",
	"VM entry with invalid control fields",
	"VM entry with invalid host-state fields",
	"VMPTRLD with invalid address",
	"VMPTRLD with VMXON ptr",
	"VMPTRLD with incorrect revision ID",
	"VM read/write from/to unsupported component",
	"VM write to read-only component",
	"[reserved]",
	"VMXON in vmx-root",
	"VM entry with invalid executive VMCS pointer",
	"VM entry with non-launched VMCS",
	"VM entry something?",
	"VMCALL with non-clear VMCS",
	"VMCALL with invalid VM exit control fields",
	"[reserved]",
	"VMCALL with incorrect revision ID",
	"VMXOFF in dual monitor",
	"VMCALL with invalid SMM features",
	"VM entry with invalid VM execution control fields",
	"VM entry with events blocked by mov ss",
	"[reserved]",
	"invalid operand to INVEPT/INVVPID"
};

static inline unsigned long vmcs_readl(unsigned long field)
{
	unsigned long val;
	asm volatile("vmread %%rdx, %%rax" : "=a"(val) : "d"(field) : "cc");
	return val;
}

static inline const char *vmcs_read_vminstr_err(void)
{
	uint32_t err = vmcs_readl(VMCS_VM_INSTRUCTION_ERROR);
	if(err >= array_len(vm_errs)) {
		panic("invalid error code from vm instruction error field");
	}
	return vm_errs[err];
}

static inline void vmcs_writel(unsigned long field, unsigned long val)
{
	uint8_t err;
	asm volatile("vmwrite %%rax, %%rdx" : "=q"(err) : "a"(val), "d"(field) : "cc");
	if(unlikely(err)) {
		panic("vmwrite error: %s", vmcs_read_vminstr_err());
	}
}

static uint32_t __attribute__((noinline)) cpuid(uint32_t x, int rnum)
{
	uint32_t regs[4];
	asm volatile("push %%rbx; cpuid; mov %%ebx, %0; pop %%rbx" : "=a"(regs[0]), "=r"(regs[1]), "=c"(regs[2]), "=d"(regs[3]) : "a"(x));
	return regs[rnum];
}

static void x86_64_enable_vmx(void)
{
	uint32_t ecx = cpuid(1, 2);
	if(!(ecx & (1 << 5))) {
		panic("VMX extensions not available (not supported");
	}

	uint32_t lo, hi;
	x86_64_rdmsr(X86_MSR_FEATURE_CONTROL, &lo, &hi);
	if(!(lo & 1) /* lock bit */ || !(lo & (1 << 2)) /* enable-outside-smx bit */) {
		panic("VMX extensions not available (not enabled)");
	}

	/* okay, now try to enable VMX instructions. */
	uint64_t cr4;
	asm volatile("mov %%cr4, %0" : "=r"(cr4));
	cr4 |= (1 << 13); //enable VMX
	uintptr_t vmxon_region = mm_physical_alloc(0x1000, PM_TYPE_DRAM, true);
	
	x86_64_rdmsr(X86_MSR_VMX_BASIC, &lo, &hi);
	revision_id = lo & 0x7FFFFFFF;
	uint32_t *vmxon_revid = (uint32_t *)(vmxon_region + PHYSICAL_MAP_START);
	*vmxon_revid = revision_id;

	uint8_t error;
	asm volatile("mov %1, %%cr4; vmxon %2; setc %0" :"=g"(error): "r"(cr4), "m"(vmxon_region) : "cc");
	if(error) {
		panic("failed to enter VMX operation");
	}
	printk("VMX enabled.\n");
}


__attribute__((used)) static void x86_64_vmentry_failed(void)
{
	printk("HERE! %s\n", vmcs_read_vminstr_err());
	for(;;);
}

void x86_64_vmenter(struct processor *proc)
{
	/* VMCS does not deal with CPU registers, so we must save and restore them. */

	asm volatile(
			"pushf;"
			"cmp $0, %0;"
			"mov %c[cr2](%%rcx), %%rax;"
			"mov %%rax, %%cr2;"
			"mov %c[rax](%%rcx), %%rax;"
			"mov %c[rbx](%%rcx), %%rbx;"
			"mov %c[rdx](%%rcx), %%rdx;"
			"mov %c[rdi](%%rcx), %%rdi;"
			"mov %c[rsi](%%rcx), %%rsi;"
			"mov %c[rbp](%%rcx), %%rbp;"
			"mov %c[r8](%%rcx),  %%r8;"
			"mov %c[r9](%%rcx),  %%r9;"
			"mov %c[r10](%%rcx), %%r10;"
			"mov %c[r11](%%rcx), %%r11;"
			"mov %c[r12](%%rcx), %%r12;"
			"mov %c[r13](%%rcx), %%r13;"
			"mov %c[r14](%%rcx), %%r14;"
			"mov %c[r15](%%rcx), %%r15;"
			"mov %c[rcx](%%rcx), %%rcx;" //this kills rcx
			"jne launched;"
			"vmlaunch; jmp failed;"
			"launched: vmresume; failed:"
			"popf;"
			"call x86_64_vmentry_failed;"
			::
			"r"(proc->arch.launched),
			"c"(proc->arch.vcpu_state_regs),
			[rax]"i"(REG_RAX*8),
			[rbx]"i"(REG_RBX*8),
			[rcx]"i"(REG_RCX*8),
			[rdx]"i"(REG_RDX*8),
			[rdi]"i"(REG_RDI*8),
			[rsi]"i"(REG_RSI*8),
			[rbp]"i"(REG_RBP*8),
			[r8] "i"(REG_R8 *8),
			[r9] "i"(REG_R9 *8),
			[r10]"i"(REG_R10*8),
			[r11]"i"(REG_R11*8),
			[r12]"i"(REG_R12*8),
			[r13]"i"(REG_R13*8),
			[r14]"i"(REG_R14*8),
			[r15]"i"(REG_R15*8),
			[cr2]"i"(REG_CR2*8));
}

void vmx_entry_point(void)
{
	for(;;);
}

void vmx_exit_point(void)
{

}

void vtx_setup_vcpu(struct processor *proc)
{
	/* we have to set-up the vcpu state to "mirror" our physical CPU.
	 * Strap yourself in, it's gonna be a long ride. */

	/* segment selectors */
	vmcs_writel(VMCS_GUEST_CS_SEL, 0x8);
	vmcs_writel(VMCS_GUEST_CS_BASE, 0);
	vmcs_writel(VMCS_GUEST_CS_LIM, 0xffff);
	vmcs_writel(VMCS_GUEST_CS_ARBYTES, 0xA09B);
	/* TODO: do we need to do the other segments? */

	vmcs_writel(VMCS_GUEST_TR_SEL, 0);
	vmcs_writel(VMCS_GUEST_TR_BASE, 0);
	vmcs_writel(VMCS_GUEST_TR_LIM, 0xffff);
	vmcs_writel(VMCS_GUEST_TR_ARBYTES, 0x008b);

	vmcs_writel(VMCS_GUEST_LDTR_SEL, 0);
	vmcs_writel(VMCS_GUEST_LDTR_BASE, 0);
	vmcs_writel(VMCS_GUEST_LDTR_LIM, 0xffff);
	vmcs_writel(VMCS_GUEST_LDTR_ARBYTES, 0x0082);

	/* GDT */
	vmcs_writel(VMCS_GUEST_GDTR_BASE, (uintptr_t)&proc->arch.gdt);
	vmcs_writel(VMCS_GUEST_GDTR_LIM, sizeof(struct x86_64_gdt_entry) * 8 - 1);

	/* TODO: ldt, idt? */

	/* CPU control info and stack */
	vmcs_writel(VMCS_GUEST_RFLAGS, 0x02);
	vmcs_writel(VMCS_GUEST_RIP, vmx_entry_point);
	vmcs_writel(VMCS_GUEST_RSP, proc->arch.kernel_stack + KERNEL_STACK_SIZE);
	
	/* TODO: debug registers? */


	/* TODO: what? */
	vmcs_writel(VMCS_GUEST_ACTIVITY_STATE, 0);
	vmcs_writel(VMCS_GUEST_INTRRUPTIBILITY_INFO, 0);
	vmcs_writel(VMCS_GUEST_PENDING_DBG_EXCEPTIONS, 0);
	vmcs_writel(VMCS_GUEST_IA32_DEBUGCTL, 0);

	/* I/O */
	vmcs_writel(VMCS_IO_BITMAP_A, 0); //TODO: what values?
	vmcs_writel(VMCS_IO_BITMAP_B, 0);

	vmcs_writel(VMCS_LINK_POINTER, ~0ull);

	/* VM control fields. */
	/* TODO: PROCBASED */
	// PINBASED_CTLS
	
	vmcs_writel(VMCS_EXCEPTION_BITMAP, 0);
	vmcs_writel(VMCS_PF_ERROR_CODE_MASK, 0);
	vmcs_writel(VMCS_PF_ERROR_CODE_MATCH, 0);

	vmcs_writel(VMCS_HOST_CR0, read_cr(0));
	vmcs_writel(VMCS_HOST_CR3, read_cr(1));
	vmcs_writel(VMCS_HOST_CR4, read_cr(1));

	vmcs_writel(VMCS_HOST_CS_SEL, 0x8);
	vmcs_writel(VMCS_HOST_DS_SEL, 0x10);
	vmcs_writel(VMCS_HOST_ES_SEL, 0x10);
	vmcs_writel(VMCS_HOST_FS_SEL, 0x10);
	vmcs_writel(VMCS_HOST_GS_SEL, 0x10);
	vmcs_writel(VMCS_HOST_SS_SEL, 0x10);
	vmcs_writel(VMCS_HOST_TS_SEL, 0x2B);

}

void x86_64_start_vmx(struct processor *proc)
{
	x86_64_enable_vmx();

	proc->arch.launched = 0;
	memset(proc->arch.vcpu_state_regs, 0, sizeof(proc->arch.vcpu_state_regs));
	printk("Starting VMX system\n");
	proc->arch.vmcs = mm_physical_alloc(VMCS_SIZE, PM_TYPE_DRAM, true);
	uint32_t *vmcs_rev = (uint32_t *)(proc->arch.vmcs + PHYSICAL_MAP_START);
	*vmcs_rev = revision_id & 0x7FFFFFFF;

	uint8_t error;
	asm volatile("vmptrld (%%rax); setna %0" : "=g"(error) : "a"(&proc->arch.vmcs) : "cc");
	if(error) {
		panic("failed to load VMCS region: %s", vmcs_read_vminstr_err());
	}

	printk("Got here.\n");






	printk("Launching!\n");

	x86_64_vmenter(proc);

	for(;;);
	asm volatile("vmlaunch; setna %0" : "=g"(error) :: "cc");
	if(error) {
		panic("error occurred during VMLAUNCH: %s", vmcs_read_vminstr_err());
	}
	for(;;);
}

