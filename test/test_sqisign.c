// SPDX-License-Identifier: Apache-2.0

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <api.h>
#include <bench_test_arguments.h>
#include <rng.h>
#include <sig.h>
#ifdef TARGET_BIG_ENDIAN
#include <tutil.h>
#endif

#ifdef ENABLE_CT_TESTING
#include <valgrind/memcheck.h>
#endif

static void
usage(FILE *stream, const char *program)
{
    fprintf(stream, "Usage: %s [--seed=<seed>] [--msglen=<positive length>]\n", program);
}

static int
all_zero(const unsigned char *buf, size_t len)
{
    unsigned char acc = 0;

    for (size_t i = 0; i < len; i++) {
        acc |= buf[i];
    }
    return acc == 0;
}

static int
expect_open_rejection(unsigned char *output,
                      size_t output_capacity,
                      size_t expected_clear_len,
                      const unsigned char *sm,
                      unsigned long long smlen,
                      const unsigned char *pk)
{
    unsigned long long opened_len = UINT64_MAX;

    memset(output, 0xa5, output_capacity);
    if (sqisign_open(output, &opened_len, sm, smlen, pk) == 0) {
        return 0;
    }
    return opened_len == 0 && all_zero(output, expected_clear_len);
}

static int
test_sqisign(unsigned long long in_msglen)
{
    const size_t msg_size = (size_t)in_msglen;
    const size_t signed_size = CRYPTO_BYTES + msg_size;
    unsigned char *pk = malloc(CRYPTO_PUBLICKEYBYTES);
    unsigned char *sk = malloc(CRYPTO_SECRETKEYBYTES);
    unsigned char *sm = malloc(signed_size);
    unsigned char *msg = malloc(msg_size);
    unsigned char *opened = malloc(signed_size);
    unsigned char *work = malloc(signed_size);
    unsigned long long smlen = 0;
    unsigned long long opened_len = 0;
    int result = 1;

#define FAIL(step)                                  \
    do {                                            \
        fprintf(stderr, "FAIL: %s\n", (step));    \
        goto cleanup;                               \
    } while (0)

    if (pk == NULL || sk == NULL || sm == NULL || msg == NULL || opened == NULL || work == NULL) {
        FAIL("allocation");
    }
    if (randombytes(msg, in_msglen) != 0) {
        FAIL("message randomness");
    }

#ifdef ENABLE_CT_TESTING
    VALGRIND_MAKE_MEM_DEFINED(msg, msg_size);
#endif

    printf("Testing %s correctness (message length: %llu)\n", CRYPTO_ALGNAME, in_msglen);

    if (sqisign_keypair(pk, sk) != 0) {
        FAIL("key generation");
    }

#ifdef ENABLE_CT_TESTING
    VALGRIND_MAKE_MEM_DEFINED(pk, CRYPTO_PUBLICKEYBYTES);
    /* The public-key prefix of the serialized secret key is public data. */
    VALGRIND_MAKE_MEM_DEFINED(sk, CRYPTO_PUBLICKEYBYTES);
#endif

    if (memcmp(sk, pk, CRYPTO_PUBLICKEYBYTES) != 0) {
        FAIL("serialized secret-key public-key prefix");
    }

    if (sqisign_sign(sm, &smlen, msg, in_msglen, sk) != 0) {
        FAIL("signing");
    }

#ifdef ENABLE_CT_TESTING
    VALGRIND_MAKE_MEM_DEFINED(sm, smlen);
#endif

    if (smlen != (unsigned long long)signed_size) {
        FAIL("signed-message length");
    }
    if (memcmp(sm + CRYPTO_BYTES, msg, msg_size) != 0) {
        FAIL("signed-message layout");
    }

    if (sqisign_verify(msg, in_msglen, sm, CRYPTO_BYTES, pk) != 0) {
        FAIL("detached verification");
    }
    if (sqisign_verify(msg, in_msglen, sm, CRYPTO_BYTES - 1, pk) == 0 ||
        sqisign_verify(msg, in_msglen, sm, CRYPTO_BYTES + 1, pk) == 0) {
        FAIL("detached signature-length rejection");
    }

    memset(opened, 0xa5, signed_size);
    opened_len = UINT64_MAX;
    if (sqisign_open(opened, &opened_len, sm, smlen, pk) != 0 ||
        opened_len != in_msglen || memcmp(opened, msg, msg_size) != 0) {
        FAIL("normal open");
    }

    memcpy(work, sm, signed_size);
    opened_len = UINT64_MAX;
    if (sqisign_open(work, &opened_len, work, smlen, pk) != 0 ||
        opened_len != in_msglen || memcmp(work, msg, msg_size) != 0) {
        FAIL("in-place open");
    }

    memcpy(work, sm, signed_size);
    work[0] ^= 1;
    if (!expect_open_rejection(opened, signed_size, msg_size, work, smlen, pk)) {
        FAIL("signature-tamper rejection and clearing");
    }

    memcpy(work, sm, signed_size);
    work[CRYPTO_BYTES] ^= 1;
    if (!expect_open_rejection(opened, signed_size, msg_size, work, smlen, pk)) {
        FAIL("message-tamper rejection and clearing");
    }

    if (!expect_open_rejection(opened,
                               signed_size,
                               CRYPTO_BYTES - 1,
                               sm,
                               CRYPTO_BYTES - 1,
                               pk)) {
        FAIL("truncated signed-message rejection and clearing");
    }

    result = 0;
    printf("PASS: %s correctness\n", CRYPTO_ALGNAME);

cleanup:
    free(pk);
    free(sk);
    free(sm);
    free(msg);
    free(opened);
    free(work);
#undef FAIL
    return result;
}

int
main(int argc, char *argv[])
{
    uint32_t seed[12] = { 0 };
    unsigned long long msglen = 32;
    int seed_set = 0;
    int msglen_set = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            usage(stdout, argv[0]);
            return 0;
        }

        if (!seed_set && !parse_seed(argv[i], seed)) {
            seed_set = 1;
            continue;
        }

        if (strncmp(argv[i], "--msglen=", 9) == 0 && !msglen_set) {
            char *end = NULL;
            unsigned long long parsed;

            errno = 0;
            parsed = strtoull(argv[i] + 9, &end, 10);
            if (errno != 0 || end == argv[i] + 9 || *end != '\0' || parsed == 0 ||
                parsed > (unsigned long long)(SIZE_MAX - CRYPTO_BYTES)) {
                fprintf(stderr, "Invalid message length: %s\n", argv[i] + 9);
                usage(stderr, argv[0]);
                return 1;
            }
            msglen = parsed;
            msglen_set = 1;
            continue;
        }

        fprintf(stderr, "Unknown or duplicate argument: %s\n", argv[i]);
        usage(stderr, argv[0]);
        return 1;
    }

    if (!seed_set && randombytes_select((unsigned char *)seed, sizeof(seed)) != 0) {
        fprintf(stderr, "Unable to obtain a random seed\n");
        return 1;
    }

    print_seed(seed);

#if defined(TARGET_BIG_ENDIAN)
    for (int i = 0; i < 12; i++) {
        seed[i] = BSWAP32(seed[i]);
    }
#endif

    randombytes_init((unsigned char *)seed, NULL, 256);
    return test_sqisign(msglen);
}
