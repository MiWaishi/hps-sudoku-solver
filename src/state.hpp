#pragma once
#include "digit_set.hpp"
#include "position_range.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <tuple>
#include <vector>

/* Reading guide: state and the three different meanings of a bit.
 *
 * values[cell]                    -> which digit has been filled (0 = empty).
 * candidates[cell]                -> which DIGITS remain possible in that cell.
 * position_index[unit][digit-1]    -> which POSITIONS still allow that digit.
 * detail::CellMask, in cell_mask.hpp, is a fourth helper: a set of BOARD CELLS
 * used by graph/pattern searches, not another copy of the candidate state.
 *
 * Example: r1c1 {1,3,6} has candidate bits 0,2,5. If digit 3 can occur in row 1
 * at c1,c4,c9, position_index[0][2] has bits 0,3,8. Do not compare these two
 * masks directly: identical bit numbers can refer to different kinds of things.
 */
namespace sudoku {

// Internal coordinates are zero-based: r1c1 is cell 0, r1c4 is cell 3,
// and r9c9 is cell 80. Candidate digits themselves remain in the range 1..9.
using Cells = std::vector<int>;

// A detector returns instructions, not a modified board or an explanation.
// assign=true fills a cell; assign=false removes one candidate from that cell.
struct Action {
    // Preconditions for detector-produced actions: cell is 0..80, digit is
    // 1..9, and the action was justified without mutating the input State.
    // Example: {true,3,3} fills r1c4 with 3; {false,0,3} removes 3 from r1c1.
    bool assign;
    int cell;
    int digit;
    // Preserve the frozen order: assignments first, then cell, then digit.
    auto key() const { return std::make_tuple(assign ? 0 : 1, cell, digit); }
    bool operator<(const Action& other) const { return key() < other.key(); }
    bool operator==(const Action& other) const { return key() == other.key(); }
};
using Actions = std::vector<Action>;

// The same action may be found through multiple units or patterns. Deduplicate
// it before applying the batch, so a repeated action is not mistaken for stale.
inline void canonical(Actions& actions) {
    // This normalizes identity, not compatibility: two different assignments
    // to one cell would remain different actions. The detector's logical proof
    // and State::apply's applicability checks have separate responsibilities.
    std::sort(actions.begin(), actions.end());
    actions.erase(std::unique(actions.begin(), actions.end()), actions.end());
}

// Unit IDs: rows 0..8, columns 9..17, boxes 18..26. Cells are row-major 0..80.
// These relationships never change. All solve calls may share this read-only
// topology, but each call owns a separate mutable State and SubsetCache.
struct Topology {
    // units[u][p] converts a local unit position p into a global board cell.
    // A column's positions are row indices; a box's positions are row-major
    // within its 3x3 area. Each vector has exactly nine cells.
    std::array<Cells, 27> units;
    // Each sorted vector has 20 DISTINCT cells sharing at least one unit.
    // The overlap between row/column peers and box peers is counted only once.
    std::array<Cells, 81> peers;
    Topology();
};
extern const Topology topology;

struct State {
    // This type is internal, not a public mutable-state API. Initialization and
    // the update methods maintain its invariants. Tests that edit candidates
    // directly must call rebuild_positions() before running any detector.
    // The paper's V, M and P: filled values, per-cell candidate masks, and
    // per-unit candidate-position masks are three views of the same board.
    // values[cell]==0 means empty; a filled cell has an empty candidate mask.
    std::array<int, 81> values{};
    std::array<DigitSet, 81> candidates;
    // [unit][digit - 1]: bit p refers to topology.units[unit][p].
    // Example: digit 3 at columns 1,4,9 of row 1 sets bits 0,3,8 in [0][2].
    // Invariant: a position bit is set exactly when that cell has the candidate.
    std::array<std::array<uint16_t, 9>, 27> position_index{};

    // Parse exactly 81 digits, derive legal initial candidates, build indexes,
    // and reject local contradictions. This does not prove global solvability.
    explicit State(const std::string& puzzle);
    bool solved() const; // Completion only; validate() checks structural validity.
    void validate() const; // Full scan at initialization and before returning.
    void rebuild_positions(); // Initialization/tests, not every normal update.
    // All normal removals pass here to keep cell and position masks synchronized.
    void erase_candidate(int cell, int digit);
    // Preconditions: unit is 0..26 and digit is 1..9. The returned range owns
    // its bit snapshot; it must not be reused as a fresh query after an update.
    PositionRange candidate_positions(int unit, int digit) const;
    // Apply a previously detected batch, checking each action against current
    // values/candidates. Does not perform a full-board scan after each action.
    void apply(const Actions& actions);
    std::string grid() const; // 81 row-major digits; unresolved cells remain '0'.
};

} // namespace sudoku
