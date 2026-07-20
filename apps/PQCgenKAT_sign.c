// SPDX-License-Identifier: Apache-2.0 and Unknown

/*
NIST-developed software is provided by NIST as a public service. You may use,
copy, and distribute copies of the software in any medium, provided that you
keep intact this entire notice. You may improve, modify, and create derivative
works of the software or any portion of the software, and you may copy and
distribute such modifications or works. Modified works should carry a notice
stating that you changed the software and should note the date and nature of any
such change. Please explicitly acknowledge the National Institute of Standards
and Technology as the source of the software.

NIST-developed software is expressly provided "AS IS." NIST MAKES NO WARRANTY OF
ANY KIND, EXPRESS, IMPLIED, IN FACT, OR ARISING BY OPERATION OF LAW, INCLUDING,
WITHOUT LIMITATION, THE IMPLIED WARRANTY OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE, NON-INFRINGEMENT, AND DATA ACCURACY. NIST NEITHER REPRESENTS
NOR WARRANTS THAT THE OPERATION OF THE SOFTWARE WILL BE UNINTERRUPTED OR
ERROR-FREE, OR THAT ANY DEFECTS WILL BE CORRECTED. NIST DOES NOT WARRANT OR MAKE
ANY REPRESENTATIONS REGARDING THE USE OF THE SOFTWARE OR THE RESULTS THEREOF,
INCLUDING BUT NOT LIMITED TO THE CORRECTNESS, ACCURACY, RELIABILITY, OR
USEFULNESS OF THE SOFTWARE.

You are solely responsible for determining the appropriateness of using and
distributing the software and you assume all risks associated with its use,
including but not limited to the risks and costs of program errors, compliance
with applicable laws, damage to or loss of data, programs or equipment, and the
unavailability or interruption of operation. This software is not intended to be
used in any situation where a failure could cause risk of injury or damage to
property. The software developed by NIST employees is not subject to copyright
protection within the United States.
*/

#include "api.h"
#include "rng.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MARKER_LEN 50

#define KAT_SUCCESS 0
#define KAT_FILE_OPEN_ERROR -1
#define KAT_DATA_ERROR -3
#define KAT_CRYPTO_FAILURE -4
#define KAT_MAX_VECTORS 100
#define KAT_PATH_MAX 4096

int FindMarker(FILE *infile, const char *marker);
int ReadHex(FILE *infile, unsigned char *A, int Length, char *str);
int HexDigit(int ch);
void fprintBstr(FILE *fp, char *S, unsigned char *A, unsigned long long L);

static void usage(const char *program) {
  fprintf(stderr,
          "Usage: %s [--vectors 1..%d] [--output-dir directory]\n",
          program, KAT_MAX_VECTORS);
}

static int parse_vector_count(const char *value, int *vectors) {
  char *end = NULL;
  long parsed;

  errno = 0;
  parsed = strtol(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' || parsed < 1 ||
      parsed > KAT_MAX_VECTORS) {
    return 0;
  }
  *vectors = (int)parsed;
  return 1;
}

int main(int argc, char **argv) {
  char fn_req[KAT_PATH_MAX], fn_rsp[KAT_PATH_MAX];
  FILE *fp_req, *fp_rsp;
  unsigned char seed[48];
  unsigned char msg[3300];
  unsigned char entropy_input[48];
  unsigned char *m, *sm, *m1;
  unsigned long long mlen, smlen, mlen1;
  int count;
  int done;
  int processed = 0;
  unsigned char pk[CRYPTO_PUBLICKEYBYTES], sk[CRYPTO_SECRETKEYBYTES];
  int ret_val;
  int vectors = KAT_MAX_VECTORS;
  const char *output_dir = ".";

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--vectors") == 0 && i + 1 < argc) {
      if (!parse_vector_count(argv[++i], &vectors)) {
        usage(argv[0]);
        return KAT_DATA_ERROR;
      }
    } else if (strcmp(argv[i], "--output-dir") == 0 && i + 1 < argc) {
      output_dir = argv[++i];
    } else if (strcmp(argv[i], "--help") == 0) {
      usage(argv[0]);
      return KAT_SUCCESS;
    } else {
      usage(argv[0]);
      return KAT_DATA_ERROR;
    }
  }

  // Create the REQUEST file
  ret_val = snprintf(fn_req, sizeof(fn_req), "%s/PQCsignKAT_%d_%s.req",
                     output_dir, CRYPTO_SECRETKEYBYTES, CRYPTO_ALGNAME);
  if (ret_val < 0 || (size_t)ret_val >= sizeof(fn_req)) {
    fprintf(stderr, "KAT request path is too long\n");
    return KAT_FILE_OPEN_ERROR;
  }
  if ((fp_req = fopen(fn_req, "w")) == NULL) {
    printf("Couldn't open <%s> for write\n", fn_req);
    return KAT_FILE_OPEN_ERROR;
  }
  ret_val = snprintf(fn_rsp, sizeof(fn_rsp), "%s/PQCsignKAT_%d_%s.rsp",
                     output_dir, CRYPTO_SECRETKEYBYTES, CRYPTO_ALGNAME);
  if (ret_val < 0 || (size_t)ret_val >= sizeof(fn_rsp)) {
    fprintf(stderr, "KAT response path is too long\n");
    fclose(fp_req);
    return KAT_FILE_OPEN_ERROR;
  }
  if ((fp_rsp = fopen(fn_rsp, "w")) == NULL) {
    printf("Couldn't open <%s> for write\n", fn_rsp);
    fclose(fp_req);
    return KAT_FILE_OPEN_ERROR;
  }

  for (int i = 0; i < 48; i++)
    entropy_input[i] = i;

  randombytes_init(entropy_input, NULL, 256);
  for (int i = 0; i < vectors; i++) {
    fprintf(fp_req, "count = %d\n", i);
    if (randombytes(seed, 48) != 0) {
      fclose(fp_req);
      fclose(fp_rsp);
      return KAT_CRYPTO_FAILURE;
    }
    fprintBstr(fp_req, "seed = ", seed, 48);
    mlen = 33 * (i + 1);
    fprintf(fp_req, "mlen = %llu\n", mlen);
    if (randombytes(msg, mlen) != 0) {
      fclose(fp_req);
      fclose(fp_rsp);
      return KAT_CRYPTO_FAILURE;
    }
    fprintBstr(fp_req, "msg = ", msg, mlen);
    fprintf(fp_req, "pk =\n");
    fprintf(fp_req, "sk =\n");
    fprintf(fp_req, "smlen =\n");
    fprintf(fp_req, "sm =\n");
    if (i + 1 < vectors) {
      fprintf(fp_req, "\n");
    }
  }
  ret_val = ferror(fp_req);
  if (fclose(fp_req) != 0 || ret_val) {
    printf("Couldn't finish writing <%s>\n", fn_req);
    fclose(fp_rsp);
    return KAT_FILE_OPEN_ERROR;
  }

  // Create the RESPONSE file based on what's in the REQUEST file
  if ((fp_req = fopen(fn_req, "r")) == NULL) {
    printf("Couldn't open <%s> for read\n", fn_req);
    fclose(fp_rsp);
    return KAT_FILE_OPEN_ERROR;
  }

  fprintf(fp_rsp, "# %s\n\n", CRYPTO_ALGNAME);
  done = 0;
  do {
    if (FindMarker(fp_req, "count = ")) {
      if (fscanf(fp_req, "%d", &count) != 1 || count != processed)
        return KAT_DATA_ERROR;
    } else {
      done = 1;
      break;
    }
    fprintf(fp_rsp, "count = %d\n", count);

    if (!ReadHex(fp_req, seed, 48, "seed = ")) {
      printf("ERROR: unable to read 'seed' from <%s>\n", fn_req);
      return KAT_DATA_ERROR;
    }
    fprintBstr(fp_rsp, "seed = ", seed, 48);

    randombytes_init(seed, NULL, 256);

    if (FindMarker(fp_req, "mlen = ")) {
      if (fscanf(fp_req, "%llu", &mlen) != 1 || mlen == 0 ||
          mlen > sizeof(msg))
        return KAT_DATA_ERROR;
    } else {
      printf("ERROR: unable to read 'mlen' from <%s>\n", fn_req);
      return KAT_DATA_ERROR;
    }
    fprintf(fp_rsp, "mlen = %llu\n", mlen);

    m = (unsigned char *)calloc(mlen, sizeof(unsigned char));
    m1 = (unsigned char *)calloc(mlen + CRYPTO_BYTES, sizeof(unsigned char));
    sm = (unsigned char *)calloc(mlen + CRYPTO_BYTES, sizeof(unsigned char));
    if (m == NULL || m1 == NULL || sm == NULL) {
      free(m);
      free(m1);
      free(sm);
      return KAT_CRYPTO_FAILURE;
    }

    if (!ReadHex(fp_req, m, (int)mlen, "msg = ")) {
      printf("ERROR: unable to read 'msg' from <%s>\n", fn_req);
      return KAT_DATA_ERROR;
    }
    fprintBstr(fp_rsp, "msg = ", m, mlen);

    // Generate the public/private keypair
    if ((ret_val = crypto_sign_keypair(pk, sk)) != 0) {
      printf("crypto_sign_keypair returned <%d>\n", ret_val);
      return KAT_CRYPTO_FAILURE;
    }
    fprintBstr(fp_rsp, "pk = ", pk, CRYPTO_PUBLICKEYBYTES);
    fprintBstr(fp_rsp, "sk = ", sk, CRYPTO_SECRETKEYBYTES);

    if ((ret_val = crypto_sign(sm, &smlen, m, mlen, sk)) != 0) {
      printf("crypto_sign returned <%d>\n", ret_val);
      return KAT_CRYPTO_FAILURE;
    }
    fprintf(fp_rsp, "smlen = %llu\n", smlen);
    fprintBstr(fp_rsp, "sm = ", sm, smlen);
    if (count + 1 < vectors) {
      fprintf(fp_rsp, "\n");
    }

    if ((ret_val = crypto_sign_open(m1, &mlen1, sm, smlen, pk)) != 0) {
      printf("crypto_sign_open returned <%d>\n", ret_val);
      return KAT_CRYPTO_FAILURE;
    }

    if (mlen != mlen1) {
      printf(
          "crypto_sign_open returned bad 'mlen': Got <%llu>, expected <%llu>\n",
          mlen1, mlen);
      return KAT_CRYPTO_FAILURE;
    }

    if (memcmp(m, m1, mlen)) {
      printf("crypto_sign_open returned bad 'm' value\n");
      return KAT_CRYPTO_FAILURE;
    }

    free(m);
    free(m1);
    free(sm);
    processed++;

  } while (!done);

  if (processed != vectors || ferror(fp_req)) {
    fclose(fp_req);
    fclose(fp_rsp);
    return KAT_DATA_ERROR;
  }

  int req_read_error = ferror(fp_req);
  int rsp_write_error = ferror(fp_rsp);
  int req_close_error = fclose(fp_req);
  int rsp_close_error = fclose(fp_rsp);
  if (req_read_error || rsp_write_error || req_close_error != 0 ||
      rsp_close_error != 0) {
    printf("Couldn't finish KAT file generation\n");
    return KAT_FILE_OPEN_ERROR;
  }

  return KAT_SUCCESS;
}

//
// ALLOW TO READ HEXADECIMAL ENTRY (KEYS, DATA, TEXT, etc.)
//
int FindMarker(FILE *infile, const char *marker) {
  char line[MAX_MARKER_LEN];
  int i, len;
  int curr_line;

  len = (int)strlen(marker);
  if (len > MAX_MARKER_LEN - 1)
    len = MAX_MARKER_LEN - 1;

  for (i = 0; i < len; i++) {
    curr_line = fgetc(infile);
    line[i] = curr_line;
    if (curr_line == EOF)
      return 0;
  }
  line[len] = '\0';

  while (1) {
    if (!strncmp(line, marker, len))
      return 1;

    for (i = 0; i < len - 1; i++)
      line[i] = line[i + 1];
    curr_line = fgetc(infile);
    line[len - 1] = curr_line;
    if (curr_line == EOF)
      return 0;
    line[len] = '\0';
  }

  // shouldn't get here
  return 0;
}

//
// ALLOW TO READ HEXADECIMAL ENTRY (KEYS, DATA, TEXT, etc.)
//
int ReadHex(FILE *infile, unsigned char *A, int Length, char *str) {
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

int HexDigit(int ch) {
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

void fprintBstr(FILE *fp, char *S, unsigned char *A, unsigned long long L) {
  unsigned long long i;

  fprintf(fp, "%s", S);

  for (i = 0; i < L; i++)
    fprintf(fp, "%02X", A[i]);

  if (L == 0)
    fprintf(fp, "00");

  fprintf(fp, "\n");
}
