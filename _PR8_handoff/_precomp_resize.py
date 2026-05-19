#!/usr/bin/env python3
"""Resize compact-SQIsign precomp raw-limb arrays from one IBZ_LIMBS to another.

Each ibz value in the compact format is one of:
  {0x0}                                       -- zero
  {[0 ... N-1] = 0xffffffffffffffff}          -- -1 (all-ones) where N=old_limbs
  {limb0, limb1, ...}                         -- explicit limbs (trailing zeros may be omitted)

Conversion: parse → reconstruct signed integer using old_limbs 2's-complement (sign bit
at limb[old_limbs-1] MSB) → re-serialize as new_limbs 2's-complement.

Warns + exits non-zero if any value would lose information (significant bits exceed
new_limbs * 64 - 1, leaving no room for the sign bit).

Usage: python3 _precomp_resize.py <input.c> <output.c> <old_limbs> <new_limbs>
"""
import re, sys

def main():
    if len(sys.argv) != 5:
        print(__doc__, file=sys.stderr); sys.exit(1)
    inp, outp, old_n, new_n = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
    src = open(inp).read()
    OLD_MASK = (1 << (64 * old_n)) - 1
    OLD_SIGN_BIT = 1 << (64 * old_n - 1)
    NEW_MASK = (1 << (64 * new_n)) - 1
    NEW_BITS = 64 * new_n

    n_total = n_resized = n_lost = 0

    # Pattern 1: designated all-ones initializer  {[0 ... K] = 0xffffffffffffffff}
    designated_pat = re.compile(r'\{\[0 \.\.\. (\d+)\] = 0x[fF]{16}\}')
    def repl_designated(m):
        nonlocal n_total, n_resized
        n_total += 1
        k = int(m.group(1)) + 1
        if k != old_n:
            return m.group(0)  # not this format
        n_resized += 1
        # value is -1 → all-ones in new size
        return f'{{[0 ... {new_n-1}] = 0xffffffffffffffff}}'

    src = designated_pat.sub(repl_designated, src)

    # Pattern 2: explicit limb list {0x..., 0x..., ...} or {0x0}
    limb_pat = re.compile(r'\{((?:\s*0x[0-9a-fA-F]+\s*,?\s*)+)\}')
    def repl_limbs(m):
        nonlocal n_total, n_resized, n_lost
        n_total += 1
        toks = [t.strip() for t in m.group(1).split(',') if t.strip()]
        limbs = [int(t, 0) for t in toks]
        # extend to old_n limbs by zeros (compact format may omit trailing zeros)
        while len(limbs) < old_n:
            limbs.append(0)
        # reconstruct integer
        val = 0
        for i, l in enumerate(limbs):
            val |= (l & ((1<<64)-1)) << (64*i)
        if val & OLD_SIGN_BIT:
            val -= 1 << (64 * old_n)
        # check fit
        if val >= 0:
            if val.bit_length() > NEW_BITS - 1:
                n_lost += 1
                print(f"LOSS: positive value {val.bit_length()} bits > {NEW_BITS-1}", file=sys.stderr)
        else:
            if (-val).bit_length() > NEW_BITS - 1:
                n_lost += 1
                print(f"LOSS: negative value {(-val).bit_length()} bits > {NEW_BITS-1}", file=sys.stderr)
        # re-serialize
        repr_val = val & NEW_MASK
        new_limbs = [(repr_val >> (64*i)) & ((1<<64)-1) for i in range(new_n)]
        # all-ones?
        if all(l == (1<<64)-1 for l in new_limbs):
            n_resized += 1
            return f'{{[0 ... {new_n-1}] = 0xffffffffffffffff}}'
        # strip trailing zeros for terseness
        while len(new_limbs) > 1 and new_limbs[-1] == 0:
            new_limbs.pop()
        n_resized += 1
        return '{' + ', '.join(f'0x{l:x}' for l in new_limbs) + '}'

    src = limb_pat.sub(repl_limbs, src)

    open(outp, 'w').write(src)
    print(f"converted {n_resized}/{n_total} ibz blocks, {n_lost} lossy (sign/precision loss)")
    sys.exit(1 if n_lost else 0)

if __name__ == '__main__':
    main()
