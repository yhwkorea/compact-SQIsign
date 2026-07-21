#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <verification.h>
#include <signature.h>
#include <rng.h>
#include <bench_test_arguments.h>

#include <pthread.h>

int threads = 4;
int iterations = SQISIGN_TEST_REPS;

char dummy = 0;

void *
test_sqisign(void *_)
{
    (void)_;

    bool res = 1;

    public_key_t pk;
    secret_key_t sk;
    signature_t sig;
    unsigned char msg[32] = { 0 };

    public_key_init(&pk);
    secret_key_init(&sk);

    for (int i = 0; i < iterations; ++i) {
        if (!protocols_keygen(&pk, &sk) ||
            !protocols_sign(&sig, &pk, &sk, msg, sizeof(msg) / sizeof(*msg)) ||
            !protocols_verify(&sig, &pk, msg, sizeof(msg) / sizeof(*msg))) {
            res = false;
            break;
        }
    }

    public_key_finalize(&pk);
    secret_key_finalize(&sk);

    return res ? &dummy : NULL;
}

int
main(int argc, char *argv[])
{
    uint32_t seed[12] = { 0 };
    int help = 0;
    int seed_set = 0;

    for (int i = 1; i < argc; i++) {
        if (!help && strcmp(argv[i], "--help") == 0) {
            help = 1;
            continue;
        }

        if (!seed_set && !parse_seed(argv[i], seed)) {
            seed_set = 1;
            continue;
        }

        if (sscanf(argv[i], "--iterations=%d", &iterations) == 1) {
            continue;
        }

        if (sscanf(argv[i], "--threads=%d", &threads) == 1) {
            continue;
        }
    }

    if (help || iterations <= 0 || threads <= 0) {
        printf("Usage: %s [--iterations=<iterations>] [--threads=<threads>] [--seed=<seed>]\n", argv[0]);
        printf("Where <iterations> is the number of iterations used for testing; if not "
               "present, uses the default: %d)\n",
               iterations);
        printf("Where <threads> is the number of threads used for testing; if not "
               "present, uses the default: %d)\n",
               threads);
        printf("Where <seed> is the random seed to be used; if not present, a random seed is "
               "generated\n");
        return 1;
    }

    if (!seed_set) {
        randombytes_select((unsigned char *)seed, sizeof(seed));
    }

    print_seed(seed);

#if defined(TARGET_BIG_ENDIAN)
    for (int i = 0; i < 12; i++) {
        seed[i] = BSWAP32(seed[i]);
    }
#endif

    randombytes_init((unsigned char *)seed, NULL, 256);

    pthread_t *thread_handles = calloc((size_t)threads, sizeof(*thread_handles));
    if (thread_handles == NULL) {
        fprintf(stderr, "unable to allocate thread handles\n");
        return 1;
    }

    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0) {
        fprintf(stderr, "unable to configure worker stack\n");
        free(thread_handles);
        return 1;
    }
    if (pthread_attr_setstacksize(&attr, 8u * 1024u * 1024u) != 0) {
        fprintf(stderr, "unable to configure worker stack\n");
        pthread_attr_destroy(&attr);
        free(thread_handles);
        return 1;
    }

    /* The implementation currently requires the documented enlarged stack. */
    int created = 0;
    for (; created < threads; ++created) {
        if (pthread_create(&thread_handles[created], &attr, &test_sqisign, NULL) != 0) {
            fprintf(stderr, "pthread_create failed at worker %d\n", created);
            break;
        }
    }
    pthread_attr_destroy(&attr);

    bool ok = created == threads;
    for (int i = 0; i < created; ++i) {
        void *worker_result = NULL;
        if (pthread_join(thread_handles[i], &worker_result) != 0) {
            fprintf(stderr, "pthread_join failed at worker %d\n", i);
            ok = false;
        } else {
            ok = ok && worker_result != NULL;
        }
    }
    free(thread_handles);

    if (!ok) {
        printf("\nSome tests failed!\n");
    } else {
        printf("All tests passed!\n");
    }
    return !ok;
}
