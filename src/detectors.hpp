#pragma once
#include "state.hpp"

namespace sudoku {

/* Detector contract:
 *   - Input State must have synchronized values, cell masks, position indexes.
 *   - Return all actions found by the selected implemented check on that State.
 *   - Empty output means "no action found", not "no solution exists".
 *   - No candidate or value changes until solver.cpp calls State::apply().
 *   - Output actions are deterministic and deduplicated. No witnesses, old
 *     actions, or explanatory text are kept for use after the state changes.
 *
 * The families below implement fifteen named rules, not every Sudoku rule.
 * In particular Hidden Quad and unbounded chains are not enabled implicitly.
 */
// Numeric IDs and strategy semantics match the frozen paper implementation.
// Each entry selects a pattern detector; recursive searches inside detectors
// enumerate patterns, not guesses at values in the Sudoku solution.
enum class Strategy {
    NakedSingle, HiddenSingle, NakedPair, HiddenPair, Pointing, Claiming,
    NakedTriple, HiddenTriple, NakedQuad, XWing, Swordfish, SimpleColoring,
    YWing, XYZWing, XYChain
};

// Only failed subset checks are cached, using exact values/candidate snapshots.
// A cache entry belongs to one (subset strategy, unit). Successful action lists
// are never retained and reused after changing the board.
class SubsetCache {
public:
    // true: skip this exact failed check; false: caller must scan the unit.
    // On false, skip() first records the new inputs and clears the failure flag.
    bool skip(const State& state, Strategy strategy, int unit);
    // Complete that scan's cache entry. Call only after all combinations have
    // been considered, with empty=false if even one legal removal was found.
    void finish(Strategy strategy, int unit, bool empty);

private:
    struct Entry {
        std::array<uint16_t, 9> signature{};
        // Means "this exact input produced no action", not "the unit is empty".
        bool empty = false;
    };
    // Five subset strategies x 27 units. Initial empty=false prevents an
    // uninitialized snapshot from being treated as a proven unsuccessful check.
    std::array<std::array<Entry, 27>, 5> entries_{};
    static int slot(Strategy strategy);
};

// OR together discoveries in per-cell digit masks, then flatten them once into
// cell/digit order. Identical eliminations from multiple patterns appear once.
Actions actions_from_masks(const std::array<uint16_t, 81>& eliminations);
// M for naked subsets; P for hidden subsets. Failed scans use SubsetCache.
Actions find_subset_actions(const State&, Strategy, SubsetCache&);
// P for confinement between a box and a row/column.
Actions find_intersection_actions(const State&, Strategy);
// P converted to row/column footprints for a single digit.
Actions find_fish_actions(const State&, Strategy);
// P supplies strong links; CellMask supplies component/visibility operations.
Actions find_coloring_actions(const State&);
// M supplies bivalue/trivalue cells; CellMask intersects their peers.
Actions find_wing_actions(const State&, Strategy);
// M and peer relations supply directed candidate implications, bounded to 8 cells.
Actions find_xy_chain_actions(const State&);

// Complete read-only scan for one strategy; applying actions is a separate step.
// The only mutable argument is the exact failure cache, not the puzzle State.
Actions detect_actions(const State&, Strategy, SubsetCache&);

} // namespace sudoku
