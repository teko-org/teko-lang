#!/usr/bin/env python3
# =====================================================================================
# gen_decimal_kats.py — Phase 17.F.1 KAT generator (the Python `decimal` oracle).
#
# Emits tests/runtime/teko_decimal_kat_vectors.h: a static const table of
#   { op, a[256], b[256], expect_ok, result[256] }
# whose operands AND expected results are the EXACT 256-byte little-endian encoding of the
# teko_decimal struct, computed by a Python `decimal.Decimal` oracle that MIRRORS the C
# arithmetic semantics in teko_decimal.c BYTE-FOR-BYTE:
#   * exact compute at high precision (getcontext().prec >= 600),
#   * if the result needs > 38 fractional digits -> quantize to scale 38, ROUND_HALF_EVEN,
#   * a case is expect_fail when the result coefficient would not fit 1984 bits (>= 2**1984)
#     or on div/mod by zero,
#   * results stored UN-NORMALIZED at the rule's natural scale (add/sub = max scale,
#     mul = sa+sb capped at 38, div = 38), matching the C.
#
# Deterministic: a fixed-seed LCG drives all random operands (no os.urandom / random module),
# so re-running regenerates the identical header. Commit BOTH this script and the header.
#
# Usage:  python3 tools/gen_decimal_kats.py > tests/runtime/teko_decimal_kat_vectors.h
# =====================================================================================
import sys
from decimal import Decimal, getcontext, ROUND_HALF_EVEN, localcontext

LIMBS = 31
MAX_SCALE = 38
COEFF_MAX = 1 << (64 * LIMBS)   # 2**1984
getcontext().prec = 600

# Op tags — MUST match the TEKO_DEC_OP_* enum in test_decimal.c.
OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_CMP = range(6)
OP_NAME = {OP_ADD: "ADD", OP_SUB: "SUB", OP_MUL: "MUL",
           OP_DIV: "DIV", OP_MOD: "MOD", OP_CMP: "CMP"}

# --- deterministic LCG (numerical recipes constants) --------------------------------
class LCG:
    def __init__(self, seed):
        self.s = seed & 0xFFFFFFFFFFFFFFFF
    def next(self):
        self.s = (self.s * 6364136223846793005 + 1442695040888963407) & 0xFFFFFFFFFFFFFFFF
        return self.s
    def below(self, n):
        return self.next() % n
    def digits(self, ndigits):
        # a decimal string of exactly ndigits digits (no leading-zero suppression needed;
        # the C from_components accepts leading zeros).
        out = []
        for _ in range(ndigits):
            out.append(chr(ord('0') + self.below(10)))
        return ''.join(out)

# --- 256-byte LE encoding of a (sign, scale, coeff-int) triple ----------------------
def encode(sign, scale, coeff):
    assert 0 <= scale <= MAX_SCALE
    assert 0 <= coeff < COEFF_MAX
    if coeff == 0:
        sign = 0   # canonical zero sign
    b = bytearray(256)
    b[0] = sign & 1
    b[1] = scale
    b[2] = 0
    # _pad[5] already zero
    for i in range(LIMBS):
        limb = (coeff >> (64 * i)) & 0xFFFFFFFFFFFFFFFF
        for j in range(8):
            b[8 + i * 8 + j] = (limb >> (8 * j)) & 0xFF
    return bytes(b)

# A decimal operand expressed as the (sign, scale, coeff) triple the C builds via
# teko_decimal_from_components. value = (-1)^sign * coeff * 10^-scale.
class Op:
    def __init__(self, sign, scale, coeff):
        if coeff == 0:
            sign = 0
        self.sign = sign & 1
        self.scale = scale
        self.coeff = coeff
    def to_decimal(self):
        d = Decimal(self.coeff).scaleb(-self.scale)
        return -d if self.sign else d
    def encode(self):
        return encode(self.sign, self.scale, self.coeff)

# Decompose a Decimal into (sign, coeff-int, exponent). For a value v = c * 10^e.
def decompose(d):
    s, digits, exp = d.as_tuple()
    coeff = 0
    for dd in digits:
        coeff = coeff * 10 + dd
    return s, coeff, exp

# Given an exact Decimal `val` and a TARGET scale, produce (ok, sign, scale, coeff) under the
# C semantics: store at `target_scale`; if the exact value has MORE than target_scale frac
# digits, banker's-round to target_scale; then check the coefficient fits 1984 bits.
def reduce_to_scale(val, target_scale):
    q = Decimal(1).scaleb(-target_scale)
    with localcontext() as ctx:
        ctx.prec = 10000
        r = val.quantize(q, rounding=ROUND_HALF_EVEN)
    sgn, coeff, exp = decompose(r)
    # exp should be -target_scale (quantize forces it); coeff is the integer at that scale.
    # Normalize exp to exactly -target_scale by padding (quantize guarantees this, but be safe).
    if exp > -target_scale:
        coeff *= 10 ** (exp + target_scale)
        exp = -target_scale
    scale = -exp
    if scale < 0 or scale > MAX_SCALE:
        return (False, 0, 0, 0)
    if coeff >= COEFF_MAX:
        return (False, 0, 0, 0)
    return (True, sgn, scale, coeff)

# --- oracle per op (mirrors teko_decimal.c EXACTLY) ---------------------------------
def oracle(op, a, b):
    da, db = a.to_decimal(), b.to_decimal()
    if op == OP_CMP:
        # compare returns -1/0/1; encode the result as a scale-0 decimal in [-1,0,1].
        if da < db:
            c = -1
        elif da > db:
            c = 1
        else:
            c = 0
        sign = 1 if c < 0 else 0
        return (True, Op(sign, 0, abs(c)))
    if op == OP_ADD:
        s = max(a.scale, b.scale)
        ok, sgn, scale, coeff = reduce_to_scale(da + db, s)
    elif op == OP_SUB:
        s = max(a.scale, b.scale)
        ok, sgn, scale, coeff = reduce_to_scale(da - db, s)
    elif op == OP_MUL:
        s = min(a.scale + b.scale, MAX_SCALE)
        ok, sgn, scale, coeff = reduce_to_scale(da * db, s)
    elif op == OP_DIV:
        if db == 0:
            return (False, None)
        ok, sgn, scale, coeff = reduce_to_scale(da / db, MAX_SCALE)
    elif op == OP_MOD:
        if db == 0:
            return (False, None)
        # Mirror teko_decimal_mod EXACTLY: result = a - trunc(a/b)*b, where trunc(a/b) is the
        # integer (scale-0) quotient toward zero. In the C, t has scale 0, so t*b has scale
        # b.scale (<=38, no rounding), and a - (t*b) has the NATURAL scale max(a.scale,b.scale).
        # The result value is exact at that scale; we store it there (NOT at Python's chosen
        # zero-exponent, which would otherwise pick a different scale for a zero remainder).
        with localcontext() as ctx:
            ctx.prec = 10000
            t = (da / db).to_integral_value(rounding='ROUND_DOWN')  # trunc toward zero
            tb = t * db
            r = da - tb
        # C fails if the intermediate t*b coefficient overflows 1984 bits (mul fail-loud).
        _, tb_coeff, _ = decompose(tb)
        if tb_coeff >= COEFF_MAX:
            return (False, None)
        result_scale = min(max(a.scale, b.scale), MAX_SCALE)
        ok, sgn, scale, coeff = reduce_to_scale(r, result_scale)
    else:
        raise ValueError(op)
    if not ok:
        return (False, None)
    return (True, Op(sgn, scale, coeff))

# --- vector construction -------------------------------------------------------------
VECTORS = []  # list of (op, Op a, Op b, expect_ok, result-Op-or-None)

def add_case(op, a, b):
    ok, res = oracle(op, a, b)
    VECTORS.append((op, a, b, ok, res))

def main():
    rng = LCG(0x1F2E3D4C5B6A7988)

    # 1. Hand-picked exact/boundary cases.
    fixed = [
        # add/sub aligning scales
        (OP_ADD, Op(0, 2, 150), Op(0, 0, 0)),     # 1.50 + 0 -> 1.50 (scale 2, un-normalized)
        (OP_ADD, Op(0, 1, 15), Op(0, 2, 25)),     # 1.5 + 0.25 -> 1.75
        (OP_SUB, Op(0, 0, 5), Op(0, 0, 5)),       # 5 - 5 -> 0
        (OP_SUB, Op(0, 0, 3), Op(0, 0, 5)),       # 3 - 5 -> -2
        (OP_ADD, Op(1, 0, 2), Op(0, 0, 2)),       # -2 + 2 -> 0 (sign canonicalized)
        (OP_ADD, Op(0, 38, 1), Op(0, 0, 1)),      # 1e-38 + 1
        # mul scaling
        (OP_MUL, Op(0, 2, 125), Op(0, 1, 2)),     # 1.25 * 0.2 -> 0.250 (scale 3)
        (OP_MUL, Op(0, 20, 3), Op(0, 20, 7)),     # 3e-20 * 7e-20 -> scale 40 -> round to 38
        (OP_MUL, Op(1, 1, 5), Op(0, 1, 5)),       # -0.5 * 0.5 -> -0.25
        # banker's boundary cases via div / mul-round
        (OP_DIV, Op(0, 0, 5), Op(0, 0, 2)),       # 5/2 -> 2.5 -> stored scale 38 = 2.5
        (OP_DIV, Op(0, 0, 1), Op(0, 0, 3)),       # 1/3 -> 0.333...3 (38 places, round even)
        (OP_DIV, Op(0, 0, 2), Op(0, 0, 7)),       # 2/7 -> non-terminating at 38
        (OP_DIV, Op(0, 0, 1), Op(0, 0, 8)),       # 0.125 exact
        (OP_DIV, Op(0, 0, 7), Op(0, 0, 1)),       # 7/1 -> 7.000...0
        (OP_DIV, Op(0, 0, 10), Op(0, 0, 4)),      # 10/4 -> 2.5
        # round-half-to-even ties through mul (scale-cap rounding):
        # 0.0...025 @ extreme scales -> rounds to even at 38
        (OP_MUL, Op(0, 19, 25), Op(0, 19, 1)),    # 25e-19 * 1e-19 = 25e-38 (scale 38 exact)
        (OP_MUL, Op(0, 19, 25), Op(0, 20, 1)),    # 25e-19 * 1e-20 = 25e-39 -> 2e-38? (2.5->2)
        (OP_MUL, Op(0, 19, 35), Op(0, 20, 1)),    # 35e-39 -> 4e-38 (3.5->4)
        # mod (Python semantics)
        (OP_MOD, Op(0, 0, 7), Op(0, 0, 3)),       # 7 % 3 -> 1
        (OP_MOD, Op(1, 0, 7), Op(0, 0, 3)),       # -7 % 3 -> -1 (sign of a)
        (OP_MOD, Op(0, 0, 7), Op(1, 0, 3)),       # 7 % -3 -> 1
        (OP_MOD, Op(0, 1, 75), Op(0, 1, 2)),      # 7.5 % 0.2 -> 0.1
        (OP_MOD, Op(0, 2, 1000), Op(0, 0, 3)),    # 10.00 % 3 -> 1.00
        # cmp
        (OP_CMP, Op(0, 1, 15), Op(0, 2, 150)),    # 1.5 == 1.50
        (OP_CMP, Op(1, 0, 1), Op(0, 0, 1)),       # -1 < 1
        (OP_CMP, Op(0, 0, 0), Op(1, 0, 0)),       # +0 == -0
        (OP_CMP, Op(0, 0, 3), Op(0, 1, 25)),      # 3 > 2.5
        (OP_CMP, Op(1, 0, 3), Op(1, 1, 25)),      # -3 < -2.5
        # div/mod by zero -> expect_fail
        (OP_DIV, Op(0, 0, 5), Op(0, 0, 0)),
        (OP_MOD, Op(0, 0, 5), Op(0, 0, 0)),
        # coefficient overflow -> expect_fail (huge * huge)
        (OP_MUL, Op(0, 0, (10 ** 300)), Op(0, 0, (10 ** 300))),  # ~600 digit product >= 2^1984
        (OP_ADD, Op(0, 0, (10 ** 596) - 1), Op(0, 0, (10 ** 596) - 1)),  # near-limit add
    ]
    for op, a, b in fixed:
        add_case(op, a, b)

    # 2. Every scale 0..38 in an add and a div (exercise alignment + 38-place rounding).
    for sc in range(0, MAX_SCALE + 1):
        a = Op(0, sc, (rng.next() % (10 ** 12)) + 1)
        b = Op(0, (sc * 7) % (MAX_SCALE + 1), (rng.next() % (10 ** 9)) + 1)
        add_case(OP_ADD, a, b)
        add_case(OP_SUB, a, b)
        add_case(OP_CMP, a, b)
        # division by a small nonzero -> scale-38 rounding everywhere
        d = Op(0, sc % 5, (rng.next() % 97) + 1)
        add_case(OP_DIV, a, d)
        add_case(OP_MOD, a, d)
        add_case(OP_MUL, a, d)

    # 3. Random small & near-590-digit operands across all ops.
    for _ in range(120):
        nd_a = 1 + rng.below(40)
        nd_b = 1 + rng.below(40)
        sa = rng.below(MAX_SCALE + 1)
        sb = rng.below(MAX_SCALE + 1)
        sga = rng.below(2)
        sgb = rng.below(2)
        a = Op(sga, sa, int(rng.digits(nd_a)))
        b = Op(sgb, sb, int(rng.digits(nd_b)))
        for op in (OP_ADD, OP_SUB, OP_MUL, OP_CMP):
            add_case(op, a, b)
        # avoid div/mod by zero in the random stream (separate fail cases above)
        if b.coeff != 0:
            add_case(OP_DIV, a, b)
            add_case(OP_MOD, a, b)

    # 4. Near-limit coefficients (~590 digits) for add/sub/cmp (mul would overflow).
    for _ in range(20):
        nd = 580 + rng.below(10)
        a = Op(rng.below(2), rng.below(MAX_SCALE + 1), int(rng.digits(nd)))
        b = Op(rng.below(2), rng.below(MAX_SCALE + 1), int(rng.digits(nd)))
        add_case(OP_ADD, a, b)
        add_case(OP_SUB, a, b)
        add_case(OP_CMP, a, b)

    # 5. Non-terminating divisions in bulk (the Knuth-division safety net).
    for n in range(1, 41):
        for d in (3, 7, 9, 11, 13, 17, 23):
            add_case(OP_DIV, Op(0, 0, n), Op(0, 0, d))

    emit()

def emit():
    out = sys.stdout
    out.write("// AUTO-GENERATED by tools/gen_decimal_kats.py — DO NOT EDIT BY HAND.\n")
    out.write("// Phase 17.F.1 decimal KAT vectors: Python `decimal` oracle (ROUND_HALF_EVEN,\n")
    out.write("// prec>=600), 256-byte little-endian teko_decimal encoding, byte-for-byte.\n")
    out.write("#ifndef TEKO_DECIMAL_KAT_VECTORS_H\n#define TEKO_DECIMAL_KAT_VECTORS_H\n")
    out.write("#include <stdint.h>\n\n")
    out.write("typedef struct {\n")
    out.write("    int      op;          // TEKO_DEC_OP_*\n")
    out.write("    uint8_t  a[256];\n")
    out.write("    uint8_t  b[256];\n")
    out.write("    int      expect_ok;   // 1 = op returns 1 (and result matches); 0 = fail-loud\n")
    out.write("    uint8_t  result[256]; // expected 256-byte encoding when expect_ok (else zeros)\n")
    out.write("    const char* label;\n")
    out.write("} teko_decimal_kat;\n\n")

    def barr(b):
        return "{" + ",".join(str(x) for x in b) + "}"

    out.write("static const teko_decimal_kat TEKO_DECIMAL_KATS[] = {\n")
    n_ok = 0
    n_fail = 0
    for (op, a, b, ok, res) in VECTORS:
        ae = a.encode()
        be = b.encode()
        if ok:
            re = res.encode()
            n_ok += 1
        else:
            re = bytes(256)
            n_fail += 1
        label = "%s s%d/s%d" % (OP_NAME[op], a.scale, b.scale)
        out.write("  { %d, %s, %s, %d, %s, \"%s\" },\n" %
                  (op, barr(ae), barr(be), 1 if ok else 0, barr(re), label))
    out.write("};\n\n")
    out.write("#define TEKO_DECIMAL_KAT_COUNT (sizeof(TEKO_DECIMAL_KATS)/sizeof(TEKO_DECIMAL_KATS[0]))\n")
    out.write("#endif // TEKO_DECIMAL_KAT_VECTORS_H\n")
    sys.stderr.write("generated %d vectors (%d ok, %d expect_fail)\n" %
                     (len(VECTORS), n_ok, n_fail))

if __name__ == "__main__":
    main()
