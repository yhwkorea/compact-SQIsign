#include <stdio.h>
#include <inttypes.h>
#include <locale.h>
#include <string.h>
#include <time.h>

#include <verification.h>
#include <signature.h>
#include <encoded_sizes.h>
#include <torsion_constants.h>

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
        
        if (!protocols_keygen(&pk, &sk)) {
            printf("keygen failed ! \n");
            res = 0;
            continue;
        }
        printf("-----keygen done------\n");

        // Secret-key serialization must reject values that do not fit their
        // fixed-width field even in Release builds, and must not leave a
        // partially encoded key behind.
        {
            unsigned char encoded_sk[SECRETKEY_BYTES];
            ibz_t saved_norm;
            ibz_init(&saved_norm);
            ibz_copy(&saved_norm, &sk.secret_ideal.norm);
            ibz_pow(&sk.secret_ideal.norm, &ibz_const_two, 8 * FP_ENCODED_BYTES);
            memset(encoded_sk, 0xa5, sizeof(encoded_sk));
            if (secret_key_to_bytes(encoded_sk, &sk, &pk)) {
                printf("oversized secret-key component accepted ! \n");
                res = 0;
            }
            for (size_t j = 0; j < sizeof(encoded_sk); ++j) {
                if (encoded_sk[j] != 0) {
                    printf("failed secret-key encoding was not cleared ! \n");
                    res = 0;
                    break;
                }
            }
            ibz_copy(&sk.secret_ideal.norm, &saved_norm);
            ibz_finalize(&saved_norm);

            /* The matrix is modulo 2^TORSION_EVEN_POWER.  Its byte encoding
             * has unused high bits at every parameter set; those bits must
             * not provide alternate encodings of the same secret key. */
            {
                secret_key_t decoded_sk;
                public_key_t decoded_pk;
                const size_t matrix_offset =
                    SECRETKEY_BYTES - 4 * TORSION_2POWER_BYTES;
                secret_key_init(&decoded_sk);
                public_key_init(&decoded_pk);
                if (!secret_key_to_bytes(encoded_sk, &sk, &pk)) {
                    printf("valid secret-key encoding failed ! \n");
                    res = 0;
                } else {
                    encoded_sk[matrix_offset + TORSION_2POWER_BYTES - 1] |= 0x80;
                    if (secret_key_from_bytes(&decoded_sk, &decoded_pk, encoded_sk)) {
                        printf("non-canonical secret-key matrix encoding accepted ! \n");
                        res = 0;
                    }
                }
                public_key_finalize(&decoded_pk);
                secret_key_finalize(&decoded_sk);
            }

            // Flipping hint_A selects the construction with the wrong
            // quadratic character. Secret-key decoding must propagate the
            // canonical-basis reconstruction failure.
            if (!fp2_is_zero(&pk.curve.A)) {
                secret_key_t decoded_sk;
                public_key_t decoded_pk;
                secret_key_init(&decoded_sk);
                public_key_init(&decoded_pk);
                if (!secret_key_to_bytes(encoded_sk, &sk, &pk)) {
                    printf("valid secret-key encoding failed ! \n");
                    res = 0;
                } else {
                    encoded_sk[PUBLICKEY_BYTES - 1] ^= 1;
                    if (secret_key_from_bytes(&decoded_sk, &decoded_pk, encoded_sk)) {
                        printf("invalid secret-key basis hint accepted ! \n");
                        res = 0;
                    }
                }
                public_key_finalize(&decoded_pk);
                secret_key_finalize(&decoded_sk);
            }
        }

        if (!protocols_sign(&sig, &pk, &sk, msg, 32)) {
            printf("sign failed ! \n");
            res = 0;
            continue;
        }
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
