#include <quaternion.h>
#include "internal.h"

// Internal helper functions

void
quat_alg_init_set_ui(quat_alg_t *alg, unsigned int p)
{
    ibz_t bp;
    ibz_init(&bp);
    ibz_set(&bp, p);
    quat_alg_init_set(alg, &bp);
    ibz_finalize(&bp);
}

void
quat_alg_coord_mul(ibz_vec_4_t *res, const ibz_vec_4_t *a, const ibz_vec_4_t *b, const quat_alg_t *alg)
{
    ibz_t prod;
    ibz_vec_4_t sum;
    ibz_init(&prod);
    ibz_vec_4_init(&sum);

    ibz_set(&(sum[0]), 0);
    ibz_set(&(sum[1]), 0);
    ibz_set(&(sum[2]), 0);
    ibz_set(&(sum[3]), 0);

    // compute 1 coordinate
    ibz_mul(&prod, &((*a)[2]), &((*b)[2]));
    ibz_sub(&(sum[0]), &(sum[0]), &prod);
    ibz_mul(&prod, &((*a)[3]), &((*b)[3]));
    ibz_sub(&(sum[0]), &(sum[0]), &prod);
    ibz_mul(&(sum[0]), &(sum[0]), &(alg->p));
    ibz_mul(&prod, &((*a)[0]), &((*b)[0]));
    ibz_add(&(sum[0]), &(sum[0]), &prod);
    ibz_mul(&prod, &((*a)[1]), &((*b)[1]));
    ibz_sub(&(sum[0]), &(sum[0]), &prod);
    // compute i coordiante
    ibz_mul(&prod, &((*a)[2]), &((*b)[3]));
    ibz_add(&(sum[1]), &(sum[1]), &prod);
    ibz_mul(&prod, &((*a)[3]), &((*b)[2]));
    ibz_sub(&(sum[1]), &(sum[1]), &prod);
    ibz_mul(&(sum[1]), &(sum[1]), &(alg->p));
    ibz_mul(&prod, &((*a)[0]), &((*b)[1]));
    ibz_add(&(sum[1]), &(sum[1]), &prod);
    ibz_mul(&prod, &((*a)[1]), &((*b)[0]));
    ibz_add(&(sum[1]), &(sum[1]), &prod);
    // compute j coordiante
    ibz_mul(&prod, &((*a)[0]), &((*b)[2]));
    ibz_add(&(sum[2]), &(sum[2]), &prod);
    ibz_mul(&prod, &((*a)[2]), &((*b)[0]));
    ibz_add(&(sum[2]), &(sum[2]), &prod);
    ibz_mul(&prod, &((*a)[1]), &((*b)[3]));
    ibz_sub(&(sum[2]), &(sum[2]), &prod);
    ibz_mul(&prod, &((*a)[3]), &((*b)[1]));
    ibz_add(&(sum[2]), &(sum[2]), &prod);
    // compute ij coordiante
    ibz_mul(&prod, &((*a)[0]), &((*b)[3]));
    ibz_add(&(sum[3]), &(sum[3]), &prod);
    ibz_mul(&prod, &((*a)[3]), &((*b)[0]));
    ibz_add(&(sum[3]), &(sum[3]), &prod);
    ibz_mul(&prod, &((*a)[2]), &((*b)[1]));
    ibz_sub(&(sum[3]), &(sum[3]), &prod);
    ibz_mul(&prod, &((*a)[1]), &((*b)[2]));
    ibz_add(&(sum[3]), &(sum[3]), &prod);

    ibz_copy(&((*res)[0]), &(sum[0]));
    ibz_copy(&((*res)[1]), &(sum[1]));
    ibz_copy(&((*res)[2]), &(sum[2]));
    ibz_copy(&((*res)[3]), &(sum[3]));

    ibz_finalize(&prod);
    ibz_vec_4_finalize(&sum);
}

void
quat_alg_equal_denom(quat_alg_elem_t *res_a, quat_alg_elem_t *res_b, const quat_alg_elem_t *a, const quat_alg_elem_t *b)
{
    ibz_t gcd, r;
    ibz_init(&gcd);
    ibz_init(&r);
    ibz_gcd(&gcd, &(a->denom), &(b->denom));
    // temporarily set res_a.denom to a.denom/gcd, and res_b.denom to b.denom/gcd
    ibz_div(&(res_a->denom), &r, &(a->denom), &gcd);
    ibz_div(&(res_b->denom), &r, &(b->denom), &gcd);
    for (int i = 0; i < 4; i++) {
        // multiply coordiates by reduced denominators from the other element
        ibz_mul(&(res_a->coord[i]), &(a->coord[i]), &(res_b->denom));
        ibz_mul(&(res_b->coord[i]), &(b->coord[i]), &(res_a->denom));
    }
    // multiply both reduced denominators
    ibz_mul(&(res_a->denom), &(res_a->denom), &(res_b->denom));
    // multiply them by the gcd to get the new common denominator
    ibz_mul(&(res_b->denom), &(res_a->denom), &gcd);
    ibz_mul(&(res_a->denom), &(res_a->denom), &gcd);
    ibz_finalize(&gcd);
    ibz_finalize(&r);
}

// Public Functions

void
quat_alg_add(quat_alg_elem_t *res, const quat_alg_elem_t *a, const quat_alg_elem_t *b)
{
    quat_alg_elem_t res_a, res_b;
    quat_alg_elem_init(&res_a);
    quat_alg_elem_init(&res_b);
    // put both on the same denominator
    quat_alg_equal_denom(&res_a, &res_b, a, b);
    // then add
    ibz_copy(&(res->denom), &(res_a.denom));
    ibz_vec_4_add(&(res->coord), &(res_a.coord), &(res_b.coord));
    quat_alg_elem_finalize(&res_a);
    quat_alg_elem_finalize(&res_b);
}

void
quat_alg_sub(quat_alg_elem_t *res, const quat_alg_elem_t *a, const quat_alg_elem_t *b)
{
    quat_alg_elem_t res_a, res_b;
    quat_alg_elem_init(&res_a);
    quat_alg_elem_init(&res_b);
    // put both on the same denominator
    quat_alg_equal_denom(&res_a, &res_b, a, b);
    // then substract
    ibz_copy(&res->denom, &res_a.denom);
    ibz_vec_4_sub(&res->coord, &res_a.coord, &res_b.coord);
    quat_alg_elem_finalize(&res_a);
    quat_alg_elem_finalize(&res_b);
}

void
quat_alg_mul(quat_alg_elem_t *res, const quat_alg_elem_t *a, const quat_alg_elem_t *b, const quat_alg_t *alg)
{
    // denominator: product of denominators
    ibz_mul(&(res->denom), &(a->denom), &(b->denom));
    quat_alg_coord_mul(&(res->coord), &(a->coord), &(b->coord), alg);
}

void
quat_alg_norm_mod(ibz_t *res, const quat_alg_elem_t *x, const ibz_t *N, const quat_alg_t *alg)
{
    /* paper Issue 14: nrd(x) mod N = (x0^2 + x1^2 + p*x2^2 + p*x3^2) mod N
     * with each ibz_mul reduced before the next operation. Max transient
     * is max(2*log2(N), p_bits + log2(N)), matching paper Lemma 4N^2 when N >= p. */
    assert(ibz_is_one(&(x->denom)));
    ibz_t tmp;
    ibz_init(&tmp);

    /* res = x0^2 mod N */
    ibz_mul(res, &(x->coord[0]), &(x->coord[0]));
    ibz_mod(res, res, N);

    /* res += x1^2 mod N */
    ibz_mul(&tmp, &(x->coord[1]), &(x->coord[1]));
    ibz_mod(&tmp, &tmp, N);
    ibz_add(res, res, &tmp);

    /* res += p * (x2^2 mod N) mod N */
    ibz_mul(&tmp, &(x->coord[2]), &(x->coord[2]));
    ibz_mod(&tmp, &tmp, N);
    ibz_mul(&tmp, &tmp, &(alg->p));
    ibz_mod(&tmp, &tmp, N);
    ibz_add(res, res, &tmp);

    /* res += p * (x3^2 mod N) mod N */
    ibz_mul(&tmp, &(x->coord[3]), &(x->coord[3]));
    ibz_mod(&tmp, &tmp, N);
    ibz_mul(&tmp, &tmp, &(alg->p));
    ibz_mod(&tmp, &tmp, N);
    ibz_add(res, res, &tmp);

    ibz_mod(res, res, N);

    fprintf(stderr, "[ALGNORM-MOD] in_coord_max=%d N=%d out=%d\n",
        (int)ibz_bitsize(&(x->coord[0])) > (int)ibz_bitsize(&(x->coord[1])) ?
            (ibz_bitsize(&(x->coord[0])) > ibz_bitsize(&(x->coord[2])) ?
                (ibz_bitsize(&(x->coord[0])) > ibz_bitsize(&(x->coord[3])) ? ibz_bitsize(&(x->coord[0])) : ibz_bitsize(&(x->coord[3]))) :
                (ibz_bitsize(&(x->coord[2])) > ibz_bitsize(&(x->coord[3])) ? ibz_bitsize(&(x->coord[2])) : ibz_bitsize(&(x->coord[3])))) :
            (ibz_bitsize(&(x->coord[1])) > ibz_bitsize(&(x->coord[2])) ?
                (ibz_bitsize(&(x->coord[1])) > ibz_bitsize(&(x->coord[3])) ? ibz_bitsize(&(x->coord[1])) : ibz_bitsize(&(x->coord[3]))) :
                (ibz_bitsize(&(x->coord[2])) > ibz_bitsize(&(x->coord[3])) ? ibz_bitsize(&(x->coord[2])) : ibz_bitsize(&(x->coord[3])))),
        ibz_bitsize(N), ibz_bitsize(res));
    fflush(stderr);

    ibz_finalize(&tmp);
}

void
quat_alg_mul_mod(quat_alg_elem_t *res, const quat_alg_elem_t *a, const quat_alg_elem_t *b, const ibz_t *N, const quat_alg_t *alg)
{
    /* paper Issue 14: (a*b) mod N*O_0 — coords computed with each ibz_mul
     * followed by ibz_mod, max transient = max(2*log2(N), p_bits + log2(N)).
     * Use a local sum[4] so aliasing res==a or res==b is safe. */
    assert(ibz_is_one(&(a->denom)));
    assert(ibz_is_one(&(b->denom)));
    ibz_t prod;
    ibz_vec_4_t sum;
    ibz_init(&prod);
    ibz_vec_4_init(&sum);

    /* sum[0] = a[0]*b[0] - a[1]*b[1] - p*(a[2]*b[2] + a[3]*b[3]) */
    ibz_mul(&prod, &(a->coord[2]), &(b->coord[2]));
    ibz_mod(&prod, &prod, N);
    ibz_sub(&sum[0], &sum[0], &prod);
    ibz_mul(&prod, &(a->coord[3]), &(b->coord[3]));
    ibz_mod(&prod, &prod, N);
    ibz_sub(&sum[0], &sum[0], &prod);
    ibz_mod(&sum[0], &sum[0], N);
    ibz_mul(&sum[0], &sum[0], &(alg->p));
    ibz_mod(&sum[0], &sum[0], N);
    ibz_mul(&prod, &(a->coord[0]), &(b->coord[0]));
    ibz_mod(&prod, &prod, N);
    ibz_add(&sum[0], &sum[0], &prod);
    ibz_mul(&prod, &(a->coord[1]), &(b->coord[1]));
    ibz_mod(&prod, &prod, N);
    ibz_sub(&sum[0], &sum[0], &prod);
    ibz_mod(&sum[0], &sum[0], N);

    /* sum[1] = a[0]*b[1] + a[1]*b[0] + p*(a[2]*b[3] - a[3]*b[2]) */
    ibz_mul(&prod, &(a->coord[2]), &(b->coord[3]));
    ibz_mod(&prod, &prod, N);
    ibz_add(&sum[1], &sum[1], &prod);
    ibz_mul(&prod, &(a->coord[3]), &(b->coord[2]));
    ibz_mod(&prod, &prod, N);
    ibz_sub(&sum[1], &sum[1], &prod);
    ibz_mod(&sum[1], &sum[1], N);
    ibz_mul(&sum[1], &sum[1], &(alg->p));
    ibz_mod(&sum[1], &sum[1], N);
    ibz_mul(&prod, &(a->coord[0]), &(b->coord[1]));
    ibz_mod(&prod, &prod, N);
    ibz_add(&sum[1], &sum[1], &prod);
    ibz_mul(&prod, &(a->coord[1]), &(b->coord[0]));
    ibz_mod(&prod, &prod, N);
    ibz_add(&sum[1], &sum[1], &prod);
    ibz_mod(&sum[1], &sum[1], N);

    /* sum[2] = a[0]*b[2] + a[2]*b[0] - a[1]*b[3] + a[3]*b[1] */
    ibz_mul(&prod, &(a->coord[0]), &(b->coord[2]));
    ibz_mod(&prod, &prod, N);
    ibz_add(&sum[2], &sum[2], &prod);
    ibz_mul(&prod, &(a->coord[2]), &(b->coord[0]));
    ibz_mod(&prod, &prod, N);
    ibz_add(&sum[2], &sum[2], &prod);
    ibz_mul(&prod, &(a->coord[1]), &(b->coord[3]));
    ibz_mod(&prod, &prod, N);
    ibz_sub(&sum[2], &sum[2], &prod);
    ibz_mul(&prod, &(a->coord[3]), &(b->coord[1]));
    ibz_mod(&prod, &prod, N);
    ibz_add(&sum[2], &sum[2], &prod);
    ibz_mod(&sum[2], &sum[2], N);

    /* sum[3] = a[0]*b[3] + a[3]*b[0] - a[2]*b[1] + a[1]*b[2] */
    ibz_mul(&prod, &(a->coord[0]), &(b->coord[3]));
    ibz_mod(&prod, &prod, N);
    ibz_add(&sum[3], &sum[3], &prod);
    ibz_mul(&prod, &(a->coord[3]), &(b->coord[0]));
    ibz_mod(&prod, &prod, N);
    ibz_add(&sum[3], &sum[3], &prod);
    ibz_mul(&prod, &(a->coord[2]), &(b->coord[1]));
    ibz_mod(&prod, &prod, N);
    ibz_sub(&sum[3], &sum[3], &prod);
    ibz_mul(&prod, &(a->coord[1]), &(b->coord[2]));
    ibz_mod(&prod, &prod, N);
    ibz_add(&sum[3], &sum[3], &prod);
    ibz_mod(&sum[3], &sum[3], N);

    for (int i = 0; i < 4; i++)
        ibz_copy(&(res->coord[i]), &sum[i]);
    ibz_set(&(res->denom), 1);

    int mx = ibz_bitsize(&(res->coord[0]));
    for (int i = 1; i < 4; i++) {
        int b = ibz_bitsize(&(res->coord[i]));
        if (b > mx) mx = b;
    }
    fprintf(stderr, "[ALGMUL-MOD] N=%d out_coord_max=%d\n", ibz_bitsize(N), mx);
    fflush(stderr);

    ibz_finalize(&prod);
    ibz_vec_4_finalize(&sum);
}

void
quat_alg_norm(ibz_t *res_num, ibz_t *res_denom, const quat_alg_elem_t *a, const quat_alg_t *alg)
{
    ibz_t r, g;
    quat_alg_elem_t norm;
    ibz_init(&r);
    ibz_init(&g);
    quat_alg_elem_init(&norm);

    quat_alg_conj(&norm, a);
    quat_alg_mul(&norm, a, &norm, alg);
    ibz_gcd(&g, &(norm.coord[0]), &(norm.denom));
    ibz_div(res_num, &r, &(norm.coord[0]), &g);
    ibz_div(res_denom, &r, &(norm.denom), &g);
    ibz_abs(res_denom, res_denom);
    ibz_abs(res_num, res_num);
    assert(ibz_cmp(res_denom, &ibz_const_zero) > 0);

    /* P5 trace: input coord max bit / output norm bit / output denom bit */
    {
        int in_max = 0;
        for (int _i = 0; _i < 4; _i++) {
            int b = ibz_bitsize(&(a->coord[_i]));
            if (b > in_max) in_max = b;
        }
        int in_den = ibz_bitsize(&(a->denom));
        int out_num = ibz_bitsize(res_num);
        int out_den = ibz_bitsize(res_denom);
        fprintf(stderr, "[ALGNORM] in_coord_max=%d in_den=%d out_num=%d out_den=%d ratio=%.2f\n",
            in_max, in_den, out_num, out_den,
            in_max > 0 ? (double)out_num / (double)in_max : 0.0);
        fflush(stderr);
    }

    quat_alg_elem_finalize(&norm);
    ibz_finalize(&r);
    ibz_finalize(&g);
}

void
quat_alg_scalar(quat_alg_elem_t *elem, const ibz_t *numerator, const ibz_t *denominator)
{
    ibz_copy(&(elem->denom), denominator);
    ibz_copy(&(elem->coord[0]), numerator);
    ibz_set(&(elem->coord[1]), 0);
    ibz_set(&(elem->coord[2]), 0);
    ibz_set(&(elem->coord[3]), 0);
}

void
quat_alg_conj(quat_alg_elem_t *conj, const quat_alg_elem_t *x)
{
    ibz_copy(&(conj->denom), &(x->denom));
    ibz_copy(&(conj->coord[0]), &(x->coord[0]));
    ibz_neg(&(conj->coord[1]), &(x->coord[1]));
    ibz_neg(&(conj->coord[2]), &(x->coord[2]));
    ibz_neg(&(conj->coord[3]), &(x->coord[3]));
}

void
quat_alg_make_primitive(ibz_vec_4_t *primitive_x, ibz_t *content, const quat_alg_elem_t *x, const quat_lattice_t *order)
{
    int ok UNUSED = quat_lattice_contains(primitive_x, order, x);
    assert(ok);
    ibz_vec_4_content(content, primitive_x);
    ibz_t r;
    ibz_init(&r);
    for (int i = 0; i < 4; i++) {
        ibz_div(*primitive_x + i, &r, *primitive_x + i, content);
    }
    ibz_finalize(&r);
}

void
quat_alg_normalize(quat_alg_elem_t *x)
{
    ibz_t gcd, sign, r;
    ibz_init(&gcd);
    ibz_init(&sign);
    ibz_init(&r);
    ibz_vec_4_content(&gcd, &(x->coord));
    ibz_gcd(&gcd, &gcd, &(x->denom));
    ibz_div(&(x->denom), &r, &(x->denom), &gcd);
    ibz_vec_4_scalar_div(&(x->coord), &gcd, &(x->coord));
    ibz_set(&sign, 2 * (0 > ibz_cmp(&ibz_const_zero, &(x->denom))) - 1);
    ibz_vec_4_scalar_mul(&(x->coord), &sign, &(x->coord));
    ibz_mul(&(x->denom), &sign, &(x->denom));
    ibz_finalize(&gcd);
    ibz_finalize(&sign);
    ibz_finalize(&r);
}

int
quat_alg_elem_equal(const quat_alg_elem_t *a, const quat_alg_elem_t *b)
{
    quat_alg_elem_t diff;
    quat_alg_elem_init(&diff);
    quat_alg_sub(&diff, a, b);
    int res = quat_alg_elem_is_zero(&diff);
    quat_alg_elem_finalize(&diff);
    return (res);
}

int
quat_alg_elem_is_zero(const quat_alg_elem_t *x)
{
    int res = ibz_vec_4_is_zero(&(x->coord));
    return (res);
}

void
quat_alg_elem_set(quat_alg_elem_t *elem, int32_t denom, int32_t coord0, int32_t coord1, int32_t coord2, int32_t coord3)
{
    ibz_set(&(elem->coord[0]), coord0);
    ibz_set(&(elem->coord[1]), coord1);
    ibz_set(&(elem->coord[2]), coord2);
    ibz_set(&(elem->coord[3]), coord3);

    ibz_set(&(elem->denom), denom);
}

void
quat_alg_elem_copy(quat_alg_elem_t *copy, const quat_alg_elem_t *copied)
{
    ibz_copy(&copy->denom, &copied->denom);
    ibz_copy(&copy->coord[0], &copied->coord[0]);
    ibz_copy(&copy->coord[1], &copied->coord[1]);
    ibz_copy(&copy->coord[2], &copied->coord[2]);
    ibz_copy(&copy->coord[3], &copied->coord[3]);
}

// helper functions for lattices
void
quat_alg_elem_copy_ibz(quat_alg_elem_t *elem,
                       const ibz_t *denom,
                       const ibz_t *coord0,
                       const ibz_t *coord1,
                       const ibz_t *coord2,
                       const ibz_t *coord3)
{
    ibz_copy(&(elem->coord[0]), coord0);
    ibz_copy(&(elem->coord[1]), coord1);
    ibz_copy(&(elem->coord[2]), coord2);
    ibz_copy(&(elem->coord[3]), coord3);

    ibz_copy(&(elem->denom), denom);
}

void
quat_alg_elem_mul_by_scalar(quat_alg_elem_t *res, const ibz_t *scalar, const quat_alg_elem_t *elem)
{
    for (int i = 0; i < 4; i++) {
        ibz_mul(&(res->coord[i]), &(elem->coord[i]), scalar);
    }
    ibz_copy(&(res->denom), &(elem->denom));
}
