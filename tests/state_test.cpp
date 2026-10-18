#include "cell_mask.hpp"
#include "detectors.hpp"

#include <iostream>
#include <random>

namespace {
// Assertions are explicit exceptions instead of the assert macro, so Release
// builds with NDEBUG still execute every check rather than silently omitting it.
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void check_indexes(const sudoku::State& state) {
    // Independent reference: scan the cell candidates instead of trusting P.
    // Matching two queries that both read the same stale index would prove little.
    for (int unit = 0; unit < 27; ++unit) {
        for (int digit = 1; digit <= 9; ++digit) {
            sudoku::Cells expected, actual;
            for (int cell : sudoku::topology.units[unit])
                if (state.candidates[cell].count(digit)) expected.push_back(cell);
            for (int cell : state.candidate_positions(unit, digit)) actual.push_back(cell);
            require(actual == expected, "position index mismatch");
        }
    }
}

void check_masks_and_ranges() {
    // There are only 2^9 possible digit/position masks, so enumerate them all.
    // Check every unit too: the same position bits map to different board cells.
    // Include mask=0: an empty range must be iterable without calling ctz(0).
    // Include mask=511: all nine bits must map to exactly nine ordered items.
    for (unsigned mask = 0; mask < 512; ++mask) {
        sudoku::DigitSet digits;
        for (int digit = 1; digit <= 9; ++digit)
            if (mask & (1u << (digit - 1))) digits.insert(digit);
        require(digits.mask() == mask, "candidate mask mismatch");
        require(digits.size() == size_t(__builtin_popcount(mask)), "wrong candidate count");
        unsigned visited = 0;
        int previous = 0;
        for (int digit : digits) {
            require(digit > previous, "candidate order changed");
            visited |= 1u << (digit - 1);
            previous = digit;
        }
        require(visited == mask, "candidate iteration mismatch");
        for (int unit = 0; unit < 27; ++unit) {
            sudoku::PositionRange range(sudoku::topology.units[unit], mask);
            size_t index = 0;
            for (int cell : range) {
                require(cell == range[index++], "position indexing mismatch");
            }
            require(index == range.size(), "position count mismatch");
        }
    }
    for (int cell = 0; cell < 81; ++cell) {
        // This also crosses CellMask's word boundary at cells 63/64 and checks
        // that the 20-peer encoding never includes the cell itself.
        require(sudoku::topology.peers[cell].size() == 20, "wrong peer count");
        unsigned count = 0;
        sudoku::detail::visit_cells(sudoku::detail::peer_masks[cell], [&](int peer) {
            require(peer != cell, "cell is its own peer");
            ++count;
        });
        require(count == 20, "wrong peer mask");
    }
}

void check_updates() {
    sudoku::State state(std::string(81, '0'));
    std::mt19937 random(20261018);
    // Synthetic domains exercise update bookkeeping, not Sudoku solvability.
    // Copies must remain unchanged when the original loses a candidate.
    for (int step = 0; step < 2000; ++step) {
        const int cell = random() % 81, digit = random() % 9 + 1;
        const auto before = state;
        state.erase_candidate(cell, digit);
        check_indexes(state);
        check_indexes(before);
        if (step % 7 == 0) {
            // Direct insertion is a TEST operation, outside the solve API.
            // Rebuilding after it restores the invariant expected by detectors.
            state.candidates[cell].insert(digit);
            state.rebuild_positions();
            check_indexes(state);
        }
    }
    sudoku::State fresh(std::string(81, '0'));
    // Exercise both kinds of real update: assignment clears all target-cell
    // candidates and propagates to peers; elimination changes only its target.
    fresh.apply({{true, 0, 1}});
    fresh.apply({{false, 40, 3}, {false, 40, 4}});
    fresh.validate();
    check_indexes(fresh);
    require(fresh.values[0] == 1 && fresh.candidates[0].empty(), "assignment not committed");
    bool rejected = false;
    // r1c2 is a peer of the just-filled r1c1=1, so assigning 1 there must fail
    // immediately, even though full-board validation is delayed during solving.
    try { fresh.apply({{true, 1, 1}}); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "stale action accepted");
}

void check_detectors_and_cache() {
    using sudoku::Strategy;
    sudoku::State state(std::string(81, '0'));
    // A pair in r1c1/r1c2 belongs to both row 1 and box 1. Deduplicating their
    // shared targets leaves 13 other cells, each losing the two pair digits.
    state.candidates[0] = {2, 7};
    state.candidates[1] = {2, 7};
    state.rebuild_positions();
    sudoku::SubsetCache cache;
    const auto actions = sudoku::detect_actions(state, Strategy::NakedPair, cache);
    // Without mask-level deduplication, row/box overlap could emit duplicates.
    require(actions.size() == 26, "wrong naked pair eliminations");
    require(actions == sudoku::detect_actions(state, Strategy::NakedPair, cache),
            "cache changed detector output");

    sudoku::State empty(std::string(81, '0'));
    // An unrelated unit change keeps the failure valid, but a change to the
    // recorded row must force a new scan even if just one candidate changed.
    sudoku::SubsetCache failed;
    require(!failed.skip(empty, Strategy::NakedPair, 0), "fresh cache skipped check");
    failed.finish(Strategy::NakedPair, 0, true);
    require(failed.skip(empty, Strategy::NakedPair, 0), "failed check not cached");
    // Delete outside row 1: the snapshot of row 1 is unchanged.
    empty.erase_candidate(80, 1);
    require(failed.skip(empty, Strategy::NakedPair, 0), "unrelated update invalidated cache");
    // Delete inside row 1: an exact snapshot mismatch must invalidate failure.
    empty.erase_candidate(0, 1);
    require(!failed.skip(empty, Strategy::NakedPair, 0), "changed input reused failure");
}
} // namespace

int main() {
    try {
        check_masks_and_ranges();
        check_updates();
        check_detectors_and_cache();
        std::cout << "masks, ranges, 2000 updates, detectors, and cache checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
