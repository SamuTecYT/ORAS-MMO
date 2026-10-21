#include "Lzma.hpp"

// Decodificador LZMA1 según la especificación de dominio público de Igor Pavlov (LzmaSpec.cpp): mismo modelo de
// probabilidades, mismos estados y mismo orden de bits. Ver Lzma.hpp.
namespace Lzma {

namespace {

const int kNumBitModelTotalBits = 11, kNumMoveBits = 5;
const uint32_t kBitModelTotal = 1u << kNumBitModelTotalBits, kTopValue = 1u << 24;
const int kNumStates = 12, kNumPosBitsMax = 4, kNumLenToPosStates = 4, kNumAlignBits = 4;
const int kStartPosModelIndex = 4, kEndPosModelIndex = 14, kNumFullDistances = 1 << (kEndPosModelIndex >> 1);
const int kMatchMinLen = 2;

// Distribución de las probabilidades dentro del búfer
const int kLenChoice = 0, kLenChoice2 = 1, kLenLow = 2, kLenMid = kLenLow + (16 << 3), kLenHigh = kLenMid + (16 << 3),
          kNumLenProbs = kLenHigh + 256;
const int P_IS_MATCH = 0;
const int P_IS_REP = P_IS_MATCH + (kNumStates << kNumPosBitsMax);
const int P_IS_REP_G0 = P_IS_REP + kNumStates;
const int P_IS_REP_G1 = P_IS_REP_G0 + kNumStates;
const int P_IS_REP_G2 = P_IS_REP_G1 + kNumStates;
const int P_IS_REP0_LONG = P_IS_REP_G2 + kNumStates;
const int P_POS_SLOT = P_IS_REP0_LONG + (kNumStates << kNumPosBitsMax);
const int P_POS = P_POS_SLOT + (kNumLenToPosStates << 6);
const int P_ALIGN = P_POS + (1 + kNumFullDistances - kEndPosModelIndex);
const int P_LEN = P_ALIGN + (1 << kNumAlignBits);
const int P_REP_LEN = P_LEN + kNumLenProbs;
const int P_LIT = P_REP_LEN + kNumLenProbs;
static_assert(P_LIT == (int)Lzma::BASE_PROBS, "Lzma::BASE_PROBS no coincide con la tabla de probabilidades");

struct RangeDec {
    const uint8_t *p, *end;
    uint32_t range, code;
    bool bad;
    inline uint8_t Byte() {
        if (p < end) return *p++;
        bad = true;                                    // se acabó la entrada: flujo corrupto
        return 0;
    }
    inline void Norm() {
        if (range < kTopValue) { range <<= 8; code = (code << 8) | Byte(); }
    }
    inline unsigned Bit(uint16_t *prob) {
        uint32_t v = *prob, bound = (range >> kNumBitModelTotalBits) * v;
        unsigned b;
        if (code < bound) { v += (kBitModelTotal - v) >> kNumMoveBits; range = bound; b = 0; }
        else { v -= v >> kNumMoveBits; code -= bound; range -= bound; b = 1; }
        *prob = (uint16_t)v;
        Norm();
        return b;
    }
    uint32_t Direct(unsigned n) {
        uint32_t res = 0;
        do {
            range >>= 1;
            code -= range;
            uint32_t t = 0u - (code >> 31);
            code += range & t;
            if (code == range) bad = true;
            Norm();
            res = (res << 1) + (t + 1);
        } while (--n);
        return res;
    }
};

inline unsigned Tree(RangeDec &rc, uint16_t *probs, unsigned numBits) {
    unsigned m = 1;
    for (unsigned i = 0; i < numBits; i++) m = (m << 1) + rc.Bit(&probs[m]);
    return m - (1u << numBits);
}

inline unsigned TreeReverse(RangeDec &rc, uint16_t *probs, unsigned numBits) {
    unsigned m = 1, sym = 0;
    for (unsigned i = 0; i < numBits; i++) {
        unsigned bit = rc.Bit(&probs[m]);
        m = (m << 1) + bit;
        sym |= bit << i;
    }
    return sym;
}

inline unsigned Len(RangeDec &rc, uint16_t *L, unsigned posState) {
    if (!rc.Bit(&L[kLenChoice])) return Tree(rc, &L[kLenLow + (posState << 3)], 3);
    if (!rc.Bit(&L[kLenChoice2])) return 8 + Tree(rc, &L[kLenMid + (posState << 3)], 3);
    return 16 + Tree(rc, &L[kLenHigh], 8);
}

} // namespace

uint32_t ProbCount(int lc, int lp) { return (uint32_t)P_LIT + (0x300u << (lc + lp)); }

int32_t Decode(const uint8_t *src, uint32_t srcLen, uint8_t *dst, uint32_t dstLen, int lc, int lp, int pb, uint16_t *probs) {
    if (!src || !dst || !probs || lc < 0 || lc > 8 || lp < 0 || lp > 4 || pb < 0 || pb > 4 || srcLen < 5) return -1;
    const uint32_t n = ProbCount(lc, lp);
    for (uint32_t i = 0; i < n; i++) probs[i] = (uint16_t)(kBitModelTotal >> 1);
    RangeDec rc;
    rc.p = src; rc.end = src + srcLen; rc.range = 0xFFFFFFFFu; rc.code = 0; rc.bad = false;
    if (rc.Byte() != 0) return -1;
    for (int i = 0; i < 4; i++) rc.code = (rc.code << 8) | rc.Byte();
    if (rc.code == rc.range) return -1;

    const uint32_t pbMask = (1u << pb) - 1, lpMask = (1u << lp) - 1;
    uint32_t pos = 0, rep0 = 0, rep1 = 0, rep2 = 0, rep3 = 0;
    unsigned state = 0;
    while (pos < dstLen) {
        if (rc.bad) return -1;
        const unsigned posState = pos & pbMask;
        if (!rc.Bit(&probs[P_IS_MATCH + (state << kNumPosBitsMax) + posState])) {
            // literal
            unsigned prev = pos ? dst[pos - 1] : 0;
            uint16_t *lit = &probs[P_LIT + 0x300u * (((pos & lpMask) << lc) + (prev >> (8 - lc)))];
            unsigned sym = 1;
            if (state >= 7) {
                if (rep0 >= pos) return -1;
                unsigned matchByte = dst[pos - rep0 - 1];
                do {
                    unsigned matchBit = (matchByte >> 7) & 1;
                    matchByte <<= 1;
                    unsigned bit = rc.Bit(&lit[((1 + matchBit) << 8) + sym]);
                    sym = (sym << 1) | bit;
                    if (matchBit != bit) break;
                } while (sym < 0x100);
            }
            while (sym < 0x100) sym = (sym << 1) | rc.Bit(&lit[sym]);
            dst[pos++] = (uint8_t)(sym - 0x100);
            state = state < 4 ? 0 : state < 10 ? state - 3 : state - 6;
            continue;
        }
        unsigned len;
        if (rc.Bit(&probs[P_IS_REP + state])) {
            if (!pos) return -1;
            if (!rc.Bit(&probs[P_IS_REP_G0 + state])) {
                if (!rc.Bit(&probs[P_IS_REP0_LONG + (state << kNumPosBitsMax) + posState])) {
                    // "short rep": un byte a la distancia rep0
                    if (rep0 >= pos) return -1;
                    state = state < 7 ? 9 : 11;
                    dst[pos] = dst[pos - rep0 - 1];
                    pos++;
                    continue;
                }
            } else {
                uint32_t dist;
                if (!rc.Bit(&probs[P_IS_REP_G1 + state])) dist = rep1;
                else {
                    if (!rc.Bit(&probs[P_IS_REP_G2 + state])) dist = rep2;
                    else { dist = rep3; rep3 = rep2; }
                    rep2 = rep1;
                }
                rep1 = rep0;
                rep0 = dist;
            }
            len = Len(rc, &probs[P_REP_LEN], posState);
            state = state < 7 ? 8 : 11;
        } else {
            rep3 = rep2; rep2 = rep1; rep1 = rep0;
            len = Len(rc, &probs[P_LEN], posState);
            state = state < 7 ? 7 : 10;
            // distancia
            unsigned lenState = len < (unsigned)(kNumLenToPosStates - 1) ? len : (unsigned)(kNumLenToPosStates - 1);
            unsigned slot = Tree(rc, &probs[P_POS_SLOT + (lenState << 6)], 6);
            uint32_t dist;
            if (slot < (unsigned)kStartPosModelIndex) dist = slot;
            else {
                unsigned nd = (slot >> 1) - 1;
                dist = (2u | (slot & 1)) << nd;
                if (slot < (unsigned)kEndPosModelIndex) dist += TreeReverse(rc, &probs[P_POS + dist - slot], nd);
                else {
                    dist += rc.Direct(nd - kNumAlignBits) << kNumAlignBits;
                    dist += TreeReverse(rc, &probs[P_ALIGN], kNumAlignBits);
                }
            }
            if (dist == 0xFFFFFFFFu) return -1;        // marca de fin antes de llenar la salida
            rep0 = dist;
        }
        len += kMatchMinLen;
        if (rep0 >= pos || len > dstLen - pos) return -1;
        const uint8_t *from = dst + pos - rep0 - 1;
        uint8_t *to = dst + pos;
        for (unsigned i = 0; i < len; i++) to[i] = from[i];   // byte a byte: la copia puede solaparse (rep0 < len)
        pos += len;
    }
    return rc.bad ? -1 : (int32_t)dstLen;
}

} // namespace Lzma
