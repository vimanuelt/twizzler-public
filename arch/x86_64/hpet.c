#include <system.h>
#include <debug.h>
#include <memory.h>
#include <arch/x86_64-acpi.h>

struct __packed hpet_desc {
	struct sdt_header header;
	uint8_t hw_rev_id;
	uint8_t comp_count:5;
	uint8_t counter_size:1;
	uint8_t _res:1;
	uint8_t leg_repl:1;
	uint16_t ven_id;
	uint32_t pci_addr_data;
	uint64_t address;
	uint8_t hpet_number;
	uint16_t min_tick;
	uint8_t page_prot;
};

static struct hpet_desc *hpet;
static uint32_t countperiod;

__noinstrument static inline uint64_t hpet_read64(int offset)
{
	return *(volatile uint64_t *)(mm_ptov(hpet->address + offset));
}

__noinstrument static inline void hpet_write64(int offset, uint64_t data)
{
	*(volatile uint64_t *)(mm_ptov(hpet->address + offset)) = data;
}

__orderedinitializer(__orderedafter(ACPI_INITIALIZER_ORDER))
static void hpet_init(void)
{
	if(!(hpet = acpi_find_table("HPET"))) {
		panic("HPET not found");
	}

	uint64_t tmp = hpet_read64(0);
	countperiod = tmp >> 32;
	/* enable */
	tmp = hpet_read64(0x10);
	tmp |= 1;
	hpet_write64(0x10, tmp);
}

