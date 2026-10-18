#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sudoku {

/* Zero-allocation bridge between P's position bits and the detector's cell IDs.
 * A caller typically writes:
 *     auto cells = state.candidate_positions(row_unit, digit);
 *     if (cells.size() == 2) { ... cells[0] ... cells[1] ... }
 * or iterates with a range-for. Neither form first builds a vector of matches.
 * This is a small internal range, not a general random-access STL container.
 */
// A snapshot of nine position bits, borrowing only the immutable unit's cells.
// Iteration visits set positions directly; it does not allocate a temporary list.
// Example: row 1 with mask bits 0,3,8 yields board cells 0,3,8. The same bits
// in column 1 yield cells 0,27,72: a bit is local to the unit, not a cell ID.
class PositionRange {
public:
    class Iterator {
    public:
        Iterator(const std::vector<int>* cells, unsigned remaining)
            : cells_(cells), remaining_(remaining) {}
        // Map the next set position to a board cell only when requested.
        // remaining_ must be nonzero; callers must not dereference end().
        int operator*() const { return (*cells_)[__builtin_ctz(remaining_)]; }
        Iterator& operator++() {
            // The backing unit order does not change; clearing the lowest bit
            // moves to the next available position in that same unit.
            remaining_ &= remaining_ - 1;
            return *this;
        }
        bool operator!=(const Iterator& other) const {
            return cells_ != other.cells_ || remaining_ != other.remaining_;
        }

    private:
        const std::vector<int>* cells_;
        unsigned remaining_;
    };

    // Own the bits, not a reference to State::position_index. Later State
    // updates do not change this range's membership. The borrowed unit must
    // outlive the range; normal callers use the global immutable topology.
    PositionRange(const std::vector<int>& cells, uint16_t mask)
        : cells_(&cells), mask_(mask) {}
    Iterator begin() const { return {cells_, mask_}; }
    Iterator end() const { return {cells_, 0}; }
    // A count of available positions, not the last position index plus one.
    size_t size() const { return __builtin_popcount(mask_); }
    int operator[](size_t offset) const {
        // Offset means "the nth available cell", not "bit number n". This
        // costs at most nine iterator steps; offset must be less than size().
        auto current = begin();
        while (offset--) ++current;
        return *current;
    }

private:
    const std::vector<int>* cells_;
    // A bit snapshot can safely outlive a state mutation as data, but it still
    // describes the OLD query result. Run a new query to inspect the new State.
    unsigned mask_;
};

} // namespace sudoku
