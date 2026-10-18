#pragma once
#include <array>
#include <cstdint>

namespace sudoku::detail {
/* Board-wide set algebra for graph detectors:
 *   with(S,c)       -> S union {c}
 *   intersect(A,B)  -> cells common to A and B
 *   without(A,B)    -> cells in A but not B
 * None of these helpers changes candidates or proves a Sudoku rule by itself.
 * Their inputs contain only cell bits 0..80; the unused high bits stay zero.
 */
// Two machine words cover the board's 81 cells; bit operations avoid cell sets.
// Unlike DigitSet (nine digits) or PositionRange (nine unit positions), this
// mask addresses the whole board: low holds cells 0..63, high holds 64..80.
struct CellMask {
    uint64_t low = 0; // Global cell c uses bit c when c < 64.
    uint64_t high = 0; // Global cell c uses bit c-64 otherwise; cell 80 is bit 16.
};

inline CellMask bit(int cell) {
    // Subtract 64 for the high word, so no shift uses a count of 64 or more.
    // Internal callers supply a valid board cell in the range 0..80.
    return cell < 64 ? CellMask{1ull << cell, 0} : CellMask{0, 1ull << (cell - 64)};
}

// Helpers return new masks by value. Recursive pattern searches can therefore
// keep separate visited sets without editing the parent path or copying a set.
inline CellMask with(CellMask mask, int cell) {
    const auto value = bit(cell);
    mask.low |= value.low;
    mask.high |= value.high;
    return mask;
}

inline bool contains(CellMask mask, int cell) {
    const auto value = bit(cell);
    return ((mask.low & value.low) | (mask.high & value.high)) != 0;
}

inline CellMask intersect(CellMask left, CellMask right) {
    // Intersect the two words independently; no cell-to-word conversion is
    // needed once both sets are encoded in this common board coordinate system.
    return {left.low & right.low, left.high & right.high};
}

inline CellMask without(CellMask source, CellMask removed) {
    // ~removed alone sets unused bits, but AND with source cannot create any
    // new bit. This is safe while source obeys the 81-cell input invariant.
    return {source.low & ~removed.low, source.high & ~removed.high};
}

// peer_masks[cell] is the same 20-peer relation as topology.peers[cell], encoded
// for fast intersections such as "cells that see both wing cells".
extern const std::array<CellMask, 81> peer_masks;

template<class Visit>
inline void visit_cells(CellMask mask, const Visit& visit) {
    // ctzll is used only inside a nonzero-word loop. Low-word cells come first,
    // then high-word cells, giving deterministic ascending board-cell order.
    // mask is a local copy. A callback may accumulate output eliminations
    // without changing which cells this particular traversal will visit.
    while (mask.low) {
        const int cell = __builtin_ctzll(mask.low);
        visit(cell);
        mask.low &= mask.low - 1;
    }
    while (mask.high) {
        const int cell = 64 + __builtin_ctzll(mask.high);
        visit(cell);
        mask.high &= mask.high - 1;
    }
}

// Used instead of converting the set to a vector merely to ask if it is empty.
inline bool any(CellMask mask) { return mask.low || mask.high; }
} // namespace sudoku::detail
