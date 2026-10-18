#include "state.hpp"
#include "cell_mask.hpp"

namespace sudoku {

// The three membership formulas used throughout this implementation are:
//   row unit    = cell / 9
//   column unit = 9 + cell % 9
//   box unit    = 18 + (cell / 27)*3 + (cell % 9)/3
// These return unit IDs, not the cell's position WITHIN the unit.
Topology::Topology() {
    // Appending cells in row-major order also orders each unit's nine positions.
    // A box's local positions likewise run left-to-right, top-to-bottom.
    for (int cell = 0; cell < 81; ++cell) {
        units[cell / 9].push_back(cell);
        units[9 + cell % 9].push_back(cell);
        units[18 + (cell / 27) * 3 + (cell % 9) / 3].push_back(cell);
    }
    for (const auto& unit : units)
        for (int cell : unit)
            for (int peer : unit)
                if (cell != peer) peers[cell].push_back(peer);
    // A peer may share both a row and a box. Remove duplicates to obtain the
    // 20 distinct peers per cell, preserving deterministic ascending iteration.
    for (auto& neighbors : peers) {
        std::sort(neighbors.begin(), neighbors.end());
        neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
    }
}

const Topology topology;
// Keep both immutable tables in this translation unit, in initialization order.
// peer_masks is constructed from topology, so it must not be initialized first.
// Both tables are built once, not once per puzzle or once per worker.
namespace detail {
const std::array<CellMask, 81> peer_masks = [] {
    std::array<CellMask, 81> masks{};
    for (int cell = 0; cell < 81; ++cell)
        for (int peer : topology.peers[cell]) masks[cell] = with(masks[cell], peer);
    return masks;
}();
} // namespace detail

State::State(const std::string& puzzle) {
    // The string constructor at the C boundary copies exactly length bytes;
    // an embedded NUL is not an end marker here and fails the digit check.
    if (puzzle.size() != 81) throw std::invalid_argument("81 digits required");
    // Read every given before deriving candidates, so later input positions
    // exclude digits from earlier empty cells just as earlier givens do.
    for (int cell = 0; cell < 81; ++cell) {
        if (puzzle[cell] < '0' || puzzle[cell] > '9')
            throw std::invalid_argument("invalid digit");
        values[cell] = puzzle[cell] - '0';
    }
    // Start each empty cell with 1..9 and remove filled-peer digits. No guessed
    // values or stored answers participate in candidate initialization.
    // A filled cell is deliberately left with the default empty DigitSet:
    // candidate-position queries must never report an already filled value.
    for (int cell = 0; cell < 81; ++cell) {
        if (values[cell]) continue;
        candidates[cell] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
        for (int peer : topology.peers[cell]) candidates[cell].erase(values[peer]);
    }
    // Build the reverse view only after all cell candidate masks are complete.
    rebuild_positions();
    validate();
}

bool State::solved() const {
    // An all-filled board is only a candidate for successful return. The solve
    // loop calls validate() before reporting it as SOLVED, so duplicate filled
    // digits cannot be accepted merely because no zeros remain.
    return std::all_of(values.begin(), values.end(), [](int value) { return value != 0; });
}

void State::validate() const {
    // Read-only structural check, not a repair step. It never adds candidates,
    // changes a value, or rebuilds P. P/M synchronization is maintained by the
    // update methods and independently checked in tests, not rescanned here.
    // On failure the public solve call returns an error without exporting grid.
    // First collect each unit's occupied digits. Encountering the same bit
    // twice means two filled cells violate a row, column, or box constraint.
    std::array<uint16_t, 27> occupied{};
    for (int unit = 0; unit < 27; ++unit) {
        for (int cell : topology.units[unit]) {
            const int value = values[cell];
            if (value < 0 || value > 9) throw std::runtime_error("invalid value");
            if (!value) continue;
            const auto bit = static_cast<uint16_t>(1u << (value - 1));
            if (occupied[unit] & bit) throw std::runtime_error("conflicting values");
            occupied[unit] |= bit;
        }
    }
    // For an empty cell, the union of these three occupied masks is exactly
    // the set of digits forbidden by its filled peers. This catches structural
    // contradictions; it does not search for a solution of an incomplete board.
    // Check duplicates before candidates to preserve the frozen validation
    // order. A conflict or empty candidate set is an error, not a stalled solve.
    for (int cell = 0; cell < 81; ++cell) {
        if (values[cell]) continue;
        if (candidates[cell].empty()) throw std::runtime_error("empty candidates");
        const auto forbidden = occupied[cell / 9] | occupied[9 + cell % 9] |
            occupied[18 + (cell / 27) * 3 + (cell % 9) / 3];
        if (candidates[cell].mask() & forbidden)
            throw std::runtime_error("illegal candidate");
    }
}

void State::rebuild_positions() {
    // Reconstruct P from M. Normal solving instead updates just the affected
    // bits in erase_candidate(); tests can rebuild independently for comparison.
    for (auto& unit : position_index) unit.fill(0);
    for (int unit = 0; unit < 27; ++unit)
        for (int position = 0; position < 9; ++position)
            for (int digit : candidates[topology.units[unit][position]])
                position_index[unit][digit - 1] |= 1u << position;
}

void State::erase_candidate(int cell, int digit) {
    // A removal already performed has no effect. Otherwise exactly three unit
    // masks need changing: this cell's row, column, and box for this digit.
    // Worked example: remove 3 from r3c4 (global cell 21).
    //   M(21): clear digit bit 2.
    //   P(row 3,3)    = [2][2]:  clear position bit 3 (column 4).
    //   P(column 4,3) = [12][2]: clear position bit 2 (row 3).
    //   P(box 2,3)    = [19][2]: clear position bit 6 (bottom-left of box 2).
    // All four changes describe the SAME candidate removal in different views.
    if (!candidates[cell].erase(digit)) return;
    const int row = cell / 9, column = cell % 9;
    position_index[row][digit - 1] &= ~(1u << column);
    position_index[9 + column][digit - 1] &= ~(1u << row);
    // The box's local position is 3*(row within box) + (column within box).
    position_index[18 + (row / 3) * 3 + column / 3][digit - 1] &=
        ~(1u << ((row % 3) * 3 + column % 3));
}

PositionRange State::candidate_positions(int unit, int digit) const {
    // Copy nine bits, not a vector of cells. PositionRange borrows only the
    // immutable topology and translates each set bit when the caller visits it.
    return {topology.units[unit], position_index[unit][digit - 1]};
}

void State::apply(const Actions& actions) {
    // Detectors have already justified the actions from one unchanged state.
    // Applicability is still checked in sequence; an unexpected stale action
    // is an internal error, not permission to silently discard a deduction.
    // This internal operation is not transactional: a late error may follow
    // earlier updates in this batch. Its caller then discards the per-call State
    // and returns an error, so a partly applied board is not returned as valid.
    for (const auto& action : actions) {
        if (values[action.cell] || !candidates[action.cell].count(action.digit))
            throw std::runtime_error("stale action");
        if (action.assign) {
            values[action.cell] = action.digit;
            // A filled cell no longer holds candidates. Use the same removal
            // function to clear its positions for every digit, not just its value.
            // Do not replace this loop with candidates[cell].clear(): that
            // would leave the filled cell present in its old position indexes.
            while (!candidates[action.cell].empty())
                erase_candidate(action.cell, *candidates[action.cell].begin());
            // Propagate the assigned digit only to unfilled peers. A plain
            // elimination below changes just its target; later rounds detect
            // any new Singles rather than inserting extra assignments here.
            for (int peer : topology.peers[action.cell])
                if (!values[peer]) erase_candidate(peer, action.digit);
        } else {
            // There is no filled value to propagate for an elimination. Only
            // this (cell,digit) entry and its three reverse-index bits change.
            erase_candidate(action.cell, action.digit);
        }
    }
}

std::string State::grid() const {
    // Serialization happens after validation in solver.cpp. This method does
    // not decide whether a partial grid is solved or stalled; it only formats V.
    std::string result;
    result.reserve(81);
    for (int value : values) result += char('0' + value);
    return result;
}

} // namespace sudoku
