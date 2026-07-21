#include <encoded_sizes.h>
#include <string.h>
#include <verification.h>

int
main(void)
{
    unsigned char public_key[PUBLICKEY_BYTES] = { 0 };
    unsigned char signature[SIGNATURE_BYTES] = { 0 };
    public_key_t decoded_public_key = { 0 };
    signature_t decoded_signature;

    if (public_key_from_bytes(&decoded_public_key, public_key) !=
        public_key + PUBLICKEY_BYTES) {
        return 1;
    }
    if (!signature_from_bytes(&decoded_signature, signature)) {
        return 1;
    }

    /* Length bytes are attacker controlled.  Reject them before any shift or
     * isogeny computation, including the former UINT8_MAX shift/DoS case. */
    decoded_signature.backtracking = UINT8_MAX;
    if (protocols_verify(&decoded_signature, &decoded_public_key, NULL, 0)) {
        return 1;
    }
    decoded_signature.backtracking = 0;
    decoded_signature.two_resp_length = UINT8_MAX;
    if (protocols_verify(&decoded_signature, &decoded_public_key, NULL, 0)) {
        return 1;
    }

    memset(public_key, 0xff, FP2_ENCODED_BYTES);
    if (public_key_from_bytes(&decoded_public_key, public_key) != NULL) {
        return 1;
    }

    memset(signature, 0xff, FP2_ENCODED_BYTES);
    if (signature_from_bytes(&decoded_signature, signature)) {
        return 1;
    }

    return 0;
}
