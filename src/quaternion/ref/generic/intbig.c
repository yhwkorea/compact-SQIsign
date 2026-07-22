/*
 * Fixed-precision integer arithmetic implementation
 * Using 2's complement representation for signed integers
 * Uses the per-variant limb budgets selected in intbig.h.
 */

#include "intbig.h"
#include <stdlib.h>

#if defined(SQISIGN_INTBIG_OVERFLOW_CHECK)
static void
ibz_overflow_abort(const char *operation)
{
    fprintf(stderr,
            "[INTBIG-OVERFLOW] %s exceeds %d-bit signed capacity\n",
            operation,
            IBZ_BITS);
    abort();
}
#endif

static void
ibz_division_by_zero_abort(const char *operation)
{
    fprintf(stderr, "[INTBIG] %s by zero\n", operation);
    abort();
}

static void
ibz_invalid_argument_abort(const char *operation)
{
    fprintf(stderr, "[INTBIG] invalid argument: %s\n", operation);
    abort();
}

const uint64_t ibz_const_zero[IBZ_LIMBS] = { 0 };
const uint64_t ibz_const_one[IBZ_LIMBS] = { 1 };
const uint64_t ibz_const_two[IBZ_LIMBS] = { 2 };
const uint64_t ibz_const_three[IBZ_LIMBS] = { 3 };

#define KARATSUBA_THRESHOLD 32

// Helper macros
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

// Bit manipulation helpers
static inline int
clz64(uint64_t x)
{
    if (x == 0)
        return 64;
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_clzll(x);
#else
    int n = 0;
    if (x <= 0x00000000FFFFFFFFULL) {
        n += 32;
        x <<= 32;
    }
    if (x <= 0x0000FFFFFFFFFFFFULL) {
        n += 16;
        x <<= 16;
    }
    if (x <= 0x00FFFFFFFFFFFFFFULL) {
        n += 8;
        x <<= 8;
    }
    if (x <= 0x0FFFFFFFFFFFFFFFULL) {
        n += 4;
        x <<= 4;
    }
    if (x <= 0x3FFFFFFFFFFFFFFFULL) {
        n += 2;
        x <<= 2;
    }
    if (x <= 0x7FFFFFFFFFFFFFFFULL) {
        n += 1;
    }
    return n;
#endif
}

static inline int
ctz64(uint64_t x)
{
    if (x == 0)
        return 64;
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctzll(x);
#else
    int n = 0;
    if ((x & 0x00000000FFFFFFFFULL) == 0) {
        n += 32;
        x >>= 32;
    }
    if ((x & 0x000000000000FFFFULL) == 0) {
        n += 16;
        x >>= 16;
    }
    if ((x & 0x00000000000000FFULL) == 0) {
        n += 8;
        x >>= 8;
    }
    if ((x & 0x000000000000000FULL) == 0) {
        n += 4;
        x >>= 4;
    }
    if ((x & 0x0000000000000003ULL) == 0) {
        n += 2;
        x >>= 2;
    }
    if ((x & 0x0000000000000001ULL) == 0) {
        n += 1;
    }
    return n;
#endif
}

// 64x64 -> 128 bit multiplication, with a portable 32-bit-half fallback
static void
mul64_128(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo)
{
#if defined(HAVE_UINT128)
    __uint128_t product = (__uint128_t)a * b;
    *lo = (uint64_t)product;
    *hi = (uint64_t)(product >> 64);
#else
    uint32_t a_lo = (uint32_t)a;
    uint32_t a_hi = (uint32_t)(a >> 32);
    uint32_t b_lo = (uint32_t)b;
    uint32_t b_hi = (uint32_t)(b >> 32);

    uint64_t p00 = (uint64_t)a_lo * b_lo;
    uint64_t p01 = (uint64_t)a_lo * b_hi;
    uint64_t p10 = (uint64_t)a_hi * b_lo;
    uint64_t p11 = (uint64_t)a_hi * b_hi;

    uint64_t middle = p01 + p10;
    uint64_t carry = (middle < p01) ? (1ULL << 32) : 0;

    *lo = p00 + (middle << 32);
    carry += (*lo < p00) ? 1 : 0;
    *hi = p11 + (middle >> 32) + carry;
#endif
}

static size_t
ibz_used_limbs(const uint64_t *value, size_t capacity)
{
    while (capacity > 0 && value[capacity - 1] == 0)
        --capacity;
    return capacity;
}

/* Full unsigned product.  The output capacity is exactly twice the fixed
 * precision, which is also enough for two maximum-width magnitudes. */
static void
ibz_mul_unsigned_wide(uint64_t product[2 * IBZ_LIMBS],
                      const uint64_t *a,
                      size_t a_size,
                      const uint64_t *b,
                      size_t b_size)
{
    memset(product, 0, 2 * IBZ_LIMBS * sizeof(*product));

    for (size_t i = 0; i < a_size; ++i) {
        if (a[i] == 0)
            continue;

#if defined(HAVE_UINT128)
        uint64_t carry = 0;
        for (size_t j = 0; j < b_size; ++j) {
            const size_t index = i + j;
            __uint128_t accumulator = (__uint128_t)a[i] * b[j] +
                                      product[index] + carry;
            product[index] = (uint64_t)accumulator;
            carry = (uint64_t)(accumulator >> 64);
        }

        size_t index = i + b_size;
        while (carry != 0 && index < 2 * IBZ_LIMBS) {
            __uint128_t accumulator = (__uint128_t)product[index] + carry;
            product[index] = (uint64_t)accumulator;
            carry = (uint64_t)(accumulator >> 64);
            ++index;
        }
#else
        for (size_t j = 0; j < b_size; ++j) {
            if (b[j] == 0)
                continue;

            const size_t index = i + j;
            uint64_t hi, lo;
            mul64_128(a[i], b[j], &hi, &lo);

            uint64_t old = product[index];
            product[index] = old + lo;
            uint64_t carry = product[index] < old;

            size_t k = index + 1;
            if (k < 2 * IBZ_LIMBS) {
                old = product[k];
                product[k] = old + hi;
                uint64_t next_carry = product[k] < old;
                old = product[k];
                product[k] = old + carry;
                next_carry |= product[k] < old;
                carry = next_carry;
                ++k;

                while (carry != 0 && k < 2 * IBZ_LIMBS) {
                    old = product[k];
                    product[k] = old + 1;
                    carry = product[k] == 0;
                    ++k;
                }
            }
        }
#endif
    }
}

#if defined(HAVE_UINT128)
/* Knuth-style normalized long division in base 2^64.  The dividend may be a
 * regular fixed-width value or a full 2N-limb multiplication result.  Only
 * the remainder is required by wide modular multiplication, so quotient may
 * be NULL. */
static void
ibz_divrem_unsigned_wide(uint64_t quotient[2 * IBZ_LIMBS],
                         uint64_t remainder[IBZ_LIMBS],
                         const uint64_t *dividend,
                         size_t dividend_capacity,
                         const uint64_t divisor[IBZ_LIMBS])
{
    uint64_t u[2 * IBZ_LIMBS + 1];
    uint64_t v[IBZ_LIMBS];
    const size_t dividend_size = ibz_used_limbs(dividend, dividend_capacity);
    const size_t divisor_size = ibz_used_limbs(divisor, IBZ_LIMBS);

    if (divisor_size == 0)
        ibz_division_by_zero_abort("division");

    if (quotient != NULL)
        memset(quotient, 0, 2 * IBZ_LIMBS * sizeof(*quotient));
    memset(remainder, 0, IBZ_LIMBS * sizeof(*remainder));

    if (dividend_size == 0)
        return;
    if (dividend_size < divisor_size) {
        memcpy(remainder, dividend, dividend_size * sizeof(*remainder));
        return;
    }

    if (divisor_size == 1) {
        uint64_t carry = 0;
        for (size_t i = dividend_size; i-- > 0;) {
            __uint128_t numerator = ((__uint128_t)carry << 64) | dividend[i];
            if (quotient != NULL)
                quotient[i] = (uint64_t)(numerator / divisor[0]);
            carry = (uint64_t)(numerator % divisor[0]);
        }
        remainder[0] = carry;
        return;
    }

    memset(u, 0, sizeof(u));
    memset(v, 0, sizeof(v));

    const unsigned int normalization = (unsigned int)clz64(divisor[divisor_size - 1]);
    if (normalization == 0) {
        memcpy(v, divisor, divisor_size * sizeof(*v));
        memcpy(u, dividend, dividend_size * sizeof(*u));
    } else {
        uint64_t carry = 0;
        for (size_t i = 0; i < divisor_size; ++i) {
            const uint64_t limb = divisor[i];
            v[i] = (limb << normalization) | carry;
            carry = limb >> (64 - normalization);
        }

        carry = 0;
        for (size_t i = 0; i < dividend_size; ++i) {
            const uint64_t limb = dividend[i];
            u[i] = (limb << normalization) | carry;
            carry = limb >> (64 - normalization);
        }
        u[dividend_size] = carry;
    }

    const size_t quotient_size = dividend_size - divisor_size + 1;
    const uint64_t divisor_top = v[divisor_size - 1];
    for (size_t position = quotient_size; position-- > 0;) {
        const size_t top = position + divisor_size;
        uint64_t qhat;
        uint64_t rhat;
        int rhat_overflow = 0;

        if (u[top] >= divisor_top) {
            qhat = UINT64_MAX;
            __uint128_t sum = (__uint128_t)u[top - 1] + divisor_top;
            rhat = (uint64_t)sum;
            rhat_overflow = (int)(sum >> 64);
        } else {
            __uint128_t numerator = ((__uint128_t)u[top] << 64) | u[top - 1];
            qhat = (uint64_t)(numerator / divisor_top);
            rhat = (uint64_t)(numerator % divisor_top);
        }

        while (!rhat_overflow &&
               (__uint128_t)qhat * v[divisor_size - 2] >
                   ((__uint128_t)rhat << 64) + u[top - 2]) {
            --qhat;
            __uint128_t sum = (__uint128_t)rhat + divisor_top;
            rhat = (uint64_t)sum;
            rhat_overflow = (int)(sum >> 64);
        }

        uint64_t borrow = 0;
        for (size_t i = 0; i < divisor_size; ++i) {
            __uint128_t subtrahend = (__uint128_t)qhat * v[i] + borrow;
            const uint64_t low = (uint64_t)subtrahend;
            const uint64_t high = (uint64_t)(subtrahend >> 64);
            const uint64_t old = u[position + i];
            u[position + i] = old - low;
            borrow = high + (old < low);
        }

        const uint64_t old_top = u[top];
        u[top] = old_top - borrow;
        if (old_top < borrow) {
            --qhat;
            uint64_t carry = 0;
            for (size_t i = 0; i < divisor_size; ++i) {
                __uint128_t sum = (__uint128_t)u[position + i] + v[i] + carry;
                u[position + i] = (uint64_t)sum;
                carry = (uint64_t)(sum >> 64);
            }
            u[top] += carry;
        }

        if (quotient != NULL)
            quotient[position] = qhat;
    }

    if (normalization == 0) {
        memcpy(remainder, u, divisor_size * sizeof(*remainder));
    } else {
        for (size_t i = 0; i < divisor_size; ++i)
            remainder[i] = (u[i] >> normalization) |
                           (u[i + 1] << (64 - normalization));
    }
}
#endif

// Initialize/finalize
void
ibz_init(ibz_t *x)
{
    memset(*x, 0, sizeof(ibz_t));
}

void
ibz_finalize(ibz_t *x)
{
    memset(*x, 0, sizeof(ibz_t));
}

// Copy and swap
void
ibz_copy(ibz_t *target, const ibz_t *value)
{
    memcpy(*target, *value, sizeof(ibz_t));
}

void
ibz_swap(ibz_t *a, ibz_t *b)
{
    uint64_t tmp[IBZ_LIMBS];
    memcpy(tmp, *a, sizeof(ibz_t));
    memcpy(*a, *b, sizeof(ibz_t));
    memcpy(*b, tmp, sizeof(ibz_t));
}

// Check if negative (2's complement)
int
ibz_is_negative(const ibz_t *x)
{
    return ((*x)[IBZ_LIMBS - 1] >> 63) & 1;
}

// Raw two's-complement negation. Multiplication also uses this on an unsigned
// magnitude before assigning its sign, so overflow policy belongs in the
// public signed wrapper below.
static void
ibz_neg_raw(ibz_t *neg, const ibz_t *a)
{
    uint64_t carry = 1;
    for (int i = 0; i < IBZ_LIMBS; i++) {
        uint64_t tmp = ~(*a)[i];
        (*neg)[i] = tmp + carry;
        carry = ((*neg)[i] < tmp) ? 1 : 0;
    }
}

/*
 * Several internal algorithms need an unsigned magnitude.  In particular,
 * the magnitude of the most-negative signed value is 2^(IBZ_BITS-1), which
 * has no positive ibz_t representation.  Keep that value as an unsigned limb
 * array instead of feeding it to the signed comparison/arithmetic helpers.
 */
static void
ibz_abs_unsigned(ibz_t *magnitude, const ibz_t *a)
{
    if (ibz_is_negative(a))
        ibz_neg_raw(magnitude, a);
    else
        ibz_copy(magnitude, a);
}

static int
ibz_cmp_unsigned(const ibz_t *a, const ibz_t *b)
{
    for (int i = IBZ_LIMBS - 1; i >= 0; --i) {
        if ((*a)[i] > (*b)[i])
            return 1;
        if ((*a)[i] < (*b)[i])
            return -1;
    }
    return 0;
}

static int
ibz_is_min_value(const ibz_t *a)
{
    if ((*a)[IBZ_LIMBS - 1] != (UINT64_C(1) << 63))
        return 0;
    for (int i = 0; i < IBZ_LIMBS - 1; ++i)
        if ((*a)[i] != 0)
            return 0;
    return 1;
}

static int
ibz_bitsize_unsigned(const ibz_t *a)
{
    for (int i = IBZ_LIMBS - 1; i >= 0; --i) {
        if ((*a)[i] != 0)
            return i * 64 + (64 - clz64((*a)[i]));
    }
    return 0;
}

static void
ibz_sub_unsigned(ibz_t *difference, const ibz_t *a, const ibz_t *b)
{
    uint64_t borrow = 0;
    for (int i = 0; i < IBZ_LIMBS; ++i) {
        uint64_t ai = (*a)[i];
        uint64_t first = ai - borrow;
        uint64_t first_borrow = first > ai;
        uint64_t second = first - (*b)[i];
        uint64_t second_borrow = second > first;
        (*difference)[i] = second;
        borrow = first_borrow | second_borrow;
    }
}

static void
ibz_shift_right_unsigned(ibz_t *result, const ibz_t *a, uint32_t shift)
{
    ibz_t input;
    ibz_copy(&input, a);
    ibz_init(result);

    if (shift >= IBZ_BITS)
        return;

    const uint32_t limb_shift = shift / 64;
    const uint32_t bit_shift = shift % 64;
    for (uint32_t i = 0; i + limb_shift < IBZ_LIMBS; ++i) {
        uint64_t value = input[i + limb_shift] >> bit_shift;
        if (bit_shift != 0 && i + limb_shift + 1 < IBZ_LIMBS)
            value |= input[i + limb_shift + 1] << (64 - bit_shift);
        (*result)[i] = value;
    }
}

static void
ibz_shift_left_unsigned(ibz_t *result, const ibz_t *a, uint32_t shift)
{
    ibz_t input;
    ibz_copy(&input, a);
    ibz_init(result);

    if (shift >= IBZ_BITS)
        return;

    const uint32_t limb_shift = shift / 64;
    const uint32_t bit_shift = shift % 64;
    for (int i = IBZ_LIMBS - 1; i >= (int)limb_shift; --i) {
        uint64_t value = input[i - limb_shift] << bit_shift;
        if (bit_shift != 0 && i > (int)limb_shift)
            value |= input[i - limb_shift - 1] >> (64 - bit_shift);
        (*result)[i] = value;
    }
}

/* Unsigned fixed-width division.  Targets with 128-bit intermediates use
 * normalized base-2^64 long division; the portable fallback uses aligned
 * shift/subtract.  Both paths publish outputs only after consuming inputs, so
 * every output/input aliasing pattern remains valid. */
static void
ibz_div_unsigned(ibz_t *quotient,
                 ibz_t *remainder,
                 const ibz_t *dividend,
                 const ibz_t *divisor)
{
#if defined(HAVE_UINT128)
    uint64_t q_wide[2 * IBZ_LIMBS];
    uint64_t r_words[IBZ_LIMBS];
    ibz_divrem_unsigned_wide(quotient != NULL ? q_wide : NULL,
                             r_words,
                             *dividend,
                             IBZ_LIMBS,
                             *divisor);
    if (quotient != NULL)
        memcpy(*quotient, q_wide, sizeof(ibz_t));
    if (remainder != NULL)
        memcpy(*remainder, r_words, sizeof(ibz_t));
#else
    ibz_t dividend_input, divisor_input, shifted_divisor, q, r;
    ibz_copy(&dividend_input, dividend);
    ibz_copy(&divisor_input, divisor);
    ibz_init(&q);
    ibz_copy(&r, &dividend_input);

    const int dividend_bits = ibz_bitsize_unsigned(&dividend_input);
    const int divisor_bits = ibz_bitsize_unsigned(&divisor_input);
    if (dividend_bits >= divisor_bits) {
        const int shift = dividend_bits - divisor_bits;
        ibz_shift_left_unsigned(&shifted_divisor, &divisor_input, (uint32_t)shift);
        for (int bit = shift; bit >= 0; --bit) {
            if (ibz_cmp_unsigned(&r, &shifted_divisor) >= 0) {
                ibz_sub_unsigned(&r, &r, &shifted_divisor);
                q[bit / 64] |= UINT64_C(1) << (bit % 64);
            }
            if (bit != 0)
                ibz_shift_right_unsigned(&shifted_divisor, &shifted_divisor, 1);
        }
    }

    if (quotient)
        ibz_copy(quotient, &q);
    if (remainder)
        ibz_copy(remainder, &r);
#endif
}

static uint32_t
ibz_div_unsigned_small(ibz_t *quotient, const ibz_t *dividend, uint32_t divisor)
{
    ibz_t q;
    uint64_t remainder = 0;
    ibz_init(&q);

    for (int i = IBZ_LIMBS - 1; i >= 0; --i) {
        uint64_t hi, lo;
        /* remainder < divisor <= UINT32_MAX, so each shifted half fits u64. */
        hi = (remainder << 32) | ((*dividend)[i] >> 32);
        q[i] = (hi / divisor) << 32;
        remainder = hi % divisor;
        lo = (remainder << 32) | ((*dividend)[i] & UINT64_C(0xffffffff));
        q[i] |= lo / divisor;
        remainder = lo % divisor;
    }

    ibz_copy(quotient, &q);
    return (uint32_t)remainder;
}

static uint32_t
ibz_mod_unsigned_small(const ibz_t *dividend, uint32_t divisor)
{
    uint64_t remainder = 0;
    size_t size = ibz_used_limbs(*dividend, IBZ_LIMBS);

    while (size-- > 0) {
        uint64_t half = (remainder << 32) | ((*dividend)[size] >> 32);
        remainder = half % divisor;
        half = (remainder << 32) |
               ((*dividend)[size] & UINT64_C(0xffffffff));
        remainder = half % divisor;
    }
    return (uint32_t)remainder;
}

// Negation (2's complement)
void
ibz_neg(ibz_t *neg, const ibz_t *a)
{
#if defined(SQISIGN_INTBIG_OVERFLOW_CHECK)
    int input_negative = ibz_is_negative(a);
#endif
    ibz_neg_raw(neg, a);
#if defined(SQISIGN_INTBIG_OVERFLOW_CHECK)
    /* The only negative input whose two's-complement negation is still
     * negative is -2^(IBZ_BITS-1), which has no positive counterpart. */
    if (input_negative && ibz_is_negative(neg))
        ibz_overflow_abort("negation");
#endif
}

// Absolute value
void
ibz_abs(ibz_t *abs, const ibz_t *a)
{
    /* The magnitude of INTBIG_MIN is not representable as a positive ibz_t.
     * Preserve its magnitude bit pattern (and therefore INTBIG_MIN itself),
     * just as fixed-width two's-complement absolute-value instructions do. */
    ibz_abs_unsigned(abs, a);
}

// Addition
void
ibz_add(ibz_t *sum, const ibz_t *a, const ibz_t *b)
{
#if defined(SQISIGN_INTBIG_OVERFLOW_CHECK)
    int a_negative = ibz_is_negative(a);
    int b_negative = ibz_is_negative(b);
#endif
    uint64_t carry = 0;
    for (int i = 0; i < IBZ_LIMBS; i++) {
        uint64_t tmp = (*a)[i] + carry;
        carry = (tmp < (*a)[i]) ? 1 : 0;
        (*sum)[i] = tmp + (*b)[i];
        carry += ((*sum)[i] < tmp) ? 1 : 0;
    }
#if defined(SQISIGN_INTBIG_OVERFLOW_CHECK)
    if (a_negative == b_negative && ibz_is_negative(sum) != a_negative)
        ibz_overflow_abort("addition");
#endif
}

// Subtraction
void
ibz_sub(ibz_t *diff, const ibz_t *a, const ibz_t *b)
{
#if defined(SQISIGN_INTBIG_OVERFLOW_CHECK)
    int a_negative = ibz_is_negative(a);
    int b_negative = ibz_is_negative(b);
#endif
    uint64_t borrow = 0;
    for (int i = 0; i < IBZ_LIMBS; i++) {
        uint64_t tmp = (*a)[i] - borrow;
        borrow = (tmp > (*a)[i]) ? 1 : 0;
        uint64_t tmp2 = tmp - (*b)[i];
        borrow += (tmp2 > tmp) ? 1 : 0;
        (*diff)[i] = tmp2;
    }
#if defined(SQISIGN_INTBIG_OVERFLOW_CHECK)
    if (a_negative != b_negative && ibz_is_negative(diff) != a_negative)
        ibz_overflow_abort("subtraction");
#endif
}

// static void
// ibz_mul_karatsuba_internal(uint64_t *result,
//                            const uint64_t *a,
//                            int a_size,
//                            const uint64_t *b,
//                            int b_size,
//                            uint64_t *workspace)
// {
//     // Base case: use schoolbook for small sizes
//     if (a_size < KARATSUBA_THRESHOLD || b_size < KARATSUBA_THRESHOLD) {
//         // Schoolbook multiplication
//         memset(result, 0, (a_size + b_size) * sizeof(uint64_t));

//         for (int i = 0; i < a_size; i++) {
//             if (a[i] == 0)
//                 continue;

//             uint64_t carry = 0;
//             for (int j = 0; j < b_size; j++) {
//                 uint64_t hi, lo;
//                 mul64_128(a[i], b[j], &hi, &lo);

//                 // Add lo to result[i+j]
//                 result[i + j] += lo;
//                 uint64_t c1 = (result[i + j] < lo) ? 1 : 0;

//                 // Add previous carry
//                 result[i + j] += carry;
//                 c1 += (result[i + j] < carry) ? 1 : 0;

//                 // New carry = hi + c1
//                 carry = hi + c1;
//             }
//             if (carry && (i + b_size < a_size + b_size)) {
//                 result[i + b_size] = carry;
//             }
//         }
//         return;
//     }

//     // Karatsuba: split numbers in half
//     int m = MAX(a_size, b_size) / 2;

//     // a = a1*B^m + a0
//     // b = b1*B^m + b0
//     // result = a1*b1*B^(2m) + ((a1+a0)*(b1+b0) - a1*b1 - a0*b0)*B^m + a0*b0

//     int a0_size = MIN(m, a_size);
//     int a1_size = a_size > m ? a_size - m : 0;
//     int b0_size = MIN(m, b_size);
//     int b1_size = b_size > m ? b_size - m : 0;

//     // Recursive calls for a0*b0 and a1*b1
//     ibz_mul_karatsuba_internal(workspace, a, a0_size, b, b0_size, workspace + 2 * (a_size + b_size));

//     if (a1_size > 0 && b1_size > 0) {
//         ibz_mul_karatsuba_internal(
//             workspace + 2 * m, a + m, a1_size, b + m, b1_size, workspace + 2 * (a_size + b_size));
//     }

//     // Copy results to final location
//     memcpy(result, workspace, (a_size + b_size) * sizeof(uint64_t));
// }

// // OPTIMIZED: Multiplication with Karatsuba for large numbers
// void
// ibz_mul(ibz_t *prod, const ibz_t *a, const ibz_t *b)
// {
//     // Find actual sizes
//     int a_size = IBZ_LIMBS;
//     int b_size = IBZ_LIMBS;

//     while (a_size > 1 && (*a)[a_size - 1] == 0)
//         a_size--;
//     while (b_size > 1 && (*b)[b_size - 1] == 0)
//         b_size--;

//     // // OPTIMIZATION: Use Karatsuba for large multiplications
//     // if (a_size > KARATSUBA_THRESHOLD && b_size > KARATSUBA_THRESHOLD) {
//     //     static uint64_t workspace[6 * IBZ_LIMBS]; // Working space for Karatsuba
//     //     static uint64_t temp_result[2 * IBZ_LIMBS];

//     //     memset(temp_result, 0, sizeof(temp_result));
//     //     ibz_mul_karatsuba_internal(temp_result, *a, a_size, *b, b_size, workspace);

//     //     // Copy result
//     //     for (int i = 0; i < IBZ_LIMBS; i++) {
//     //         (*prod)[i] = temp_result[i];
//     //     }
//     //     return;
//     // }

//     // Fallback to schoolbook for smaller numbers
//     static uint64_t temp_result[2 * IBZ_LIMBS];
//     memset(temp_result, 0, sizeof(temp_result));

//     for (int i = 0; i < a_size; i++) {
//         uint64_t a_limb = (*a)[i];
//         if (a_limb == 0)
//             continue;

//         for (int j = 0; j < b_size; j++) {
//             uint64_t b_limb = (*b)[j];
//             if (b_limb == 0)
//                 continue;

//             if (i + j >= 2 * IBZ_LIMBS)
//                 continue;

//             uint64_t hi, lo;
//             mul64_128(a_limb, b_limb, &hi, &lo);

//             int k = i + j;
//             temp_result[k] += lo;
//             uint64_t carry = (temp_result[k] < lo) ? 1 : 0;

//             k++;
//             if (k < 2 * IBZ_LIMBS) {
//                 temp_result[k] += hi;
//                 uint64_t c2 = (temp_result[k] < hi) ? 1 : 0;
//                 temp_result[k] += carry;
//                 c2 += (temp_result[k] < carry) ? 1 : 0;
//                 carry = c2;

//                 k++;
//                 while (carry && k < 2 * IBZ_LIMBS) {
//                     temp_result[k] += carry;
//                     carry = (temp_result[k] == 0) ? 1 : 0;
//                     k++;
//                 }
//             }
//         }
//     }

//     for (int i = 0; i < IBZ_LIMBS; i++) {
//         (*prod)[i] = temp_result[i];
//     }
// }

void
ibz_mul(ibz_t *prod, const ibz_t *a, const ibz_t *b)
{
    // alias-safe (handles prod==a or prod==b)
    ibz_t a_in, b_in;
    const ibz_t *A = a, *B = b;
    if (prod == a) { ibz_copy(&a_in, a); A = &a_in; }
    if (prod == b) { ibz_copy(&b_in, b); B = &b_in; }

    int a_neg = ibz_is_negative(A);
    int b_neg = ibz_is_negative(B);
    int neg = (a_neg != b_neg);

    ibz_t aa, bb;
    ibz_abs(&aa, A);
    ibz_abs(&bb, B);

    // Find actual sizes on |a|, |b|
    size_t a_size = IBZ_LIMBS;
    size_t b_size = IBZ_LIMBS;

    while (a_size > 1 && aa[a_size - 1] == 0) a_size--;
    while (b_size > 1 && bb[b_size - 1] == 0) b_size--;

    /* Per-call storage is required: a static buffer makes concurrent calls
     * race and corrupt otherwise independent multiplications. */
    uint64_t temp_result[2 * IBZ_LIMBS];
    ibz_mul_unsigned_wide(temp_result, aa, a_size, bb, b_size);

#if defined(SQISIGN_INTBIG_OVERFLOW_CHECK)
    {
        int overflow = 0;
        for (int i = IBZ_LIMBS; i < 2 * IBZ_LIMBS; i++)
            overflow |= temp_result[i] != 0;

        if (!neg) {
            overflow |= (temp_result[IBZ_LIMBS - 1] >> 63) != 0;
        } else if (temp_result[IBZ_LIMBS - 1] > (UINT64_C(1) << 63)) {
            overflow = 1;
        } else if (temp_result[IBZ_LIMBS - 1] == (UINT64_C(1) << 63)) {
            for (int i = 0; i < IBZ_LIMBS - 1; i++)
                overflow |= temp_result[i] != 0;
        }

        if (overflow)
            ibz_overflow_abort("multiplication");
    }
#endif

    // write low limbs
    for (int i = 0; i < IBZ_LIMBS; i++) {
        (*prod)[i] = temp_result[i];
    }

    // apply sign
    if (neg && !ibz_is_zero(prod)) {
        ibz_neg_raw(prod, prod);
    }
}

// void
// ibz_mul_2exp(ibz_t* result, const ibz_t* a, size_t shift)
// {
//     if (shift == 0) {
//         ibz_copy(result, a);
//         return;
//     }

//     size_t limb_shift = shift / 64;
//     size_t bit_shift = shift % 64;

//     ibz_init(result);

//     if (bit_shift == 0) {
//         // limb ���� ����Ʈ��
//         for (size_t i = 0; i < IBZ_LIMBS - limb_shift; i++) {
//             (*result)[i + limb_shift] = (*a)[i];
//         }
//     } else {
//         // limb + bit ����Ʈ
//         for (size_t i = 0; i < IBZ_LIMBS - limb_shift; i++) {
//             (*result)[i + limb_shift] |= (*a)[i] << bit_shift;
//             if (i + limb_shift + 1 < IBZ_LIMBS) {
//                 (*result)[i + limb_shift + 1] |= (*a)[i] >> (64 - bit_shift);
//             }
//         }
//     }
// }
void
ibz_mul_2exp(ibz_t *result, const ibz_t *a, size_t shift)
{
    // guard for in-place call: copy first if result == a
    ibz_t a_copy;
    const ibz_t *src = a;

    if (result == a) {
        ibz_copy(&a_copy, a);
        src = &a_copy;
    }

    if (shift == 0) {
        ibz_copy(result, src);
        return;
    }

    size_t limb_shift = shift / 64;
    size_t bit_shift  = shift % 64;

    ibz_init(result);

    if (limb_shift >= IBZ_LIMBS) {
        // out of range -> just zero
#if defined(SQISIGN_INTBIG_OVERFLOW_CHECK)
        if (!ibz_is_zero(src))
            ibz_overflow_abort("left shift");
#endif
        return;
    }

    if (bit_shift == 0) {
        // limb-level shift
        for (size_t i = 0; i < IBZ_LIMBS - limb_shift; i++) {
            (*result)[i + limb_shift] = (*src)[i];
        }
    } else {
        // limb + bit shift
        for (size_t i = 0; i < IBZ_LIMBS - limb_shift; i++) {
            (*result)[i + limb_shift] |= (*src)[i] << bit_shift;
            if (i + limb_shift + 1 < IBZ_LIMBS) {
                (*result)[i + limb_shift + 1] |= (*src)[i] >> (64 - bit_shift);
            }
        }
    }

#if defined(SQISIGN_INTBIG_OVERFLOW_CHECK)
    /* A signed left shift is exact iff shifting the result arithmetically
     * back by the same amount recovers the input. */
    {
        ibz_t roundtrip;
        ibz_init(&roundtrip);
        ibz_div_2exp(&roundtrip, result, (uint32_t)shift);
        if (ibz_cmp(&roundtrip, src) != 0)
            ibz_overflow_abort("left shift");
    }
#endif
}

// Division by power of 2
void
ibz_div_2exp(ibz_t *quotient, const ibz_t *a, uint32_t exp)
{
    ibz_t magnitude, shifted;
    const int negative = ibz_is_negative(a);

    /* Match truncating integer division, not an arithmetic right shift:
     * -3 / 2 is -1 and every finite value divided by 2^IBZ_BITS is zero. */
    ibz_abs_unsigned(&magnitude, a);
    ibz_shift_right_unsigned(&shifted, &magnitude, exp);
    if (negative && !ibz_is_zero(&shifted))
        ibz_neg_raw(&shifted, &shifted);
    ibz_copy(quotient, &shifted);
}

// Comparison
int
ibz_cmp(const ibz_t *a, const ibz_t *b)
{
    int a_neg = ibz_is_negative(a);
    int b_neg = ibz_is_negative(b);

    if (a_neg != b_neg) {
        return a_neg ? -1 : 1;
    }

    for (int i = IBZ_LIMBS - 1; i >= 0; i--) {
        if ((*a)[i] > (*b)[i])
            return 1;
        if ((*a)[i] < (*b)[i])
            return -1;
    }
    return 0;
}

// Basic predicates
int
ibz_is_zero(const ibz_t *x)
{
    for (int i = 0; i < IBZ_LIMBS; i++) {
        if ((*x)[i] != 0)
            return 0;
    }
    return 1;
}

int
ibz_is_one(const ibz_t *x)
{
    if ((*x)[0] != 1)
        return 0;
    for (int i = 1; i < IBZ_LIMBS; i++) {
        if ((*x)[i] != 0)
            return 0;
    }
    return 1;
}

int
ibz_is_even(const ibz_t *x)
{
    return ((*x)[0] & 1) == 0;
}

int
ibz_is_odd(const ibz_t *x)
{
    return ((*x)[0] & 1) == 1;
}

// Set from int32
void
ibz_set(ibz_t *i, int32_t x)
{
    if (x >= 0) {
        (*i)[0] = (uint64_t)x;
        memset(&(*i)[1], 0, (IBZ_LIMBS - 1) * sizeof(uint64_t));
    } else {
        (*i)[0] = (uint64_t)x;
        memset(&(*i)[1], 0xFF, (IBZ_LIMBS - 1) * sizeof(uint64_t));
    }
}

void
ibz_set_u64(ibz_t *i, uint64_t x)
{
    (*i)[0] = x;
    memset(&(*i)[1], 0, (IBZ_LIMBS - 1) * sizeof(uint64_t));
}

// int
// ibz_convert_to_str(const ibz_t *i, char *str, int base)
// {
//     if (!str || (base != 10 && base != 16))
//         return 0;

//     ibz_t abs_i, base_ibz, q, r;
//     ibz_abs(&abs_i, i);
//     ibz_set(&base_ibz, base);

//     char temp[4096];
//     int pos = 0;

//     if (ibz_is_zero(i)) {
//         str[0] = '0';
//         str[1] = '\0';
//         return 1;
//     }

//     ibz_copy(&q, &abs_i);
//     while (!ibz_is_zero(&q)) {
//         ibz_div(&q, &r, &q, &base_ibz);
//         int digit = (int)r[0];
//         temp[pos++] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
//     }

//     int offset = 0;
//     if (ibz_is_negative(i)) {
//         str[offset++] = '-';
//     }

//     for (int ind = 0; ind < pos; ind++) {
//         str[offset + ind] = temp[pos - 1 - ind];
//     }
//     str[offset + pos] = '\0';

//     return 1;
// }
int
ibz_convert_to_str(const ibz_t *i, char *str, int base)
{
    if (!str || (base != 10 && base != 16))
        return 0;

    ibz_t magnitude, q;
    ibz_abs_unsigned(&magnitude, i);

    /* Base 10 is the longest supported representation and always uses fewer
     * than IBZ_BITS digits.  Unlike the former fixed 4096-byte buffer, this
     * remains correct for every configured IBZ_LIMBS value. */
    char temp[IBZ_BITS + 1];
    size_t pos = 0;

    if (ibz_is_zero(i)) {
        str[0] = '0';
        str[1] = '\0';
        return 1;
    }

    ibz_copy(&q, &magnitude);
    while (!ibz_is_zero(&q)) {
        ibz_t q_next;
        int digit = (int)ibz_div_unsigned_small(&q_next, &q, (uint32_t)base);

        temp[pos++] = (digit < 10)
                          ? ('0' + digit)
                          : ('a' + digit - 10);

        ibz_copy(&q, &q_next);
    }

    size_t offset = 0;
    if (ibz_is_negative(i)) {
        str[offset++] = '-';
    }

    for (size_t ind = 0; ind < pos; ind++) {
        str[offset + ind] = temp[pos - 1 - ind];
    }
    str[offset + pos] = '\0';

    return 1;
}


void
ibz_print(const ibz_t *num, int base)
{
    // assert(base == 10 || base == 16);

    int num_size = ibz_size_in_base(num, base);
    char num_str[num_size + 3];
    ibz_convert_to_str(num, num_str, base);
    printf("%s", num_str);
}

int
ibz_set_from_str(ibz_t *i, const char *str, int base)
{
    if (!str || (base != 10 && base != 16))
        return 0;

    ibz_init(i);

    int is_negative = 0;
    int pos = 0;

    if (str[0] == '-') {
        is_negative = 1;
        pos = 1;
    } else if (str[0] == '+') {
        pos = 1;
    }

    if (str[pos] == '\0')
        return 0;

    ibz_t magnitude, limit;
    ibz_init(&magnitude);
    if (is_negative) {
        ibz_init(&limit);
        limit[IBZ_LIMBS - 1] = UINT64_C(1) << 63;
    } else {
        for (int limb = 0; limb < IBZ_LIMBS; ++limb)
            limit[limb] = UINT64_MAX;
        limit[IBZ_LIMBS - 1] >>= 1;
    }

    while (str[pos] != '\0') {
        char c = str[pos];
        int digit;

        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (base == 16 && c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else if (base == 16 && c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
        } else {
            return 0; // Invalid character
        }

        if (digit >= base) {
            return 0; // Invalid digit for base
        }

        /* magnitude = magnitude * base + digit, with an explicit unsigned
         * overflow check so parsing never wraps into a different value. */
        uint64_t carry = (uint64_t)digit;
        for (int limb = 0; limb < IBZ_LIMBS; ++limb) {
            uint64_t hi, lo;
            mul64_128(magnitude[limb], (uint64_t)base, &hi, &lo);
            uint64_t sum = lo + carry;
            uint64_t add_carry = sum < lo;
            magnitude[limb] = sum;
            if (hi > UINT64_MAX - add_carry) {
                ibz_init(i);
                return 0;
            }
            carry = hi + add_carry;
        }
        if (carry != 0 || ibz_cmp_unsigned(&magnitude, &limit) > 0) {
            ibz_init(i);
            return 0;
        }

        pos++;
    }

    if (is_negative && !ibz_is_zero(&magnitude))
        ibz_neg_raw(i, &magnitude);
    else
        ibz_copy(i, &magnitude);

    return 1;
}

// Get as int32
int32_t
ibz_get(const ibz_t *i)
{
    return (int32_t)((*i)[0] & 0xFFFFFFFF);
}

int
ibz_rand_interval(ibz_t *rand, const ibz_t *a, const ibz_t *b)
{
    const int order = ibz_cmp(a, b);
    if (order > 0) {
        ibz_init(rand);
        return 0;
    }
    if (order == 0) {
        ibz_copy(rand, a);
        return 1;
    }

    ibz_t range;
    if (ibz_is_negative(a) && !ibz_is_negative(b)) {
        ibz_t a_magnitude, signed_max, room;
        ibz_abs_unsigned(&a_magnitude, a);
        for (int limb = 0; limb < IBZ_LIMBS; ++limb)
            signed_max[limb] = UINT64_MAX;
        signed_max[IBZ_LIMBS - 1] >>= 1;
        ibz_sub_unsigned(&room, &signed_max, b);
        if (ibz_cmp_unsigned(&a_magnitude, &room) > 0) {
            ibz_init(rand);
            return 0;
        }
        ibz_add(&range, b, &a_magnitude);
    } else {
        ibz_sub(&range, b, a);
    }

    int len_bits = ibz_bitsize(&range);
    int len_bytes = (len_bits + 7) / 8;
    int len_limbs = (len_bytes + 7) / 8;

    uint64_t mask = (len_bits % 64 == 0) ? UINT64_MAX : ((1ULL << (len_bits % 64)) - 1);

    // Rejection sampling
    for (int tries = 0; tries < 1000; tries++) {
        unsigned char bytes[IBZ_LIMBS * sizeof(uint64_t)];
        ibz_init(rand);

        if (randombytes(bytes, (unsigned long long)len_bytes) != 0) {
            return 0;
        }

        /* Decode the entropy explicitly as a little-endian unsigned integer.
         * Casting rand to bytes made the partial most-significant limb and its
         * mask host-endian-dependent. */
        for (int byte = 0; byte < len_bytes; ++byte)
            (*rand)[byte / 8] |= (uint64_t)bytes[byte] << (8 * (byte % 8));

        if (len_limbs > 0 && len_limbs <= IBZ_LIMBS) {
            (*rand)[len_limbs - 1] &= mask;
        }

        /* range = b - a, so accepting range itself is required for the
         * documented inclusive interval [a, b]. */
        if (ibz_cmp(rand, &range) <= 0) {
            ibz_add(rand, rand, a);
            return 1;
        }
    }

    return 0;
}

int
ibz_rand_interval_i(ibz_t *rand, int32_t a, int32_t b)
{
    ibz_t a_ibz, b_ibz;
    ibz_set(&a_ibz, a);
    ibz_set(&b_ibz, b);
    return ibz_rand_interval(rand, &a_ibz, &b_ibz);
}

int
ibz_rand_interval_minm_m(ibz_t *rand, int32_t m)
{
    if (m < 0) {
        ibz_init(rand);
        return 0;
    }
    ibz_t m_big, neg_m;
    ibz_set(&m_big, m);
    ibz_neg(&neg_m, &m_big);
    return ibz_rand_interval(rand, &neg_m, &m_big);
}

int
ibz_rand_interval_bits(ibz_t *rand, uint32_t m)
{
    if (m >= IBZ_BITS - 1) {
        ibz_init(rand);
        return 0;
    }
    ibz_t max_val, min_val;
    ibz_set(&max_val, 1);
    ibz_mul_2exp(&max_val, &max_val, m);
    ibz_neg(&min_val, &max_val);
    return ibz_rand_interval(rand, &min_val, &max_val);
}

// Compare with int32
int
ibz_cmp_int32(const ibz_t *x, int32_t y)
{
    ibz_t tmp;
    ibz_set(&tmp, y);
    return ibz_cmp(x, &tmp);
}

// Get bit size (absolute value)
int
ibz_bitsize(const ibz_t *a)
{
    ibz_t magnitude;
    ibz_abs_unsigned(&magnitude, a);
    return ibz_bitsize_unsigned(&magnitude);
}

// Get size in base
size_t
ibz_size_in_base(const ibz_t *a, int base)
{
    if (base != 10 && base != 16)
        return 0;
    if (ibz_is_zero(a)) {
        return 1;
    }

    ibz_t temp;
    ibz_abs_unsigned(&temp, a);

    size_t count = 0;

    while (!ibz_is_zero(&temp)) {
        ibz_t q;
        (void)ibz_div_unsigned_small(&q, &temp, (uint32_t)base);
        ibz_copy(&temp, &q);
        count++;
    }

    return count > 0 ? count : 1;
}

size_t
ibz_digits_required(const ibz_t *a)
{
    ibz_t magnitude;
    ibz_abs_unsigned(&magnitude, a);
    for (int i = IBZ_LIMBS - 1; i >= 0; --i)
        if (magnitude[i] != 0)
            return (size_t)i + 1;
    return 1;
}

int
ibz_to_digits_checked(digit_t *target, size_t target_len, const ibz_t *a)
{
    if (target == NULL || target_len == 0 ||
        target_len > SIZE_MAX / sizeof(*target))
        return 0;

    ibz_t magnitude;
    ibz_abs_unsigned(&magnitude, a);
    const size_t required = ibz_digits_required(a);
    memset(target, 0, target_len * sizeof(*target));
    if (required > target_len)
        return 0;

    memcpy(target, magnitude, required * sizeof(*target));
    return 1;
}

// Convert the unsigned magnitude to a digit array.  The legacy entry point
// requires at least ibz_digits_required(a) output elements; new code should
// use ibz_to_digits_checked when the destination is externally sized.
void
ibz_to_digits(digit_t *target, const ibz_t *a)
{
    const size_t required = ibz_digits_required(a);
    (void)ibz_to_digits_checked(target, required, a);
}

// Copy from digit array
void
ibz_copy_digits(ibz_t *a, const digit_t *digits, size_t len)
{
    ibz_init(a);
    for (size_t i = 0; i < len && i < IBZ_LIMBS; i++) {
        (*a)[i] = digits[i];
    }
}

// Get 2-adic valuation (trailing zeros)
int
ibz_two_adic(ibz_t *pow)
{
    for (int i = 0; i < IBZ_LIMBS; i++) {
        if ((*pow)[i] != 0) {
            return i * 64 + ctz64((*pow)[i]);
        }
    }
    return IBZ_BITS;
}

// void
// ibz_div(ibz_t *quotient, ibz_t *remainder, const ibz_t *a, const ibz_t *b)
// {
//     if (ibz_is_zero(b)) {
//         ibz_init(quotient);
//         ibz_init(remainder);
//         return;
//     }

//     ibz_init(quotient);
//     ibz_init(remainder);

//     int a_neg = ibz_is_negative(a);
//     int b_neg = ibz_is_negative(b);
//     int quot_neg = (a_neg != b_neg);

//     ibz_t dividend, divisor;
//     ibz_abs(&dividend, a);
//     ibz_abs(&divisor, b);

//     if (ibz_cmp(&dividend, &divisor) < 0) {
//         ibz_copy(remainder, a);
//         return;
//     }

//     // OPTIMIZATION: Use word-by-word division for better performance
//     ibz_t q, r;
//     ibz_init(&q);
//     ibz_copy(&r, &dividend);

//     int divisor_bits = ibz_bitsize(&divisor);
//     int dividend_bits = ibz_bitsize(&dividend);

//     // Shift divisor to align with dividend
//     int shift = dividend_bits - divisor_bits;
//     ibz_t shifted_divisor;
//     ibz_mul_2exp(&shifted_divisor, &divisor, shift);

//     // Division loop
//     for (int i = shift; i >= 0; i--) {
//         if (ibz_cmp(&r, &shifted_divisor) >= 0) {
//             ibz_sub(&r, &r, &shifted_divisor);
//             q[i / 64] |= (1ULL << (i % 64));
//         }
//         if (i > 0) {
//             ibz_div_2exp(&shifted_divisor, &shifted_divisor, 1);
//         }
//     }

//     if (quot_neg && !ibz_is_zero(&q)) {
//         ibz_neg(&q, &q);
//     }

//     if (a_neg && !ibz_is_zero(&r)) {
//         ibz_neg(&r, &r);
//     }

//     ibz_copy(quotient, &q);
//     ibz_copy(remainder, &r);
// }

void
ibz_div(ibz_t *quotient, ibz_t *remainder, const ibz_t *a, const ibz_t *b)
{
    ibz_t q, r, dividend, divisor;
    ibz_t a_input, b_input;

    /* Keep both inputs intact until all output writes.  This also covers
     * quotient/remainder aliasing either input. */
    ibz_copy(&a_input, a);
    ibz_copy(&b_input, b);

    if (ibz_is_zero(&b_input))
        ibz_division_by_zero_abort("division");

    int a_neg   = ibz_is_negative(&a_input);
    int b_neg   = ibz_is_negative(&b_input);
    int quot_neg = (a_neg != b_neg);

    ibz_abs_unsigned(&dividend, &a_input);
    ibz_abs_unsigned(&divisor, &b_input);
    ibz_div_unsigned(&q, &r, &dividend, &divisor);

    if (quot_neg && !ibz_is_zero(&q))
        ibz_neg_raw(&q, &q);
    if (a_neg && !ibz_is_zero(&r))
        ibz_neg_raw(&r, &r);

#if defined(SQISIGN_INTBIG_OVERFLOW_CHECK)
    /* INTBIG_MIN / -1 is the sole division result outside the signed range. */
    if (!quot_neg && ibz_is_negative(&q))
        ibz_overflow_abort("division");
#endif

    // emit the result only at the end (alias-safe)
    if (quotient)  ibz_copy(quotient,  &q);
    if (remainder) ibz_copy(remainder, &r);
}


// Floor division
void
ibz_div_floor(ibz_t *q, ibz_t *r, const ibz_t *n, const ibz_t *d)
{
    ibz_t n_input, d_input, q_tmp, r_tmp, one;
    ibz_copy(&n_input, n);
    ibz_copy(&d_input, d);
    ibz_div(&q_tmp, &r_tmp, &n_input, &d_input);

    /* Truncating division differs from floor exactly when n/d is negative
     * and non-integral.  The floor remainder then has the divisor's sign. */
    if (!ibz_is_zero(&r_tmp) &&
        ibz_is_negative(&n_input) != ibz_is_negative(&d_input)) {
        ibz_set(&one, 1);
        ibz_sub(&q_tmp, &q_tmp, &one);
        ibz_add(&r_tmp, &r_tmp, &d_input);
    }

    if (q)
        ibz_copy(q, &q_tmp);
    if (r)
        ibz_copy(r, &r_tmp);
}

// Modulo
void
ibz_mod(ibz_t *r, const ibz_t *a, const ibz_t *b)
{
    ibz_t q;
    ibz_init(&q);
    ibz_div_floor(&q, r, a, b);
}

/* Reduce a signed value modulo a positive unsigned magnitude.  The modulus
 * may be 2^(IBZ_BITS-1), which is not a positive signed ibz_t. */
static void
ibz_mod_positive_magnitude(ibz_t *r, const ibz_t *a, const ibz_t *modulus)
{
    ibz_t magnitude, reduced;
    ibz_abs_unsigned(&magnitude, a);
    ibz_div_unsigned(NULL, &reduced, &magnitude, modulus);
    if (ibz_is_negative(a) && !ibz_is_zero(&reduced))
        ibz_sub_unsigned(&reduced, modulus, &reduced);
    ibz_copy(r, &reduced);
}

#if !defined(HAVE_UINT128)
/* a,b are in [0, modulus); modulus is an unsigned positive magnitude. */
static void
ibz_add_mod_positive(ibz_t *sum,
                     const ibz_t *a,
                     const ibz_t *b,
                     const ibz_t *modulus)
{
    ibz_t distance;
    ibz_sub_unsigned(&distance, modulus, b);
    if (ibz_cmp_unsigned(a, &distance) >= 0)
        ibz_sub_unsigned(sum, a, &distance);
    else
        ibz_add(sum, a, b);
}
#endif

/* Overflow-free modular multiplication for fixed integers.  On 128-bit
 * capable targets the complete 2N-limb product is reduced directly, so no
 * high product limbs are lost at the fixed-width boundary.  The portable
 * fallback retains add-and-double for products that do not fit. */
static void
ibz_mul_mod_positive(ibz_t *product,
                     const ibz_t *a,
                     const ibz_t *b,
                     const ibz_t *modulus)
{
#if defined(HAVE_UINT128)
    const size_t a_size = ibz_used_limbs(*a, IBZ_LIMBS);
    const size_t b_size = ibz_used_limbs(*b, IBZ_LIMBS);
    if (a_size == 0 || b_size == 0) {
        ibz_init(product);
        return;
    }

    uint64_t wide_product[2 * IBZ_LIMBS];
    uint64_t reduced[IBZ_LIMBS];
    ibz_mul_unsigned_wide(wide_product, *a, a_size, *b, b_size);
    ibz_divrem_unsigned_wide(NULL,
                             reduced,
                             wide_product,
                             a_size + b_size,
                             *modulus);
    memcpy(*product, reduced, sizeof(ibz_t));
#else
    const int a_bits = ibz_bitsize_unsigned(a);
    const int b_bits = ibz_bitsize_unsigned(b);
    if (a_bits == 0 || b_bits == 0) {
        ibz_init(product);
        return;
    }

    if (a_bits <= (IBZ_BITS - 1) - b_bits) {
        ibz_t unreduced;
        ibz_mul(&unreduced, a, b);
        ibz_div_unsigned(NULL, product, &unreduced, modulus);
        return;
    }

    ibz_t result, addend;
    ibz_init(&result);
    ibz_copy(&addend, a);

    const int bits = ibz_bitsize_unsigned(b);
    for (int bit = 0; bit < bits; ++bit) {
        if ((((*b)[bit / 64] >> (bit % 64)) & UINT64_C(1)) != 0)
            ibz_add_mod_positive(&result, &result, &addend, modulus);
        if (bit + 1 < bits)
            ibz_add_mod_positive(&addend, &addend, &addend, modulus);
    }
    ibz_copy(product, &result);
#endif
}

// Power
void
ibz_pow(ibz_t *pow, const ibz_t *x, uint32_t e)
{
    ibz_t result, base;
    ibz_set(&result, 1);
    ibz_copy(&base, x);

    while (e > 0) {
        if (e & 1)
            ibz_mul(&result, &result, &base);
        e >>= 1;
        if (e != 0)
            ibz_mul(&base, &base, &base);
    }

    ibz_copy(pow, &result);
}

// Modulo for unsigned long
unsigned long
ibz_mod_ui(const ibz_t *n, unsigned long d)
{
    if (d == 0)
        ibz_division_by_zero_abort("modulo");

    ibz_t magnitude;
    ibz_abs_unsigned(&magnitude, n);

    /* This is the path used by primality trial division.  Reducing two
     * 32-bit halves per active limb avoids constructing an unused quotient. */
    if (d <= UINT32_MAX) {
        unsigned long remainder =
            (unsigned long)ibz_mod_unsigned_small(&magnitude, (uint32_t)d);
        if (ibz_is_negative(n) && remainder != 0)
            remainder = d - remainder;
        return remainder;
    }

    ibz_t divisor, remainder;
    ibz_set_u64(&divisor, d);
    ibz_div_unsigned(NULL, &remainder, &magnitude, &divisor);
    if (ibz_is_negative(n) && !ibz_is_zero(&remainder))
        ibz_sub_unsigned(&remainder, &divisor, &remainder);

    return (unsigned long)remainder[0];
}

// Probabilistic primality test (Miller-Rabin)
int
ibz_probab_prime(const ibz_t *n, int reps)
{
    if (reps <= 0)
        return 0;
    if (ibz_cmp_int32(n, 2) == 0)
        return 1;
    if (ibz_cmp_int32(n, 3) == 0)
        return 1;
    if (ibz_is_even(n))
        return 0;
    if (ibz_cmp_int32(n, 1) <= 0)
        return 0;

    /* Match the fixed-precision implementation's cheap composite filter.
     * Most candidates produced by quat_represent_integer have a small prime
     * factor; rejecting them here avoids entering a full Miller--Rabin
     * modular exponentiation. */
    static const unsigned long small_primes[50] = {
          3,   5,   7,  11,  13,  17,  19,  23,  29,  31,
         37,  41,  43,  47,  53,  59,  61,  67,  71,  73,
         79,  83,  89,  97, 101, 103, 107, 109, 113, 127,
        131, 137, 139, 149, 151, 157, 163, 167, 173, 179,
        181, 191, 193, 197, 199, 211, 223, 227, 229, 233
    };
    for (size_t i = 0; i < sizeof(small_primes) / sizeof(small_primes[0]); ++i) {
        const unsigned long prime = small_primes[i];
        if (ibz_cmp_int32(n, (int32_t)prime) == 0)
            return 1;
        if (ibz_mod_ui(n, prime) == 0)
            return 0;
    }

    ibz_t n_minus_1, d, a, x;
    ibz_sub(&n_minus_1, n, (const ibz_t *)&ibz_const_one);
    ibz_copy(&d, &n_minus_1);

    int s = 0;
    while (ibz_is_even(&d)) {
        ibz_div_2exp(&d, &d, 1);
        ++s;
    }

    for (int i = 0; i < reps; i++) {
        ibz_t two, n_minus_2;
        ibz_set(&two, 2);
        ibz_sub(&n_minus_2, n, &two);
        if (!ibz_rand_interval(&a, &two, &n_minus_2))
            return 0;

        // x = a^d mod n
        ibz_pow_mod(&x, &a, &d, n);

        if (ibz_is_one(&x) || ibz_cmp(&x, &n_minus_1) == 0) {
            continue;
        }

        int composite = 1;
        for (int round = 1; round < s; ++round) {
            ibz_mul_mod_positive(&x, &x, &x, n);

            if (ibz_is_one(&x)) {
                return 0; // Composite
            }
            if (ibz_cmp(&x, &n_minus_1) == 0) {
                composite = 0;
                break;
            }
        }

        if (composite) {
            return 0;
        }
    }

    return 1; // Probably prime
}

// Modular exponentiation
void
ibz_pow_mod(ibz_t *pow, const ibz_t *x, const ibz_t *e, const ibz_t *m)
{
    ibz_t result, base, exp, modulus;
    ibz_abs_unsigned(&modulus, m);
    if (ibz_is_zero(&modulus))
        ibz_division_by_zero_abort("modular exponentiation");
    if (ibz_is_negative(e))
        ibz_invalid_argument_abort("negative modular exponent");

    ibz_mod_positive_magnitude(&base, x, &modulus);
    ibz_copy(&exp, e);
    ibz_set(&result, 1);
    ibz_div_unsigned(NULL, &result, &result, &modulus); /* 1 mod m */

    const int exponent_bits = ibz_bitsize_unsigned(&exp);
    for (int bit = 0; bit < exponent_bits; ++bit) {
        if ((exp[bit / 64] & (UINT64_C(1) << (bit % 64))) != 0)
            ibz_mul_mod_positive(&result, &result, &base, &modulus);
        if (bit + 1 < exponent_bits)
            ibz_mul_mod_positive(&base, &base, &base, &modulus);
    }

    ibz_copy(pow, &result);
}

// GCD using binary algorithm
void
ibz_gcd(ibz_t *gcd, const ibz_t *a, const ibz_t *b)
{
    ibz_t u, v;
    ibz_abs_unsigned(&u, a);
    ibz_abs_unsigned(&v, b);

    if (ibz_is_zero(&u)) {
        ibz_copy(gcd, &v);
        return;
    }
    if (ibz_is_zero(&v)) {
        ibz_copy(gcd, &u);
        return;
    }

    int shift = MIN(ibz_two_adic(&u), ibz_two_adic(&v));
    ibz_shift_right_unsigned(&u, &u, (uint32_t)shift);
    ibz_shift_right_unsigned(&v, &v, (uint32_t)shift);

    while (!ibz_is_zero(&u)) {
        ibz_shift_right_unsigned(&u, &u, (uint32_t)ibz_two_adic(&u));
        ibz_shift_right_unsigned(&v, &v, (uint32_t)ibz_two_adic(&v));

        if (ibz_cmp_unsigned(&u, &v) > 0) {
            ibz_swap(&u, &v);
        }

        ibz_sub_unsigned(&v, &v, &u);
    }

    // Restore common factors of 2 in the unsigned magnitude domain.
    ibz_shift_left_unsigned(&v, &v, (uint32_t)shift);

    ibz_copy(gcd, &v);
}

void
ibz_gcdext(ibz_t *gcd, ibz_t *x, ibz_t *y,
           const ibz_t *a, const ibz_t *b)
{
    // special case: a == 0
    if (ibz_is_zero(a)) {
        ibz_abs_unsigned(gcd, b);
        ibz_set(x, 0);
        if (ibz_is_negative(b) && !ibz_is_min_value(b)) {
            ibz_set(y, -1);
        } else {
            ibz_set(y, 1);
        }
        return;
    }

    // special case: b == 0
    if (ibz_is_zero(b)) {
        ibz_abs_unsigned(gcd, a);
        if (ibz_is_negative(a) && !ibz_is_min_value(a)) {
            ibz_set(x, -1);
        } else {
            ibz_set(x, 1);
        }
        ibz_set(y, 0);
        return;
    }

    ibz_t aa, bb;
    ibz_t x0, x1, y0, y1;
    ibz_t q, r;
    ibz_t tmp1, tmp2;

    // copy a, b locally since they may be reused later
    ibz_copy(&aa, a);
    ibz_copy(&bb, b);

    // (x0, y0) = (1, 0), (x1, y1) = (0, 1)
    ibz_set(&x0, 1);
    ibz_set(&y0, 0);
    ibz_set(&x1, 0);
    ibz_set(&y1, 1);

    // standard extended Euclidean algorithm
    while (!ibz_is_zero(&bb)) {
        // aa = q * bb + r  (ibz_div: q, r with signs matching aa, bb)
        ibz_div(&q, &r, &aa, &bb);

        // (aa, bb) <- (bb, r)
        ibz_copy(&aa, &bb);
        ibz_copy(&bb, &r);

        // (x0, x1) <- (x1, x0 - q*x1)
        ibz_mul(&tmp1, &q, &x1);    // tmp1 = q * x1
        ibz_sub(&tmp2, &x0, &tmp1); // tmp2 = x0 - q*x1
        ibz_copy(&x0, &x1);
        ibz_copy(&x1, &tmp2);

        // (y0, y1) <- (y1, y0 - q*y1)
        ibz_mul(&tmp1, &q, &y1);    // tmp1 = q * y1
        ibz_sub(&tmp2, &y0, &tmp1); // tmp2 = y0 - q*y1
        ibz_copy(&y0, &y1);
        ibz_copy(&y1, &tmp2);
    }

    // now aa == gcd(a, b) (signed)
    ibz_copy(gcd, &aa);
    ibz_copy(x, &x0);
    ibz_copy(y, &y0);

    // normalize gcd to positive: if gcd < 0 flip the sign of gcd, x, y
    if (ibz_is_negative(gcd) && !ibz_is_min_value(gcd)) {
        ibz_neg(gcd, gcd);
        ibz_neg(x, x);
        ibz_neg(y, y);
    }
}


// void
// ibz_gcdext(ibz_t *gcd, ibz_t *x, ibz_t *y, const ibz_t *a, const ibz_t *b)
// {
//     if (ibz_is_zero(a)) {
//         ibz_abs(gcd, b);
//         ibz_set(x, 0);
//         if (ibz_is_negative(b)) {
//             ibz_set(y, -1);
//         } else {
//             ibz_set(y, 1);
//         }
//         return;
//     }

//     if (ibz_is_zero(b)) {
//         ibz_abs(gcd, a);
//         if (ibz_is_negative(a)) {
//             ibz_set(x, -1);
//         } else {
//             ibz_set(x, 1);
//         }
//         ibz_set(y, 0);
//         return;
//     }

//     int a_was_neg = ibz_is_negative(a);
//     int b_was_neg = ibz_is_negative(b);

//     ibz_t u, v, A, B, C, D, temp;

//     ibz_abs(&u, a);
//     ibz_abs(&v, b);

//     ibz_set(&A, 1);
//     ibz_set(&B, 0);
//     ibz_set(&C, 0);
//     ibz_set(&D, 1);

//     int shift = 0;
//     while (ibz_is_even(&u) && ibz_is_even(&v)) {
//         ibz_div_2exp(&u, &u, 1);
//         ibz_div_2exp(&v, &v, 1);
//         shift++;
//     }

//     ibz_t orig_u, orig_v;
//     ibz_copy(&orig_u, &u);
//     ibz_copy(&orig_v, &v);

//     // Binary Extended GCD
//     while (!ibz_is_zero(&u)) {
//         while (ibz_is_even(&u)) {
//             ibz_div_2exp(&u, &u, 1);

//             if (ibz_is_even(&A) && ibz_is_even(&B)) {
//                 ibz_div_2exp(&A, &A, 1);
//                 ibz_div_2exp(&B, &B, 1);
//             } else {
//                 // A = (A + orig_v) / 2
//                 ibz_add(&temp, &A, &orig_v);
//                 ibz_div_2exp(&A, &temp, 1);
//                 // B = (B - orig_u) / 2
//                 ibz_sub(&temp, &B, &orig_u);
//                 ibz_div_2exp(&B, &temp, 1);
//             }
//         }

//         while (ibz_is_even(&v)) {
//             ibz_div_2exp(&v, &v, 1);

//             if (ibz_is_even(&C) && ibz_is_even(&D)) {
//                 ibz_div_2exp(&C, &C, 1);
//                 ibz_div_2exp(&D, &D, 1);
//             } else {
//                 // C = (C + orig_v) / 2
//                 ibz_add(&temp, &C, &orig_v);
//                 ibz_div_2exp(&C, &temp, 1);
//                 // D = (D - orig_u) / 2
//                 ibz_sub(&temp, &D, &orig_u);
//                 ibz_div_2exp(&D, &temp, 1);
//             }
//         }

//         if (ibz_cmp(&u, &v) >= 0) {
//             ibz_sub(&u, &u, &v);
//             ibz_sub(&A, &A, &C);
//             ibz_sub(&B, &B, &D);
//         } else {
//             // v = v - u, C = C - A, D = D - B
//             ibz_sub(&v, &v, &u);
//             ibz_sub(&C, &C, &A);
//             ibz_sub(&D, &D, &B);
//         }
//     }

//     // gcd = v * 2^shift
//     ibz_copy(gcd, &v);
//     ibz_mul_2exp(gcd, gcd, shift);

//     ibz_copy(x, &C);
//     ibz_copy(y, &D);

//     if (a_was_neg) {
//         ibz_neg(x, x);
//     }
//     if (b_was_neg) {
//         ibz_neg(y, y);
//     }
// }

// Modular inverse: a^(-1) mod m
int
ibz_invmod(ibz_t *inv, const ibz_t *a, const ibz_t *mod)
{
    if (ibz_is_zero(mod) || ibz_cmp_int32(mod, 0) <= 0) {
        return 0;
    }

    if (ibz_is_zero(a)) {
        return 0;
    }

    ibz_t gcd, x, y;
    ibz_t a_mod;

    ibz_mod(&a_mod, a, mod);

    if (ibz_is_zero(&a_mod)) {
        return 0;
    }

    ibz_gcdext(&gcd, &x, &y, &a_mod, mod);

    if (!ibz_is_one(&gcd)) {
        return 0;
    }

    ibz_mod(inv, &x, mod);

    return 1;
}

// Check divisibility
int
ibz_divides(const ibz_t *a, const ibz_t *b)
{
    if (ibz_is_zero(b))
        return 0;
    ibz_t q, r;
    ibz_div(&q, &r, a, b);
    return ibz_is_zero(&r);
}

// Integer square root
int
ibz_sqrt(ibz_t *sqrt, const ibz_t *a)
{
    if (ibz_is_negative(a)) {
        return 0;
    }

    if (ibz_is_zero(a)) {
        ibz_init(sqrt);
        return 1;
    }

    ibz_t x, x_prev, temp;

    /* Start from a power-of-two upper bound on sqrt(a).  Starting from a
     * itself initially only halves the approximation, so a roughly 2050-bit
     * level-5 ideal index needs more than the 1000 iterations allowed below
     * and is falsely reported as a non-square. */
    const int bits = ibz_bitsize(a);
    ibz_set(&x, 1);
    ibz_mul_2exp(&x, &x, (bits + 1) / 2);

    // Newton-Raphson: x_new = (x + a/x) / 2
    for (int iter = 0; iter < 1000; iter++) {
        ibz_copy(&x_prev, &x);

        ibz_t q, r;
        ibz_div(&q, &r, a, &x);
        ibz_add(&temp, &x, &q);
        ibz_div_2exp(&x, &temp, 1);

        if (ibz_cmp(&x, &x_prev) == 0) {
            break;
        }

        if (ibz_cmp(&x, &x_prev) > 0) {
            ibz_copy(&x, &x_prev);
            break;
        }
    }

    ibz_mul(&temp, &x, &x);
    if (ibz_cmp(&temp, a) == 0) {
        ibz_copy(sqrt, &x);
        return 1;
    }

    return 0;
}

// Square root floor
void
ibz_sqrt_floor(ibz_t *sqrt, const ibz_t *a)
{
    if (ibz_is_negative(a) || ibz_is_zero(a)) {
        ibz_init(sqrt);
        return;
    }

    ibz_t x, x_next, temp, two;
    ibz_set(&two, 2);

    int bits = ibz_bitsize(a);
    ibz_set(&x, 1);
    ibz_mul_2exp(&x, &x, (bits + 1) / 2);

    // Newton-Raphson with proper termination
    for (int iter = 0; iter < 1000; iter++) {
        ibz_t q, r;
        ibz_div(&q, &r, a, &x);
        ibz_add(&temp, &x, &q);
        ibz_div_2exp(&x_next, &temp, 1);

        if (ibz_cmp(&x_next, &x) >= 0) {
            break;
        }

        ibz_copy(&x, &x_next);
    }

    ibz_copy(sqrt, &x);
}

// Legendre symbol
int
ibz_legendre(const ibz_t *a, const ibz_t *p)
{
    ibz_t a_mod, exp, result, p_minus_1;

    if (ibz_cmp_int32(p, 2) <= 0 || ibz_is_even(p))
        return 0;

    ibz_mod(&a_mod, a, p);

    if (ibz_is_zero(&a_mod)) {
        return 0;
    }

    ibz_sub(&p_minus_1, p, (const ibz_t *)&ibz_const_one);
    ibz_div_2exp(&exp, &p_minus_1, 1);

    ibz_pow_mod(&result, &a_mod, &exp, p);

    if (ibz_is_one(&result)) {
        return 1;
    } else {
        return -1;
    }
}

// Modular square root
// int
// ibz_sqrt_mod_p(ibz_t *sqrt, const ibz_t *a, const ibz_t *p)
// {
//     ibz_t a_mod;
//     ibz_mod(&a_mod, a, p);

//     if (ibz_is_zero(&a_mod)) {
//         ibz_init(sqrt);
//         return 1;
//     }

//     if (ibz_legendre(&a_mod, p) != 1) {
//         return 0;
//     }

//     ibz_t p_mod_4, three, four;
//     ibz_set(&three, 3);
//     ibz_set(&four, 4);
//     ibz_mod(&p_mod_4, p, &four);

//     if (ibz_cmp(&p_mod_4, &three) == 0) {
//         ibz_t exp;
//         ibz_add(&exp, p, (const ibz_t *)&ibz_const_one);
//         ibz_div_2exp(&exp, &exp, 2);
//         ibz_pow_mod(sqrt, &a_mod, &exp, p);
//         return 1;
//     }

//     return 0;
// }

// Modular square root for odd prime p (Tonelli–Shanks)
// returns 1 if sqrt exists (and writes it), 0 otherwise.
int
ibz_sqrt_mod_p(ibz_t *sqrt, const ibz_t *a, const ibz_t *p)
{
    /* Cornacchia intentionally calls this as sqrt == a.  Preserve both
     * inputs before clearing the transactional output; otherwise the former
     * eager ibz_init(sqrt) changed the radicand to zero and made every
     * Cornacchia attempt fail. */
    ibz_t a_input, p_input;
    ibz_copy(&a_input, a);
    ibz_copy(&p_input, p);
    const ibz_t *modulus = &p_input;

    ibz_init(sqrt);
    if (ibz_cmp_int32(modulus, 2) < 0 ||
        (ibz_cmp_int32(modulus, 2) > 0 && ibz_is_even(modulus)))
        return 0;

    // handle p == 2 (not really used in your setting, but safe)
    if (ibz_cmp_int32(modulus, 2) == 0) {
        ibz_t am;
        ibz_mod(&am, &a_input, modulus);
        ibz_copy(sqrt, &am);
        return 1;
    }

    // a := a mod p
    ibz_t n;
    ibz_mod(&n, &a_input, modulus);
    if (ibz_is_zero(&n)) {
        ibz_init(sqrt);
        return 1;
    }

    // Check quadratic residue via Legendre
    if (ibz_legendre(&n, modulus) != 1) {
        return 0;
    }

    // Factor p-1 = q * 2^s with q odd
    ibz_t pm1, q;
    ibz_sub(&pm1, modulus, (const ibz_t *)&ibz_const_one);
    ibz_copy(&q, &pm1);

    int s = 0;
    while (ibz_is_even(&q)) {
        ibz_div_2exp(&q, &q, 1);
        s++;
    }

    // If s == 1, p % 4 == 3 fast-path works too, but TS handles it fine.

    // Find z, a quadratic non-residue mod p
    ibz_t z;
    ibz_set(&z, 2);
    while (ibz_legendre(&z, modulus) != -1) {
        ibz_add(&z, &z, (const ibz_t *)&ibz_const_one);
    }

    // c = z^q mod p
    ibz_t c;
    ibz_pow_mod(&c, &z, &q, modulus);

    // x = n^((q+1)/2) mod p
    ibz_t q1, e;
    ibz_add(&q1, &q, (const ibz_t *)&ibz_const_one);
    ibz_div_2exp(&e, &q1, 1);
    ibz_t x;
    ibz_pow_mod(&x, &n, &e, modulus);

    // t = n^q mod p
    ibz_t t;
    ibz_pow_mod(&t, &n, &q, modulus);

    int m = s;

    ibz_t one;
    ibz_set(&one, 1);

    while (!ibz_is_one(&t)) {
        // Find least i (0 < i < m) such that t^(2^i) == 1
        int i = 0;
        ibz_t t2i;
        ibz_copy(&t2i, &t);

        for (i = 1; i < m; i++) {
            // t2i = t2i^2 mod p
            ibz_mul_mod_positive(&t2i, &t2i, &t2i, modulus);
            if (ibz_is_one(&t2i))
                break;
        }

        // Should always find such i
        if (i == m) {
            return 0; // defensive (shouldn't happen if p prime and residue)
        }

        // b = c^(2^(m-i-1)) mod p
        ibz_t b;
        ibz_copy(&b, &c);
        for (int j = 0; j < (m - i - 1); j++) {
            ibz_mul_mod_positive(&b, &b, &b, modulus);
        }

        // x = x*b mod p
        ibz_mul_mod_positive(&x, &x, &b, modulus);

        // t = t*b^2 mod p
        ibz_t b2;
        ibz_mul_mod_positive(&b2, &b, &b, modulus);

        ibz_mul_mod_positive(&t, &t, &b2, modulus);

        // c = b^2 mod p
        ibz_copy(&c, &b2);

        m = i;
    }

    ibz_copy(sqrt, &x);
    return 1;
}
