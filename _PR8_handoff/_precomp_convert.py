#!/usr/bin/env python3
"""
Convert the-sqisign GMP mpz_t designator format precomp file
to compact-SQIsign raw limb array format, with correct
two's-complement representation of negatives for ibz_t = uint64_t[IBZ_LIMBS].

Usage: python3 _precomp_convert.py <input> <output> <ibz_limbs>
"""
import re
import sys

def convert_file(input_path, output_path, ibz_limbs):
    with open(input_path, 'r') as f:
        src = f.read()

    # Match every GMP designator block of the form:
    #   #if 0
    #   #elif GMP_LIMB_BITS == 16 ... #elif GMP_LIMB_BITS == 32 ...
    #   #elif GMP_LIMB_BITS == 64
    #   {{._mp_alloc = 0, ._mp_size = S, ._mp_d = (mp_limb_t[]) {L0, L1, ...}}}
    #   #endif
    # Replace the entire block with raw limb array literal.

    pattern = re.compile(
        r'#if 0\s*'
        r'(?:#elif GMP_LIMB_BITS == 16\s*\{\{[^}]*?\}\}\}\s*)?'
        r'(?:#elif GMP_LIMB_BITS == 32\s*\{\{[^}]*?\}\}\}\s*)?'
        r'#elif GMP_LIMB_BITS == 64\s*'
        r'\{\{\._mp_alloc = 0, \._mp_size = (-?\d+), \._mp_d = \(mp_limb_t\[\]\)\s*\{([^}]*)\}\}\}\s*'
        r'#endif\s*',
        re.DOTALL
    )

    n_conv = 0
    n_neg = 0
    n_pos = 0
    n_zero = 0

    def replace(m):
        nonlocal n_conv, n_neg, n_pos, n_zero
        n_conv += 1
        size = int(m.group(1))
        limbs_str = m.group(2).strip()
        limbs = []
        for tok in limbs_str.split(','):
            tok = tok.strip()
            if tok:
                limbs.append(int(tok, 0))

        if size == 0:
            n_zero += 1
            return '{0x0}\n'

        abs_count = abs(size)
        if abs_count != len(limbs):
            print(f"WARNING: mp_size abs={abs_count} but {len(limbs)} limbs at conv {n_conv}", file=sys.stderr)
        abs_value = 0
        for i, l in enumerate(limbs):
            abs_value |= l << (64 * i)

        if size < 0:
            n_neg += 1
            # 2's complement on ibz_limbs * 64 bits
            total = ((1 << (64 * ibz_limbs)) - abs_value) & ((1 << (64 * ibz_limbs)) - 1)
        else:
            n_pos += 1
            total = abs_value

        # Split into ibz_limbs limbs
        out = []
        for i in range(ibz_limbs):
            out.append((total >> (64 * i)) & ((1 << 64) - 1))

        # If all limbs are 0xFF...FF, use designated initializer
        if all(l == 0xFFFFFFFFFFFFFFFF for l in out):
            return f'{{[0 ... {ibz_limbs-1}] = 0xffffffffffffffff}}\n'

        # Otherwise strip trailing zeros for shorter output
        while out and out[-1] == 0:
            out.pop()
        if not out:
            out = [0]

        return '{' + ', '.join(f'0x{l:x}' for l in out) + '}\n'

    new_src = pattern.sub(replace, src)

    with open(output_path, 'w') as f:
        f.write(new_src)

    print(f"Converted {n_conv} ibz values: {n_pos} positive, {n_neg} negative, {n_zero} zero")
    print(f"  Output: {output_path}")
    return n_conv, n_neg

if __name__ == '__main__':
    if len(sys.argv) != 4:
        print(__doc__, file=sys.stderr)
        sys.exit(1)
    convert_file(sys.argv[1], sys.argv[2], int(sys.argv[3]))
