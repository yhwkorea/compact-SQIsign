// SPDX-License-Identifier: Apache-2.0 and Unknown

/*
NIST-developed software is provided by NIST as a public service. You may use, copy, and distribute copies of the software in any medium, provided that you keep intact this entire notice. You may improve, modify, and create derivative works of the software or any portion of the software, and you may copy and distribute such modifications or works. Modified works should carry a notice stating that you changed the software and should note the date and nature of any such change. Please explicitly acknowledge the National Institute of Standards and Technology as the source of the software.

NIST-developed software is expressly provided "AS IS." NIST MAKES NO WARRANTY OF ANY KIND, EXPRESS, IMPLIED, IN FACT, OR ARISING BY OPERATION OF LAW, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTY OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, NON-INFRINGEMENT, AND DATA ACCURACY. NIST NEITHER REPRESENTS NOR WARRANTS THAT THE OPERATION OF THE SOFTWARE WILL BE UNINTERRUPTED OR ERROR-FREE, OR THAT ANY DEFECTS WILL BE CORRECTED. NIST DOES NOT WARRANT OR MAKE ANY REPRESENTATIONS REGARDING THE USE OF THE SOFTWARE OR THE RESULTS THEREOF, INCLUDING BUT NOT LIMITED TO THE CORRECTNESS, ACCURACY, RELIABILITY, OR USEFULNESS OF THE SOFTWARE.

You are solely responsible for determining the appropriateness of using and distributing the software and you assume all risks associated with its use, including but not limited to the risks and costs of program errors, compliance with applicable laws, damage to or loss of data, programs or equipment, and the unavailability or interruption of operation. This software is not intended to be used in any situation where a failure could cause risk of injury or damage to property. The software developed by NIST employees is not subject to copyright protection within the United States.
*/

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rng.h>
#include <sig.h>
#include <api.h>

#define MAX_MARKER_LEN         50

#define KAT_SUCCESS             0
#define KAT_FILE_OPEN_ERROR    -1
#define KAT_DATA_ERROR         -3
#define KAT_CRYPTO_FAILURE     -4
#define KAT_VERIFICATION_ERROR -5

static int      FindMarker(FILE *infile, const char *marker);
static int      ReadHex(FILE *infile, unsigned char *A, int Length, char *str);
static int      HexDigit(int ch);
static int      test_sig_kat(const char *fn_rsp, int cnt, int expected_vectors, int replay);

static void usage(const char *program) {
    fprintf(stderr,
            "Usage: %s --file response.rsp --mode replay|verify-only "
            "--expected-vectors N [--count N]\n",
            program);
}

int main(int argc, char *argv[]) {
    const char *fn_rsp = NULL;
    const char *mode = NULL;
    int cnt = -1;
    int expected_vectors = -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            fn_rsp = argv[++i];
        } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            mode = argv[++i];
        } else if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) {
            char *end = NULL;
            long parsed;
            errno = 0;
            parsed = strtol(argv[++i], &end, 10);
            if (errno != 0 || end == argv[i] || *end != '\0' ||
                parsed < 0 || parsed > INT_MAX) {
                usage(argv[0]);
                return KAT_DATA_ERROR;
            }
            cnt = (int)parsed;
        } else if (strcmp(argv[i], "--expected-vectors") == 0 && i + 1 < argc) {
            char *end = NULL;
            long parsed;
            errno = 0;
            parsed = strtol(argv[++i], &end, 10);
            if (errno != 0 || end == argv[i] || *end != '\0' ||
                parsed < 1 || parsed > INT_MAX) {
                usage(argv[0]);
                return KAT_DATA_ERROR;
            }
            expected_vectors = (int)parsed;
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return KAT_SUCCESS;
        } else {
            usage(argv[0]);
            return KAT_DATA_ERROR;
        }
    }

    if (fn_rsp == NULL || mode == NULL || expected_vectors < 1 ||
        (strcmp(mode, "replay") != 0 && strcmp(mode, "verify-only") != 0)) {
        usage(argv[0]);
        return KAT_DATA_ERROR;
    }

#if !defined(ENABLE_SIGN)
    if (strcmp(mode, "replay") == 0) {
        fprintf(stderr, "replay mode requires ENABLE_SIGN\n");
        return KAT_DATA_ERROR;
    }
#endif

    return test_sig_kat(fn_rsp, cnt, expected_vectors,
                        strcmp(mode, "replay") == 0);
}

static int test_sig_kat(const char *fn_rsp, int cnt, int expected_vectors, int replay) {
#if defined(ENABLE_SIGN)
    unsigned char       seed[48];
    unsigned char       sk[CRYPTO_SECRETKEYBYTES];
    unsigned char       pk[CRYPTO_PUBLICKEYBYTES];
    unsigned char       sk_rsp[CRYPTO_SECRETKEYBYTES];
    unsigned char       *sm;
    unsigned long long  smlen;
#endif
    unsigned char       pk_rsp[CRYPTO_PUBLICKEYBYTES];
    unsigned char       *m, *m1, *sm_rsp;
    unsigned long long  mlen, smlen_rsp, mlen1;
    int                 count;
    int                 ret_val;
    int                 tested = 0;

    FILE                *fp_rsp;

    if ( (fp_rsp = fopen(fn_rsp, "r")) == NULL ) {
        printf("Couldn't open <%s> for read\n", fn_rsp);
        return KAT_FILE_OPEN_ERROR;
    }

    while (1) {
        if ( FindMarker(fp_rsp, "count = ") ) {
            if (fscanf(fp_rsp, "%d", &count) != 1) {
                return KAT_DATA_ERROR;
            }
        } else {
            if (ferror(fp_rsp)) {
                return KAT_DATA_ERROR;
            }
            break;
        }

        if (cnt == -1 && count != tested) {
            printf("ERROR: non-contiguous KAT count <%d> in <%s>\n", count, fn_rsp);
            return KAT_DATA_ERROR;
        }
        if (cnt != -1 && cnt != count)
            continue;
        tested++;

#if defined(ENABLE_SIGN)
        if (replay) {
            if ( !ReadHex(fp_rsp, seed, 48, "seed = ") ) {
                printf("ERROR: unable to read 'seed' from <%s>\n", fn_rsp);
                return KAT_DATA_ERROR;
            }

            randombytes_init(seed, NULL, 256);
        }
#endif

        if ( FindMarker(fp_rsp, "mlen = ") ) {
            if (fscanf(fp_rsp, "%llu", &mlen) != 1 ||
                mlen == 0 || mlen > INT_MAX) {
                return KAT_DATA_ERROR;
            }
        } else {
            printf("ERROR: unable to read 'mlen' from <%s>\n", fn_rsp);
            return KAT_DATA_ERROR;
        }

        m = (unsigned char *)calloc(mlen, sizeof(unsigned char));
        m1 = (unsigned char *)calloc(mlen, sizeof(unsigned char));
#if defined(ENABLE_SIGN)
        sm = (unsigned char *)calloc(mlen + CRYPTO_BYTES, sizeof(unsigned char));
#endif
        if (m == NULL || m1 == NULL
#if defined(ENABLE_SIGN)
            || sm == NULL
#endif
        ) {
            return KAT_DATA_ERROR;
        }

        if ( !ReadHex(fp_rsp, m, (int)mlen, "msg = ") ) {
            printf("ERROR: unable to read 'msg' from <%s>\n", fn_rsp);
            return KAT_DATA_ERROR;
        }

#if defined(ENABLE_SIGN)
        if (replay) {
            // Generate the public/private keypair
            if ( (ret_val = sqisign_keypair(pk, sk)) != 0) {
                printf("crypto_sign_keypair returned <%d>\n", ret_val);
                return KAT_CRYPTO_FAILURE;
            }
        }
#endif

        if ( !ReadHex(fp_rsp, pk_rsp, CRYPTO_PUBLICKEYBYTES, "pk = ") ) {
            printf("ERROR: unable to read 'pk' from <%s>\n", fn_rsp);
            return KAT_DATA_ERROR;
        }

#if defined(ENABLE_SIGN)
        if (replay) {
            if ( !ReadHex(fp_rsp, sk_rsp, CRYPTO_SECRETKEYBYTES, "sk = ") ) {
                printf("ERROR: unable to read 'sk' from <%s>\n", fn_rsp);
                return KAT_DATA_ERROR;
            }

            if (memcmp(pk, pk_rsp, CRYPTO_PUBLICKEYBYTES) != 0) {
                printf("ERROR: pk is different from <%s>\n", fn_rsp);
                return KAT_VERIFICATION_ERROR;
            }

            if (memcmp(sk, sk_rsp, CRYPTO_SECRETKEYBYTES) != 0) {
                printf("ERROR: sk is different from <%s>\n", fn_rsp);
                return KAT_VERIFICATION_ERROR;
            }

            if ( (ret_val = sqisign_sign(sm, &smlen, m, mlen, sk)) != 0) {
                printf("crypto_sign returned <%d>\n", ret_val);
                return KAT_CRYPTO_FAILURE;
            }
        }
#endif

        if ( FindMarker(fp_rsp, "smlen = ") ) {
            if (fscanf(fp_rsp, "%llu", &smlen_rsp) != 1 ||
                smlen_rsp != mlen + CRYPTO_BYTES || smlen_rsp > INT_MAX) {
                return KAT_DATA_ERROR;
            }
        } else {
            printf("ERROR: unable to read 'smlen' from <%s>\n", fn_rsp);
            return KAT_DATA_ERROR;
        }
        sm_rsp = (unsigned char *)calloc(smlen_rsp, sizeof(unsigned char));
        if (sm_rsp == NULL) {
            return KAT_DATA_ERROR;
        }

        if ( !ReadHex(fp_rsp, sm_rsp, (int)smlen_rsp, "sm = ") ) {
            printf("ERROR: unable to read 'sm' from <%s>\n", fn_rsp);
            return KAT_DATA_ERROR;
        }

#if defined(ENABLE_SIGN)
        if (replay) {
            if (smlen != smlen_rsp || memcmp(sm, sm_rsp, smlen) != 0) {
                printf("ERROR: sm is different from <%s>\n", fn_rsp);
                return KAT_VERIFICATION_ERROR;
            }

            if ( (ret_val = sqisign_open(m1, &mlen1, sm, smlen, pk)) != 0) {
                printf("crypto_sign_open returned <%d>\n", ret_val);
                return KAT_CRYPTO_FAILURE;
            }
        } else
#endif
        {
            if ( (ret_val = sqisign_open(m1, &mlen1, sm_rsp, smlen_rsp, pk_rsp)) != 0 ) {
                printf("crypto_sign_open returned <%d>\n", ret_val);
                return KAT_CRYPTO_FAILURE;
            }
        }

        if ( mlen != mlen1 ) {
            printf("crypto_sign_open returned bad 'mlen': Got <%llu>, expected <%llu>\n", mlen1, mlen);
            return KAT_CRYPTO_FAILURE;
        }

        if ( memcmp(m, m1, mlen) ) {
            printf("crypto_sign_open returned bad 'm' value\n");
            return KAT_CRYPTO_FAILURE;
        }

        free(m);
        free(m1);
#if defined(ENABLE_SIGN)
        free(sm);
#endif
        free(sm_rsp);

    }

    fclose(fp_rsp);

    if (tested != expected_vectors) {
        printf("ERROR: found %d KAT vector%s in <%s>, expected %d\n",
               tested, tested == 1 ? "" : "s", fn_rsp, expected_vectors);
        return KAT_DATA_ERROR;
    }

    printf("Known Answer Tests PASSED (%s, %d vector%s).\n",
           replay ? "replay" : "verify-only", tested, tested == 1 ? "" : "s");
    printf("\n\n");

    return KAT_SUCCESS;
}


//
// ALLOW TO READ HEXADECIMAL ENTRY (KEYS, DATA, TEXT, etc.)
//
static int
FindMarker(FILE *infile, const char *marker) {
    char    line[MAX_MARKER_LEN];
    int     i, len;
    int curr_line;

    len = (int)strlen(marker);
    if ( len > MAX_MARKER_LEN - 1 ) {
        len = MAX_MARKER_LEN - 1;
    }

    for ( i = 0; i < len; i++ ) {
        curr_line = fgetc(infile);
        line[i] = curr_line;
        if (curr_line == EOF ) {
            return 0;
        }
    }
    line[len] = '\0';

    while ( 1 ) {
        if ( !strncmp(line, marker, len) ) {
            return 1;
        }

        for ( i = 0; i < len - 1; i++ ) {
            line[i] = line[i + 1];
        }
        curr_line = fgetc(infile);
        line[len - 1] = curr_line;
        if (curr_line == EOF ) {
            return 0;
        }
        line[len] = '\0';
    }

    // shouldn't get here
    return 0;
}

//
// ALLOW TO READ HEXADECIMAL ENTRY (KEYS, DATA, TEXT, etc.)
//
static int
ReadHex(FILE *infile, unsigned char *A, int Length, char *str) {
    int ch;

    if (Length <= 0 || !FindMarker(infile, str)) {
        return 0;
    }

    for (int i = 0; i < Length; i++) {
        int high = HexDigit(fgetc(infile));
        int low = HexDigit(fgetc(infile));
        if (high < 0 || low < 0) {
            return 0;
        }
        A[i] = (unsigned char)((high << 4) | low);
    }

    ch = fgetc(infile);
    if (ch == '\r') {
        ch = fgetc(infile);
    }
    return ch == '\n';
}

static int
HexDigit(int ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    return -1;
}
