#include "detectors.hpp"

/* File flow: exact failure-cache lookup -> build per-unit footprints ->
 * enumerate small combinations -> union useful removals -> return actions.
 * The common abstraction is "N selected items cover exactly N opposite items":
 * naked subsets select cells and cover digits; hidden subsets do the reverse.
 */
namespace sudoku {

int SubsetCache::slot(Strategy strategy) {
    // The full strategy IDs are sparse here; map the five supported subset
    // checks to dense cache rows. Other strategies do not share these entries.
    switch (strategy) {
        case Strategy::NakedPair: return 0;
        case Strategy::HiddenPair: return 1;
        case Strategy::NakedTriple: return 2;
        case Strategy::HiddenTriple: return 3;
        case Strategy::NakedQuad: return 4;
        default: throw std::invalid_argument("unsupported subset strategy");
    }
}

bool SubsetCache::skip(const State& state, Strategy strategy, int unit) {
    // Why just nine cells suffice: every tested pattern and every elimination
    // of a Subset check stays inside this unit. Its P entries are determined by
    // those same cell masks, provided State's synchronization invariant holds.
    auto& entry = entries_[slot(strategy)][unit];
    std::array<uint16_t, 9> signature{};
    for (int position = 0; position < 9; ++position) {
        const int cell = topology.units[unit][position];
        // Pack all inputs into 13 bits: lower 9 = candidates, upper 4 = value.
        // This is an exact snapshot, not a hash with a risk of collisions.
        signature[position] = static_cast<uint16_t>(
            (state.values[cell] << 9) | state.candidates[cell].mask());
    }
    // Reuse ONLY a failure on identical input. If any value/candidate differs,
    // replace the snapshot and require a fresh scan; finish() records its result.
    // An assignment elsewhere may propagate into this unit; that changes the
    // signature and forces a scan too. We never infer validity just from the
    // coordinates of the last filled cell or from a potentially colliding hash.
    if (entry.empty && entry.signature == signature) return true;
    entry.signature = signature;
    entry.empty = false;
    return false;
}

void SubsetCache::finish(Strategy strategy, int unit, bool empty) {
    // A unit with even one elimination is not cacheable as a failed check.
    // Positive results would need fresh justification after applying a batch.
    entries_[slot(strategy)][unit].empty = empty;
}

// Unified answer-only scan for Naked/Hidden Pair, Triple and Naked Quad.
// Naked example: two cells restricted to {2,7} reserve those digits, so remove
// 2 and 7 from other cells in the unit. Hidden example: digits 2 and 7 occur
// only in two cells, so remove other candidates from those two cells instead.
Actions find_subset_actions(const State& state, Strategy strategy,
                            SubsetCache& cache) {
    // Supported sizes are Pair=2, Triple=3, and Naked Quad=4. Hidden Quad is
    // deliberately absent, matching the paper's fixed fifteen-strategy set.
    const bool hidden = strategy == Strategy::HiddenPair || strategy == Strategy::HiddenTriple;
    const bool triple = strategy == Strategy::NakedTriple || strategy == Strategy::HiddenTriple;
    const int size = strategy == Strategy::NakedQuad ? 4 : (triple ? 3 : 2);
    std::array<uint16_t, 81> eliminations{};
    for (int unit_index = 0; unit_index < 27; ++unit_index) {
        if (cache.skip(state, strategy, unit_index)) continue;
        const auto& unit = topology.units[unit_index];
        std::array<uint16_t, 9> footprints{};
        std::array<int, 9> eligible{};
        int eligible_count = 0;
        // Naked subsets join candidate digits of cells; hidden subsets join
        // candidate positions of digits. Both seek a union of exactly size bits.
        for (int item = 0; item < 9; ++item) {
            // item is a cell position for naked subsets, a digit-1 for hidden
            // subsets. footprints always supplies the opposite side of that view.
            footprints[item] = hidden ? state.position_index[unit_index][item]
                                      : state.candidates[unit[item]].mask();
            const int count = __builtin_popcount(footprints[item]);
            // Keep the frozen eligibility rule: footprints contain 2..size
            // items. A naked footprint must also belong to an unfilled cell.
            // This bounds combination search before considering unions.
            if ((hidden || !state.values[unit[item]]) && count >= 2 && count <= size)
                eligible[eligible_count++] = item;
        }
        bool unit_has_action = false;
        // Worked Pair examples (all positions are zero-based within this unit):
        //   Naked: cells at positions 0 and 3 both have candidates {2,7}.
        //          chosen = bits {0,3}; joined = digit bits {1,6}.
        //          Remove 2/7 OUTSIDE positions 0/3.
        //   Hidden: digits 2 and 7 occur only at positions 0 and 3.
        //           chosen = digit bits {1,6}; joined = position bits {0,3}.
        //           Remove OTHER digits INSIDE positions 0/3.
        const auto emit = [&](uint16_t chosen, uint16_t joined) {
            // Swap the interpretation back to explicit cell and digit masks.
            // Hidden subsets restrict their cells; naked subsets exclude their
            // digits elsewhere. OR-ing removals deduplicates overlapping proofs.
            const uint16_t cells = hidden ? joined : chosen;
            const uint16_t digits = hidden ? chosen : joined;
            for (int position = 0; position < 9; ++position) {
                const int cell = unit[position];
                const bool in_group = cells & (1u << position);
                // The two modes reverse their target region. Filled cells have
                // no candidates by invariant; naked mode explicitly skips them.
                if (hidden ? !in_group : (in_group || state.values[cell])) continue;
                const uint16_t removed = static_cast<uint16_t>(hidden
                    ? state.candidates[cell].mask() & ~digits
                    : state.candidates[cell].mask() & digits);
                if (removed) {
                    // Count any currently useful removal, even if another unit
                    // already emitted it into eliminations. This unit's result
                    // was successful, so its cache entry must not record failure.
                    eliminations[cell] |= removed;
                    unit_has_action = true;
                }
            }
        };
        // Enumerate combinations of size 2, 3, or 4 in increasing item order.
        // This is pattern enumeration: no candidate is guessed or committed.
        // chosen = selected items; joined = union of their footprints.
        // Parameters:
        //   offset: first eligible array index still available to select;
        //   depth:  number of selected items, NOT Sudoku solving depth;
        //   chosen: bits of original item IDs (not eligible-array offsets);
        //   joined: OR of those items' candidate/position footprints.
        const auto search = [&](const auto& self, int offset, int depth,
                                uint16_t chosen, uint16_t joined) -> void {
            if (depth == size) {
                // Equality matters: a smaller union is not accepted as a normal
                // subset of this size, and an oversized union was pruned earlier.
                if (__builtin_popcount(joined) == size) emit(chosen, joined);
                return;
            }
            for (int next = offset; next <= eligible_count - (size - depth); ++next) {
                // Leave enough later items to finish the combination. Advancing
                // to next+1 avoids permutations such as checking {a,b} and {b,a}.
                const int item = eligible[next];
                const auto expanded = static_cast<uint16_t>(joined | footprints[item]);
                // A union can only grow, so an oversized group cannot recover.
                if (__builtin_popcount(expanded) <= size)
                    self(self, next + 1, depth + 1,
                         static_cast<uint16_t>(chosen | (1u << item)), expanded);
            }
        };
        search(search, 0, 0, 0, 0);
        // Cache failure only after every eligible combination in this unit has
        // been examined. A partly searched unit cannot justify skipping later.
        cache.finish(strategy, unit_index, !unit_has_action);
    }
    // A geometrically valid pattern with nothing left to remove returns no
    // action. Finding such a pattern alone must not start another solve round.
    return actions_from_masks(eliminations);
}

} // namespace sudoku
