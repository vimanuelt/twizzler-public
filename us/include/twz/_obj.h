#pragma once

#include <stdint.h>

#define IDFMT "%lx:%lx"
#define IDPR(x) (uint64_t)(x >> 64), (uint64_t)(x & 0xffffffffffffffff)

#define MI_MAGIC 0x54575A4F

#define MIF_SZ 0x1

#define MIP_HASHDATA 0x1
#define MIP_HASHKUID 0x2
#define MIP_DFL_READ 0x4
#define MIP_DFL_WRITE 0x8
#define MIP_DFL_EXEC 0x10
#define MIP_DFL_USE 0x20

#define OBJ_NULLPAGE_SIZE 0x1000
#define OBJ_METAPAGE_SIZE 0x1000

#define OBJ_MAXSIZE (1ul << 30)

typedef unsigned __int128 nonce_t;
typedef unsigned __int128 objid_t;

struct metainfo {
	uint32_t magic;
	uint16_t flags;
	uint16_t p_flags;
	uint16_t milen;
	uint16_t fotentries;
	uint32_t mdbottom;
	uint64_t sz;
	uint32_t nbuckets;
	uint32_t hashstart;
	nonce_t nonce;
	objid_t kuid;
	char data[];
} __packed;

#define FE_READ MIP_DFL_READ
#define FE_WRITE MIP_DFL_WRITE
#define FE_EXEC MIP_DFL_EXEC
#define FE_USE MIP_DFL_USE
#define FE_NAME 0x1000
#define FE_DERIVE 0x2000

struct fotentry {
	union {
		objid_t id;
		struct {
			char *data;
			void *nresolver;
		} name;
	};

	uint64_t flags;
	uint64_t info;
};
