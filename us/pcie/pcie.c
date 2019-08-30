#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <twz/debug.h>
#include <twz/sys.h>
#include <twz/thread.h>

#include "pcie.h"

struct object pcie_cs_obj;
objid_t pcie_cs_oid;
static struct object pids;

static struct pcie_function *pcie_list = NULL;

static bool pcief_capability_get(struct pcie_function *pf, int id, union pcie_capability_ptr *cap)
{
	if(pf->config->device.cap_ptr) {
		size_t offset = pf->config->device.cap_ptr;
		do {
			cap->header = (struct pcie_capability_header *)((char *)pf->config + offset);

			if(cap->header->capid == id)
				return true;
			offset = cap->header->next;
		} while(offset != 0);
	}
	/* TODO: pcie extended caps? */
	return false;
}

void pcie_print_function(struct pcie_function *pf, bool nums)
{
	static bool _init = false;
	if(!_init && !nums) {
		if(twz_object_open_name(&pids, "pcieids", FE_READ)) {
			fprintf(stderr, "failed to open pcieids\n");
			return;
		}
		_init = true;
	}

	char *vname = NULL, *dname = NULL;
	uint16_t vendor = pf->config->header.vendor_id;
	uint16_t device = pf->config->header.device_id;
	uint16_t class = pf->config->header.class_code;
	uint16_t subclass = pf->config->header.subclass;
	uint16_t progif = pf->config->header.progif;
	const char *str = twz_obj_base(&pids);
	const char *line;
	for(line = str; line && *line; line = strchr(line, '\n')) {
		while(*line == '\n')
			line++;
		if(*line == '#')
			continue;

		if(*line != '\t') {
			char *endptr = NULL;
			if(strtol((char *)line, &endptr, 16) == vendor) {
				char *next = strchr(line, '\n');
				while(*endptr == ' ')
					endptr++;
				vname = strndup(endptr, next - endptr);

				line = next + 1;
				for(; line && *line; line = strchr(line, '\n')) {
					while(*line == '\n')
						line++;
					if(*line == '#')
						continue;
					if(*line != '\t')
						break;
					if(strtol((char *)line + 1, &endptr, 16) == device) {
						char *next = strchr(line, '\n');
						while(*endptr == ' ')
							endptr++;
						dname = strndup(endptr, next - endptr);
					}
				}
				break;
			}
		}
	}

	char *cname = NULL, *sname = NULL, *pname = NULL;

	char *cstart = strstr(str, "\nC ");

	for(line = cstart ? cstart + 1 : NULL; line && *line; line = strchr(line, '\n')) {
		while(*line == '\n')
			line++;
		if(*line == '#')
			continue;

		if(*line == 'C') {
			char *endptr = NULL;
			if(strtol((char *)line + 2, &endptr, 16) == class) {
				char *next = strchr(line, '\n');
				while(*endptr == ' ')
					endptr++;
				cname = strndup(endptr, next - endptr);

				line = next + 1;
				for(; line && *line; line = strchr(line, '\n')) {
					while(*line == '\n')
						line++;
					if(*line == '#')
						continue;
					if(*line != '\t')
						break;
					if(*(line + 1) == '\t')
						continue;
					if(strtol((char *)line + 1, &endptr, 16) == subclass) {
						char *next = strchr(line, '\n');
						while(*endptr == ' ')
							endptr++;
						sname = strndup(endptr, next - endptr);

						line = next + 1;

						for(; line && *line; line = strchr(line, '\n')) {
							while(*line == '\n')
								line++;
							if(*line == '#')
								continue;
							if(*line != '\t')
								break;
							if(*(line + 1) != '\t')
								break;
							if(strtol((char *)line + 2, &endptr, 16) == progif) {
								char *next = strchr(line, '\n');
								while(*endptr == ' ')
									endptr++;
								pname = strndup(endptr, next - endptr);

								line = next + 1;
							}
						}
					}
				}
				break;
			}
		}
	}

	// printf("%x %x %x : %x %x\n", class, subclass, progif, vendor, device);
	printf("  %s ", sname ? sname : cname);
	if(pname)
		printf("[%s] ", pname);
	printf(":: %s %s\n", vname, dname);
}

void pcie_init_function(struct pcie_function *pf)
{
	uint64_t wc = 0;
	if(pf->config->header.vendor_id == 0x1234 && pf->config->header.device_id == 0x1111) {
		/* TODO: generalize */
		wc = 1;
	}
	struct sys_kaction_args args = {
		.id = pcie_cs_oid,
		.cmd = KACTION_CMD_PCIE_INIT_DEVICE,
		.arg = pf->segment << 16 | pf->bus << 8 | pf->device << 3 | pf->function | (wc << 32),
		.flags = KACTION_VALID,
	};
	int r;
	if((r = sys_kaction(1, &args)) < 0) {
		fprintf(stderr, "kaction: %d\n", r);
	}
	if(args.result) {
		fprintf(stderr, "kaction-result: %d\n", args.result);
	}
}

static struct pcie_function *pcie_register_device(struct pcie_bus_header *space,
  volatile struct pcie_config_space *config,
  unsigned int bus,
  unsigned int device,
  unsigned int function)
{
	struct pcie_function *pf = malloc(sizeof(*pf));
	pf->config = config;
	pf->segment = space->segnr;
	pf->bus = bus;
	pf->device = device;
	pf->function = function;

	/*
	printf("[pcie] found %.2x:%.2x.%.2x\n", bus, device, function);
	printf("  vendor=%x, device=%x, subclass=%x, class=%x, progif=%x, type=%d\n",
	  config->header.vendor_id,
	  config->header.device_id,
	  config->header.subclass,
	  config->header.class_code,
	  config->header.progif,
	  HEADER_TYPE(config->header.header_type));

	if(HEADER_TYPE(config->header.header_type)) {
	    printf("[pcie] WARNING -- unimplemented: header_type 1\n");
	    return pf;
	}

	printf("  cap_ptr: %x, bar0: %x bar1: %x bar2: %x bar3: %x bar4: %x bar5: %x\n",
	  config->device.cap_ptr,
	  config->device.bar[0],
	  config->device.bar[1],
	  config->device.bar[2],
	  config->device.bar[3],
	  config->device.bar[4],
	  config->device.bar[5]);

	  */

	return pf;
}

static void pcie_init_space(struct pcie_bus_header *space)
{
	if(space->magic != PCIE_BUS_HEADER_MAGIC) {
		printf("[pcie]: invalid PCIe bus header\n");
		return;
	}
	printf("[pcie] initializing PCIe configuration space covering %.4d:%.2x-%.2x\n",
	  space->segnr,
	  space->start_bus,
	  space->end_bus);

	/* brute-force scan. We _could_ detect areas to look in based on bridges and stuff, but this
	 * doesn't take much longer and is still fast. */
	for(unsigned bus = space->start_bus; bus < space->end_bus; bus++) {
		for(unsigned device = 0; device < 32; device++) {
			uintptr_t addr = ((bus - space->start_bus) << 20 | device << 15);
			volatile struct pcie_config_space *config =
			  (void *)twz_ptr_lea(&pcie_cs_obj, (char *)space->spaces + addr);
			/* if a device isn't plugged in, the lines go high */
			if(config->header.vendor_id == 0xffff) {
				continue;
			}
			if(config->header.header_type & HEADER_MULTIFUNC) {
				/* for a multi-function device, brute-force scan all functions. We check for
				 * this
				 * before brute-force scanning all functions to reduce time by a factor of 8 */
				for(unsigned function = 0; function < 8; function++) {
					addr = ((bus - space->start_bus) << 20 | device << 15 | function << 12);
					config = (void *)twz_ptr_lea(&pcie_cs_obj, (char *)space->spaces + addr);
					if(config->header.vendor_id != 0xffff) {
						struct pcie_function *f =
						  pcie_register_device(space, config, bus, device, function);
						f->next = pcie_list;
						pcie_list = f;
					}
				}
			} else {
				/* not multi-function (function 0 is the only valid function) */
				struct pcie_function *f = pcie_register_device(space, config, bus, device, 0);
				f->next = pcie_list;
				pcie_list = f;
			}
		}
	}
}

int main(int argc, char **argv)
{
	printf("PCIE init:: %s\n", argv[1]);

	if(!objid_parse(argv[1], strlen(argv[1]), &pcie_cs_oid)) {
		printf("invalid object ID: %s\n", argv[1]);
		exit(1);
	}

	twz_object_open(&pcie_cs_obj, pcie_cs_oid, FE_READ | FE_WRITE);

	pcie_init_space(twz_obj_base(&pcie_cs_obj));

	for(struct pcie_function *pf = pcie_list; pf; pf = pf->next) {
		pcie_print_function(pf, false);
		pcie_init_function(pf);
	}

	int r;
	if((r = twz_thread_ready(NULL, THRD_SYNC_READY, 0))) {
		printf("failed to mark ready");
		exit(1);
	}

	for(;;)
		;
}
