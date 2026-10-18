#include "detectors.hpp"

namespace sudoku {

// Every advanced detector accumulates eliminations in the same form: for each
// of 81 cells, a nine-bit set of digits to remove. This conversion is postponed
// until the complete scan ends, avoiding one action allocation for every proof.
Actions actions_from_masks(const std::array<uint16_t, 81>& eliminations) {
    // These are eliminations only. Singles build assignments separately below.
    Actions actions;
    for (int cell = 0; cell < 81; ++cell)
        for (int digit = 1; digit <= 9; ++digit)
            if (eliminations[cell] & (1u << (digit - 1)))
                actions.push_back({false, cell, digit});
    return actions;
}

static Actions find_single_actions(const State& state, Strategy strategy) {
    // Both methods return ASSIGNMENTS. Naked Single counts digits per cell;
    // Hidden Single counts cells per (unit,digit). The names describe where
    // the uniqueness is visible, not whether the resulting action differs.
    Actions actions;
    if (strategy == Strategy::NakedSingle) {
        // Cell view: one remaining digit in M(cell) determines that cell's value.
        // Example: M(r1c4)={3} -> {assign=true, cell=3, digit=3}.
        for (int cell = 0; cell < 81; ++cell)
            if (!state.values[cell] && state.candidates[cell].size() == 1)
                actions.push_back({true, cell, *state.candidates[cell].begin()});
    } else {
        // Unit view: one remaining position in P(unit,digit) fixes the digit,
        // even if that cell currently has several candidates of its own.
        // Example: row 1 allows 3 only in c4, while M(r1c4)={1,3,6}; that row
        // forces r1c4=3 even though the cell does not form a Naked Single.
        for (int unit = 0; unit < 27; ++unit)
            for (int digit = 1; digit <= 9; ++digit) {
                const auto positions = state.candidate_positions(unit, digit);
                // A zero-position mask is not a Hidden Single. In particular
                // it is normal after this digit has already been filled in u.
                if (positions.size() == 1)
                    actions.push_back({true, positions[0], digit});
            }
    }
    // A Hidden Single may be proved independently by its row, column, and box.
    canonical(actions);
    return actions;
}

Actions detect_actions(const State& state, Strategy strategy, SubsetCache& cache) {
    // Group strategies by the state view and search procedure they share.
    // No case applies its result; the solve loop owns detection/commit ordering.
    // Passing cache into this dispatcher does not cache all families: only the
    // five Subset cases consume it. Other cases recompute from current inputs.
    switch (strategy) {
        case Strategy::NakedSingle:
        case Strategy::HiddenSingle:
            return find_single_actions(state, strategy);
        case Strategy::NakedPair:
        case Strategy::HiddenPair:
        case Strategy::NakedTriple:
        case Strategy::HiddenTriple:
        case Strategy::NakedQuad:
            return find_subset_actions(state, strategy, cache);
        case Strategy::Pointing:
        case Strategy::Claiming:
            return find_intersection_actions(state, strategy);
        case Strategy::XWing:
        case Strategy::Swordfish:
            return find_fish_actions(state, strategy);
        case Strategy::SimpleColoring:
            return find_coloring_actions(state);
        case Strategy::YWing:
        case Strategy::XYZWing:
            return find_wing_actions(state, strategy);
        case Strategy::XYChain:
            return find_xy_chain_actions(state);
    }
    throw std::invalid_argument("unknown strategy");
}

} // namespace sudoku
