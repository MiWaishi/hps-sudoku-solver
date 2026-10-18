#include "detectors.hpp"
#include "cell_mask.hpp"

/* Three geometric families, all read-only:
 *   Intersections -> one digit confined to a box/line intersection, using P.
 *   Fish          -> one digit aligned across several rows/columns, using P.
 *   Wings         -> two/three-candidate cells plus shared visibility, using M.
 * Every family accumulates removal bits and converts them only after its scan.
 */
namespace sudoku {

// Return the global box UNIT ID 18..26, not the local box number 0..8.
static int cell_box(int cell) {
    return 18 + (cell / 27) * 3 + (cell % 9) / 3;
}

// Answer-only Pointing/Claiming scan using the maintained position index.
// Pointing: if a box's positions for digit d all lie in one row/column, remove
// d from the rest of that line. Claiming reverses the relation: positions in a
// row/column confined to one box exclude d from the rest of that box.
Actions find_intersection_actions(const State& state, Strategy strategy) {
    // Example: box 1 allows digit 7 only at r1c1/r1c2. Both are in row 1,
    // so 7 is excluded from r1c4..r1c9 wherever it is still a candidate.
    // Conversely, if row 1 allows 7 only inside box 1, Claiming removes 7
    // from the other rows of box 1. The target is outside the source unit.
    const bool pointing = strategy == Strategy::Pointing;
    std::array<uint16_t, 81> eliminations{};
    // Source units are boxes for Pointing, rows/columns for Claiming. Match the
    // frozen detector's pair/triple scope by accepting exactly 2 or 3 positions.
    for (int source = pointing ? 18 : 0; source < (pointing ? 27 : 18); ++source)
        for (int digit = 1; digit <= 9; ++digit) {
            const auto cells = state.candidate_positions(source, digit);
            if (cells.size() != 2 && cells.size() != 3) continue;
            const int first = cells[0];
            // Compare all candidate positions with one representative. The
            // first cell is not a preferred solution; only common membership
            // of the entire position set matters to the confinement proof.
            bool same_row = true, same_column = true, same_box = true;
            for (int cell : cells) {
                same_row &= cell / 9 == first / 9;
                same_column &= cell % 9 == first % 9;
                same_box &= cell_box(cell) == cell_box(first);
            }
            std::array<int, 2> targets{};
            // At most a row and a column can be target lines for Pointing;
            // Claiming has just its containing box. No dynamic target list.
            int target_count = 0;
            if (pointing) {
                if (same_row) targets[target_count++] = first / 9;
                if (same_column) targets[target_count++] = 9 + first % 9;
            } else if (same_box) {
                targets[target_count++] = cell_box(first);
            }
            // The source cells establish confinement and must remain intact.
            // Search the rest of the target unit for candidates to remove.
            for (int index = 0; index < target_count; ++index)
                for (int cell : state.candidate_positions(targets[index], digit)) {
                    const bool in_source = pointing ? cell_box(cell) == source :
                        (source < 9 ? cell / 9 == source : 9 + cell % 9 == source);
                    if (!in_source)
                        eliminations[cell] |= static_cast<uint16_t>(1u << (digit - 1));
                }
        }
    return actions_from_masks(eliminations);
}

// Unified answer-only X-Wing/Swordfish scan.
// For one digit, choose N base rows whose candidate columns have a union of N
// positions (N=2 for X-Wing, N=3 for Swordfish). Those columns must place the
// digit in the selected rows, so eliminate it from other rows in those columns.
Actions find_fish_actions(const State& state, Strategy strategy) {
    // Variables always use positions within a dimension:
    //   base      = row index (or column index in the transposed scan);
    //   footprints[base] = candidate cover columns (or cover rows);
    //   chosen    = mask of selected base indices;
    //   covers    = union of their footprints, not a mask of board cell IDs.
    const int size = strategy == Strategy::XWing ? 2 : 3;
    std::array<uint16_t, 81> eliminations{};
    // Direction 0 selects base rows and covering columns; 1 swaps their roles.
    for (int direction = 0; direction < 2; ++direction)
        for (int digit = 1; digit <= 9; ++digit) {
            std::array<uint16_t, 9> footprints{};
            std::array<int, 9> eligible{};
            int eligible_count = 0;
            for (int base = 0; base < 9; ++base) {
                // Convert each base unit's candidate cells into positions along
                // the covering dimension, so different bases can be OR-ed.
                for (int cell : state.candidate_positions(direction * 9 + base, digit))
                    footprints[base] |= static_cast<uint16_t>(
                        1u << (direction == 0 ? cell % 9 : cell / 9));
                const int count = __builtin_popcount(footprints[base]);
                // A base with more than N positions cannot fit an N-cover fish.
                // The detector keeps the frozen 2..N footprint eligibility rule.
                if (count >= 2 && count <= size) eligible[eligible_count++] = base;
            }
            const auto emit = [&](uint16_t chosen, uint16_t covers) {
                // chosen marks base units; covers marks perpendicular units.
                // Example: rows 1 and 5 both allow d only in columns 2 and 8.
                // Remove d from columns 2/8 outside rows 1/5, not from corners.
                // For Swordfish, three bases may have different two/three-bit
                // footprints as long as their UNION has exactly three positions;
                // requiring identical footprints would miss valid patterns.
                for (int cover = 0; cover < 9; ++cover) {
                    if (!(covers & (1u << cover))) continue;
                    const int cover_unit = (direction == 0 ? 9 : 0) + cover;
                    // Query the perpendicular unit's P directly, so cells not
                    // containing this digit are never materialized as targets.
                    for (int cell : state.candidate_positions(cover_unit, digit)) {
                        const int base = direction == 0 ? cell / 9 : cell % 9;
                        if (!(chosen & (1u << base)))
                            eliminations[cell] |= static_cast<uint16_t>(1u << (digit - 1));
                    }
                }
            };
            const auto search = [&](const auto& self, int offset, int depth,
                                    uint16_t chosen, uint16_t covers) -> void {
                // Select exactly size bases. The union may grow but never
                // shrink; more than size cover positions cannot form this fish.
                if (depth == size) {
                    // N independent base units must place N copies of the digit
                    // into these N covering units. Other bases cannot use them.
                    if (__builtin_popcount(covers) == size) emit(chosen, covers);
                    return;
                }
                for (int next = offset; next <= eligible_count - (size - depth); ++next) {
                    const int base = eligible[next];
                    const auto expanded = static_cast<uint16_t>(covers | footprints[base]);
                    if (__builtin_popcount(expanded) <= size)
                        self(self, next + 1, depth + 1,
                             static_cast<uint16_t>(chosen | (1u << base)), expanded);
                }
            };
            search(search, 0, 0, 0, 0);
        }
    return actions_from_masks(eliminations);
}

// Y-Wing: pivot {a,b}, wings {a,c} and {b,c}; at least one wing holds c.
// XYZ-Wing: pivot {a,b,c}, wings {a,c} and {b,c}; at least one of all THREE
// cells holds c. Therefore XYZ targets must see the pivot as well as both wings.
Actions find_wing_actions(const State& state, Strategy strategy) {
    // Hand-check Y-Wing with pivot {1,2}, wing A {1,3}, wing B {2,3}:
    // pivot=1 forces A=3; pivot=2 forces B=3. A cell seeing both A/B loses 3.
    // With XYZ pivot {1,2,3}, pivot=3 is a third possibility, so that target
    // must ALSO see the pivot. No adjacency between the two wings is required.
    const bool xyz = strategy == Strategy::XYZWing;
    std::array<uint16_t, 81> eliminations{};
    for (int pivot = 0; pivot < 81; ++pivot) {
        const unsigned pivot_mask = state.candidates[pivot].mask();
        if (state.values[pivot] || __builtin_popcount(pivot_mask) != (xyz ? 3 : 2)) continue;
        Cells visible;
        // Both wings must see the pivot and each must have two candidates.
        // The unordered pair loop tests each wing pair once for this pivot.
        for (int cell : topology.peers[pivot])
            if (!state.values[cell] && state.candidates[cell].size() == 2)
                visible.push_back(cell);
        for (size_t left = 0; left < visible.size(); ++left)
            for (size_t right = left + 1; right < visible.size(); ++right) {
                const int a = visible[left], b = visible[right];
                const unsigned a_mask = state.candidates[a].mask();
                const unsigned b_mask = state.candidates[b].mask();
                // outer is a DIGIT mask for the elimination digit, not a mask
                // of target cells. The later peer intersection supplies targets.
                unsigned outer = 0;
                if (xyz) {
                    // Wing digits must be subsets of the three pivot digits,
                    // jointly cover them, and share exactly one elimination digit.
                    if ((a_mask & ~pivot_mask) || (b_mask & ~pivot_mask) ||
                        (a_mask | b_mask) != pivot_mask) continue;
                    outer = a_mask & b_mask;
                    if (__builtin_popcount(outer) != 1) continue;
                } else {
                    // Each wing shares a different pivot digit. Its other digit
                    // must be the same single c in both wings; c is outside pivot.
                    const unsigned shared_a = a_mask & pivot_mask;
                    const unsigned shared_b = b_mask & pivot_mask;
                    if (__builtin_popcount(shared_a) != 1 ||
                        __builtin_popcount(shared_b) != 1 || shared_a == shared_b) continue;
                    outer = a_mask & ~pivot_mask;
                    if (__builtin_popcount(outer) != 1 || outer != (b_mask & ~pivot_mask)) continue;
                }
                // Intersect precomputed peer masks rather than constructing
                // temporary sets of cells that can see the possible holders.
                auto targets = detail::intersect(
                    detail::peer_masks[a], detail::peer_masks[b]);
                if (xyz) targets = detail::intersect(
                    targets, detail::peer_masks[pivot]);
                const int digit = __builtin_ctz(outer) + 1;
                // outer has exactly one bit after the tests above. Do not erase
                // candidates in the pattern cells themselves or absent candidates.
                detail::visit_cells(targets, [&](int cell) {
                    if (cell != pivot && cell != a && cell != b &&
                        state.candidates[cell].count(digit))
                        eliminations[cell] |= static_cast<uint16_t>(outer);
                });
            }
    }
    return actions_from_masks(eliminations);
}

} // namespace sudoku
