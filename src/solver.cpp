#include "hps.h"
#include "detectors.hpp"

#include <memory>
#include <new>

/* One complete call:
 *   check arguments -> initialize/validate State -> repeat read/detect/apply
 *   -> validate final State -> copy output and return a separate solve status.
 *
 * The outer loop is a sequence of logical deductions. It has no branch that
 * tries a candidate value, saves a speculative board, or backtracks. Pattern
 * searches may recurse internally, but they only read this same current State.
 */
namespace {
using sudoku::Strategy;
using Order = std::array<Strategy, 15>;

// The paper compares these two schedules, not different sets of strategies.
// Both exhaust all fifteen checks before reporting that a board has stalled.
constexpr Order canonical_order = {
    Strategy::NakedSingle, Strategy::HiddenSingle, Strategy::NakedPair,
    Strategy::HiddenPair, Strategy::Pointing, Strategy::Claiming,
    Strategy::NakedTriple, Strategy::HiddenTriple, Strategy::NakedQuad,
    Strategy::XWing, Strategy::Swordfish, Strategy::SimpleColoring,
    Strategy::YWing, Strategy::XYZWing, Strategy::XYChain};

// Keep the first nine cheap checks unchanged; reorder only the advanced ones.
// This is a fixed measured schedule, not online learning or a guessing policy.
constexpr Order cost_aware_order = {
    Strategy::NakedSingle, Strategy::HiddenSingle, Strategy::NakedPair,
    Strategy::HiddenPair, Strategy::Pointing, Strategy::Claiming,
    Strategy::NakedTriple, Strategy::HiddenTriple, Strategy::NakedQuad,
    Strategy::XYChain, Strategy::XYZWing, Strategy::XWing,
    Strategy::YWing, Strategy::SimpleColoring, Strategy::Swordfish};

// Shared implementation behind the two exported C functions. The order is the
// ONLY behavioral parameter; the detectors, batching, caching, and validation
// are fixed. Per-call ownership also makes independent concurrent calls safe.
int32_t solve(const char* puzzle, size_t length, char output[82],
              int32_t* status, const Order& order) noexcept {
    // The C interface has two results: a return code for call/input failures,
    // and a solve status for SOLVED/STALLED/BUDGET_EXCEEDED. Never let a C++
    // exception cross that boundary, and never expose an unchecked final grid.
    if (!puzzle || !output || !status) return HPS_INVALID_ARGUMENT;
    // Initialize writable outputs before rejecting a bad length/content. For
    // null pointers above, no output initialization can be promised to callers.
    output[0] = '\0';
    *status = HPS_STALLED;
    if (length != 81) return HPS_INVALID_INPUT;

    // Separate invalid input from failures after solving has begun.
    // A contradictory initial board is INVALID_INPUT. A contradiction found
    // after a supposedly valid deduction is INTERNAL_ERROR, not INVALID_INPUT.
    std::unique_ptr<sudoku::State> owned_state;
    try {
        owned_state = std::make_unique<sudoku::State>(std::string(puzzle, length));
    } catch (const std::invalid_argument&) {
        return HPS_INVALID_INPUT;
    } catch (const std::runtime_error&) {
        return HPS_INVALID_INPUT;
    } catch (const std::bad_alloc&) {
        return HPS_OUT_OF_MEMORY;
    } catch (...) {
        return HPS_INTERNAL_ERROR;
    }
    try {
        auto& state = *owned_state;
        // Cache lifetime is one solve call. A failed check from a different
        // puzzle can never leak into this board or into another worker's state.
        sudoku::SubsetCache cache;
        // This is a defensive round budget, not a limit on recursive pattern
        // enumeration. One successful batched strategy counts as one round.
        constexpr uint32_t round_limit = 10000;
        uint32_t rounds = 0;
        while (!state.solved() && rounds < round_limit) {
            // progressed is local to a round. It becomes true only after a
            // nonempty batch was committed; empty checks do not consume budget.
            bool progressed = false;
            for (Strategy strategy : order) {
                // Complete this strategy's read-only scan before any mutation.
                // It can return several actions; later strategies are not run
                // against this old state once a successful batch is selected.
                const auto actions = sudoku::detect_actions(state, strategy, cache);
                if (actions.empty()) continue;
                // All actions were justified by this same pre-action state.
                // Example: four Naked Singles produce four assignments. Apply
                // that batch now, rather than retain three unused actions and
                // rediscover them in later scans. No action list survives here
                // into a later round; only exact failed-check snapshots do.
                state.apply(actions);
                ++rounds;
                progressed = true;
                break; // Restart the full configured order on the new state.
            }
            // A strategy can be logically sound yet unable to advance this
            // board. STALLED does not mean the puzzle has no solution.
            if (!progressed) break;
        }
        // Delayed validation removes per-round whole-board scans, not the final
        // check. Even a board with all 81 cells filled must pass before return.
        state.validate();
        // Completion takes priority over the budget: a last permitted batch
        // may fill the final cell. Otherwise distinguish exhaustion of the
        // defensive budget from failure of all strategies to find progress.
        *status = state.solved() ? HPS_SOLVED :
            (rounds >= round_limit ? HPS_BUDGET_EXCEEDED : HPS_STALLED);
        const auto grid = state.grid();
        // A normal stalled result also exports its partial 81-digit grid.
        // The call-level return is HPS_OK in that case; the status is STALLED.
        std::copy_n(grid.data(), 81, output);
        output[81] = '\0';
        return HPS_OK;
    } catch (const std::bad_alloc&) {
        return HPS_OUT_OF_MEMORY;
    } catch (...) {
        // Includes stale actions or terminal invariant failures. output remains
        // empty because copying occurs only after successful validation.
        return HPS_INTERNAL_ERROR;
    }
}
} // namespace

extern "C" int32_t hps_solve_grid_batched_strategies(
    const char* puzzle, size_t length, char output[82], int32_t* status) {
    return solve(puzzle, length, output, status, canonical_order);
}

extern "C" int32_t hps_solve_grid_scheduled_strategies(
    const char* puzzle, size_t length, char output[82], int32_t* status) {
    return solve(puzzle, length, output, status, cost_aware_order);
}
