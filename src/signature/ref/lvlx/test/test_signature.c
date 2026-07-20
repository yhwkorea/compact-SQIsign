#include <stdio.h>
#include <inttypes.h>
#include <locale.h>
#include <time.h>

#include <verification.h>
#include <signature.h>

#include <tools.h>
#include <rng.h>
#include <bench_test_arguments.h>

#if defined(SQISIGN_ML2_PROFILE)
#include "lll_internals.h"

static void
print_ml2_profile_dimension(int d, const quat_ml2_profile_dimension_t *profile)
{
    printf("ML2_PROFILE,d=%d,inputs=%" PRIu64
           ",precision_rejected=%" PRIu64
           ",first_failures=%" PRIu64
           ",recovered_1=%" PRIu64
           ",recovered_2=%" PRIu64
           ",recovered_3=%" PRIu64
           ",exhausted=%" PRIu64
           ",underlying_attempts=%" PRIu64 "\n",
           d,
           profile->inputs,
           profile->precision_rejected,
           profile->first_attempt_failures,
           profile->recovered[0],
           profile->recovered[1],
           profile->recovered[2],
           profile->exhausted,
           profile->underlying_attempts);
}
#endif

int
test_sqisign(int repeat)
{
    int res = 1;

    public_key_t pk;
    secret_key_t sk;
    signature_t sig;
    const unsigned char msg[32] = { 0 };

    public_key_init(&pk);
    secret_key_init(&sk);

    printf("\n\nTesting signatures\n");
    for (int i = 0; i < repeat; ++i) {
        printf("#%d \n", i);
        
        protocols_keygen(&pk, &sk);
        printf("-----keygen done------\n");
        protocols_sign(&sig, &pk, &sk, msg, 32);
        printf("-----sign done------\n");
        int check = protocols_verify(&sig, &pk, msg, 32);
        if (!check) {
            printf("verif failed ! \n");
            res = 0;
        }
        printf("-----verif done------\n");
    }

    public_key_finalize(&pk);
    secret_key_finalize(&sk);

    return res;
}

// run all tests in module
int
main(int argc, char *argv[])
{
    uint32_t seed[12] = { 0 };
    int iterations = SQISIGN_TEST_REPS;
    int help = 0;
    int seed_set = 0;
    int res;

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
    }

    if (help || iterations <= 0) {
        printf("Usage: %s [--iterations=<iterations>] [--seed=<seed>]\n", argv[0]);
        printf("Where <iterations> is the number of iterations used for testing; if not "
               "present, uses the default: %d)\n",
               iterations);
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

#if defined(SQISIGN_ML2_PROFILE)
    quat_ml2_profile_reset();
#endif

    res = test_sqisign(iterations);

#if defined(SQISIGN_ML2_PROFILE)
    {
        quat_ml2_profile_t profile;
        quat_ml2_profile_get(&profile);
        print_ml2_profile_dimension(4, &profile.d4);
        print_ml2_profile_dimension(8, &profile.d8);
        print_ml2_profile_dimension(16, &profile.d16);
    }
#endif

    if (!res) {
        printf("\nSome tests failed!\n");
    } else {
        printf("All tests passed!\n");
    }
    return (!res);
}
