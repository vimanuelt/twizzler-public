#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <twz/alloc.h>
#include <twz/obj.h>
#include <twz/security.h>

#define align_up(x, a) ({ ((x) + (a)-1) & ~(a - 1); })

int twz_sctx_init(twzobj *obj, const char *name)
{
	struct secctx *sc = twz_object_base(obj);
	_Static_assert(sizeof(struct twzoa_header) <= sizeof(sc->userdata));
	if(name) {
		kso_set_name(obj, name);
	}
	sc->nbuckets = 1024;
	sc->nchain = 4096;
	struct twzoa_header *oa = (void *)sc->userdata;
	oa_hdr_init(obj,
	  oa,
	  align_up(sizeof(*sc) + sizeof(struct scbucket) * (sc->nbuckets + sc->nchain), 0x1000),
	  OBJ_TOPDATA);

	return 0;
}

static int __sctx_add_bucket(struct secctx *sc,
  objid_t target,
  void *ptr,
  uint32_t pmask,
  struct scgates *gatemask)
{
	size_t slot = target % sc->nbuckets;
	while(1) {
		struct scbucket *b = &sc->buckets[slot];
		if(b->target == 0) {
			b->target = target;
			b->data = twz_ptr_local(ptr);
			b->flags = 0;
			b->gatemask = gatemask ? *gatemask : (struct scgates){ 0 };
			b->pmask = pmask;
			break;
		}
		slot = b->chain;
		if(slot == 0) {
			for(size_t i = sc->nbuckets; i < sc->nbuckets + sc->nchain; i++) {
				struct scbucket *n = &sc->buckets[slot];
				if(n->chain == 0 && n->target == 0) {
					b->chain = slot = i;
					break;
				}
			}
			if(slot == 0) {
				return -ENOSPC;
			}
		}
	}
	return 0;
}

int twz_sctx_add(twzobj *obj,
  objid_t target,
  void *item,
  size_t itemlen,
  uint32_t pmask,
  struct scgates *gatemask)
{
	struct secctx *sc = twz_object_base(obj);
	struct twzoa_header *oa = (void *)sc->userdata;
	void *data = oa_hdr_alloc(obj, oa, itemlen);
	if(!data) {
		return -ENOMEM;
	}
	memcpy(data, item, itemlen);

	int r = __sctx_add_bucket(sc, target, data, pmask, gatemask);
	if(r) {
		/* TODO: twz_ptr_local for free? */
		oa_hdr_free(obj, oa, twz_ptr_local(data));
	}

	return r;
}

/*
struct sccap {
    objid_t target;
    objid_t accessor;
    struct screvoc rev;
    struct scgates gates;
    uint32_t perms;
    uint16_t magic;
    uint16_t flags;
    uint16_t htype;
    uint16_t etype;
    uint16_t slen;
    uint16_t pad;
    char sig[];
} __attribute__((packed));
*/

#define LTM_DESC
#include <tomcrypt.h>

#include <err.h>
static void sign_dsa(unsigned char *hash,
  size_t hl,
  const unsigned char *b64data,
  size_t b64len,
  unsigned char *sig,
  size_t *siglen)
{
	register_prng(&sprng_desc);
	ltc_mp = ltm_desc;
	size_t kdlen = b64len;
	unsigned char *kd = malloc(b64len);
	int e;
	if((e = base64_decode(b64data, b64len, kd, &kdlen)) != CRYPT_OK) {
		errx(1, "b64_decode: %s", error_to_string(e));
	}
	dsa_key k;
	if((e = dsa_import(kd, kdlen, &k)) != CRYPT_OK) {
		errx(1, "dsa_import: %s", error_to_string(e));
	}

	if((e = dsa_sign_hash(hash, hl, sig, siglen, NULL, find_prng("sprng"), &k)) != CRYPT_OK) {
		errx(1, "dsa_sign_hash: %s", error_to_string(e));
	}

	int stat;
	if((e = dsa_verify_hash(sig, *siglen, hash, hl, &stat, &k)) != CRYPT_OK) {
		errx(1, "dsa_verify_hash: %s", error_to_string(e));
	}

	if(!stat) {
		errx(1, "failed to test validate signature");
	}
}

int twz_key_new(twzobj *pri, twzobj *pub)
{
	/* probaby shouldn't do this on every function call */
	ltc_mp = ltm_desc;

	prng_state prng;
	int err;
	/* register yarrow */
	if(register_prng(&fortuna_desc) == -1) {
		printf("Error registering Fortuna\n");
		return -1;
	}
	/* setup the PRNG */
	if((err = rng_make_prng(128, find_prng("fortuna"), &prng, NULL)) != CRYPT_OK) {
		printf("Error setting up PRNG, %s\n", error_to_string(err));
		return -1;
	}
	/* make a 192-bit ECC key */
	// if((err = ecc_make_key(&prng, find_prng("yarrow"), 24, &mykey)) != CRYPT_OK) {
	//	printf("Error making key: %s\n", error_to_string(err));
	//	return -1;
	//}

	int e;
	dsa_key key;
	/* TODO: parameters */
	if((e = dsa_make_key(&prng, find_prng("fortuna"), 20, 128, &key)) != CRYPT_OK) {
		errx(1, "dsa_make_key: %s", error_to_string(e));
	}
}

int twz_cap_create(struct sccap **cap,
  objid_t target,
  objid_t accessor,
  uint32_t perms,
  struct screvoc *revoc,
  struct scgates *gates,
  uint16_t htype,
  uint16_t etype)
{
	if(htype != SCHASH_SHA1 || etype != SCENC_DSA)
		return -ENOTSUP;
	*cap = malloc(sizeof(**cap));
	(*cap)->target = target;
	(*cap)->accessor = accessor;
	(*cap)->rev = revoc ? *revoc : (struct screvoc){ 0 };
	(*cap)->gates = gates ? *gates : (struct scgates){ 0 };
	(*cap)->perms = perms;
	(*cap)->magic = SC_CAP_MAGIC;
	(*cap)->flags = 0;
	(*cap)->htype = htype;
	(*cap)->etype = etype;
	(*cap)->pad = 0;
	(*cap)->slen = 0;
	(*cap)->flags |= gates ? SCF_GATE : 0;
	(*cap)->flags |= revoc ? SCF_REV : 0;

	unsigned char sig[4096];
	size_t siglen = 0;
	unsigned char out[128];

	size_t keylen;
	char *keystart;
	while(siglen != (*cap)->slen || siglen == 0) {
		(*cap)->slen = siglen;
		_Alignas(16) hash_state hs;
		sha1_init(&hs);
		sha1_process(&hs, (unsigned char *)(*cap), sizeof(**cap));
		sha1_done(&hs, out);

		siglen = sizeof(sig);
		memset(sig, 0, siglen);
		sign_dsa(out, 20, (unsigned char *)keystart, keylen, sig, &siglen);
	}

	*cap = realloc(*cap, sizeof(**cap) + (*cap)->slen);
	memcpy((*cap) + 1, sig, (*cap)->slen);

	return 0;
}

void child(void)
{
	printf("Hello from child!\n");
}

int main(int argc, char **argv)
{
	twzobj context, pri, pub;

	if(twz_object_new(&context, NULL, NULL, TWZ_OC_DFL_READ | TWZ_OC_DFL_WRITE | TWZ_OC_DFL_USE)) {
		errx(1, "failed to make new object");
	}

	twz_sctx_init(&context, "test-context");

	struct sccap *cap;

	if(twz_object_new(&pri, NULL, NULL, TWZ_OC_DFL_READ | TWZ_OC_DFL_WRITE | TWZ_OC_DFL_USE)) {
		errx(1, "failed to make new object");
	}

	if(twz_object_new(&pub, NULL, NULL, TWZ_OC_DFL_READ | TWZ_OC_DFL_WRITE | TWZ_OC_DFL_USE)) {
		errx(1, "failed to make new object");
	}

	twz_key_new(&pri, &pub);

	//	twz_cap_create(&cap, 0, 0, 0, NULL, NULL, SCHASH_SHA1, SCENC_DSA);

	if(!fork()) {
		child();
		exit(0);
	}

	return 0;
}