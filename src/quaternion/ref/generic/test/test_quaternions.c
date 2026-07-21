#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "quaternion_tests.h"
#include <rng.h>
#include <bench_test_arguments.h>

int quat_test_ml2_stress(uint32_t samples);

// run all tests in module
int
main(int argc, char *argv[])
{
    uint32_t seed[12] = { 0 };
    int help = 0;
    int seed_set = 0;
    int res = 0;
    uint32_t ml2_stress_samples = 0;
    int ml2_correctness_only = 0;

    for (int i = 1; i < argc; i++) {
        if (!help && strcmp(argv[i], "--help") == 0) {
            help = 1;
            continue;
        }

        if (strncmp(argv[i], "--ml2-stress=", 13) == 0) {
            char *end = NULL;
            errno = 0;
            unsigned long parsed = strtoul(argv[i] + 13, &end, 10);
            if (errno != 0 || end == argv[i] + 13 || *end != '\0' ||
                parsed == 0 || parsed > 1000000UL) {
                fprintf(stderr,
                        "Invalid --ml2-stress sample count (expected 1..1000000)\n");
                return 1;
            }
            ml2_stress_samples = (uint32_t)parsed;
            continue;
        }

        if (strcmp(argv[i], "--ml2-correctness-only") == 0) {
            ml2_correctness_only = 1;
            continue;
        }

        if (!seed_set && !parse_seed(argv[i], seed)) {
            seed_set = 1;
            continue;
        }
    }

    if (help) {
        printf("Usage: %s [--seed=<seed>]\n", argv[0]);
        printf("Where <seed> is the random seed to be used; if not present, a random seed is "
               "generated\n");
        printf("       %s --ml2-stress=<samples-per-d>\n", argv[0]);
        printf("Runs the opt-in deterministic d=4/8/16 ML2 retry-rate measurement.\n");
        printf("       %s --ml2-correctness-only\n", argv[0]);
        printf("Runs only deterministic ML2/MLLL correctness regressions.\n");
        return 1;
    }

    if (ml2_stress_samples != 0)
        return quat_test_ml2_stress(ml2_stress_samples);

    if (!seed_set) {
        randombytes_select((unsigned char *)seed, sizeof(seed));
    }

    print_seed(seed);

#if defined(TARGET_BIG_ENDIAN)
    for (int i = 0; i < 12; i++) {
        seed[i] = BSWAP32(seed[i]);
    }
#endif

    printf("Running quaternion module unit tests\n");

    if (ml2_correctness_only)
        return quat_test_ml2_correctness();

    res = res | ibz_test_mul_sqrt();
    res = res | ibz_test_rand_interval_endpoints();
    res = res | ibz_test_intbig();
    // // res = res | mini_gmp_test();
    // res = res | quat_test_finit();
    res = res | quat_test_dim4();
    res = res | quat_test_dim2();
    res = res | quat_test_integers();

    // res = res | quat_test_hnf();
    res = res | quat_test_algebra();
    res = res | quat_test_lattice();
    // res = res | quat_test_lll();
    res = res | quat_test_mlll();
    res = res | quat_test_ml2_correctness();
    res = res | quat_test_lideal();
    res = res | quat_test_normeq();
    res = res | quat_test_lat_ball();
    // res = res | quat_test_with_randomization();
    if (res != 0) {
        printf("\nSome tests failed!\n");
    }
    return (res);
}
