#pragma once
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <stdexcept>

namespace sudoku {

/* A tiny value type: copy/assignment copies one uint16_t, not a heap container.
 * The only writable operations are insert/erase; mask() returns a number by
 * value so callers cannot bypass the digit-range invariant through a reference.
 *
 * Normal solving creates full candidate sets during initialization and only
 * erases afterward. insert() is also useful to construct synthetic test states;
 * editing a State that way requires rebuilding its separate position indexes.
 */
// Bit (digit - 1) records whether that digit is a candidate. Only nine bits of
// the uint16_t are used: {1,3,6} is (1<<0)|(1<<2)|(1<<5), or decimal 37.
// This is a set of DIGITS, not a set of board cells or positions in a unit.
class DigitSet {
public:
    class Iterator {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = int;
        using difference_type = std::ptrdiff_t;
        using pointer = const int*;
        using reference = int;

        // The iterator stores a copy of the remaining bits. Advancing it cannot
        // erase candidates from the DigitSet that supplied begin().
        explicit Iterator(uint16_t remaining = 0) : remaining_(remaining) {}
        // ctz counts trailing zero bits: the lowest set bit gives the next digit.
        // Dereferencing end() is invalid, just as for a standard container.
        int operator*() const { return __builtin_ctz(unsigned(remaining_)) + 1; }
        Iterator& operator++() {
            // x & (x-1) clears exactly the lowest set bit, so iteration visits
            // candidates in ascending order without scanning nine positions.
            // Example: mask 37 ({1,3,6}) becomes 36 ({3,6}), then 32 ({6}),
            // then 0. The final zero is the end marker, not candidate digit 0.
            remaining_ &= remaining_ - 1;
            return *this;
        }
        bool operator==(const Iterator& other) const { return remaining_ == other.remaining_; }
        bool operator!=(const Iterator& other) const { return !(*this == other); }

    private:
        uint16_t remaining_;
    };

    DigitSet() = default; // Empty by default; appropriate for a filled cell.
    DigitSet(std::initializer_list<int> digits) {
        for (int digit : digits) insert(digit);
    }
    // The range check also makes erase(0) harmless during initialization, when
    // an unfilled peer contributes the value 0 rather than a real digit.
    bool count(int digit) const {
        return digit >= 1 && digit <= 9 && (mask_ & (1u << (digit - 1)));
    }
    // Count set bits, not the mask's numeric value: {1,3,6} has mask 37, size 3.
    size_t size() const { return __builtin_popcount(unsigned(mask_)); }
    bool empty() const { return mask_ == 0; }
    uint16_t mask() const { return mask_; } // Only bits 0..8 can be set.
    void insert(int digit) {
        // OR is idempotent: inserting an existing digit does not duplicate it.
        if (digit < 1 || digit > 9) throw std::invalid_argument("candidate outside 1..9");
        mask_ |= 1u << (digit - 1);
    }
    bool erase(int digit) {
        // Returning whether a bit changed lets State avoid unnecessary updates
        // of the three corresponding unit-position masks.
        if (!count(digit)) return false;
        // AND with the inverted one-bit mask preserves every other candidate.
        // For {1,3,6}, erasing 3 changes decimal 37 to 33, i.e. {1,6}.
        mask_ &= ~(1u << (digit - 1));
        return true;
    }
    Iterator begin() const { return Iterator(mask_); }
    Iterator end() const { return Iterator(); }

private:
    uint16_t mask_ = 0;
};

} // namespace sudoku
