#ifndef HPS_FINAL_H
#define HPS_FINAL_H

/* Minimal answer-only C API.
 *
 * Typical usage (the caller supplies an existing 81-digit puzzle string):
 *     char grid[82];
 *     int32_t status;
 *     int32_t code = hps_solve_grid_scheduled_strategies(puzzle, 81, grid, &status);
 *     if (code == HPS_OK && status == HPS_SOLVED) { ... use grid ... }
 *
 * The library borrows input/output buffers only for the duration of the call.
 * There are no result handles to free, callbacks, shared sessions, retained
 * explanation records, or persistent puzzle-result caches in this interface.
 */
#include <stddef.h>
#include <stdint.h>

/* Only platform linkage remains conditional; algorithms are always enabled. */
/* HPS_FINAL_BUILD is defined for the library itself, not for applications that
 * include this header. The remaining declarations are usable from C or C++. */
#if defined(_WIN32)
# if defined(HPS_FINAL_BUILD)
#  define HPS_API __declspec(dllexport)
# else
#  define HPS_API __declspec(dllimport)
# endif
#else
# define HPS_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Call-level return codes: HPS_OK does NOT mean the puzzle was solved.
 * The separate status output below describes the algorithm's stopping reason. */
#define HPS_OK ((int32_t)0)
/* Null required pointer; caller-provided storage sizes cannot be checked from
 * raw pointers, so supplying 82 output bytes is still the caller's obligation. */
#define HPS_INVALID_ARGUMENT ((int32_t)1)
/* Wrong length/non-digit input or a locally contradictory INITIAL board. */
#define HPS_INVALID_INPUT ((int32_t)2)
#define HPS_OUT_OF_MEMORY ((int32_t)3)
/* An unexpected failure after initialization, including stale actions or a
 * final structural validation failure. Such a call does not export its board. */
#define HPS_INTERNAL_ERROR ((int32_t)4)
/* STALLED means all fifteen configured strategies found no next action, not
 * that the puzzle is unsatisfiable. BUDGET_EXCEEDED is a defensive round cap. */
#define HPS_SOLVED ((int32_t)100)
#define HPS_STALLED ((int32_t)101)
#define HPS_BUDGET_EXCEEDED ((int32_t)102)

/* Input: exactly 81 ASCII digits, with 0 for an empty cell.
 * The input need not be NUL-terminated; length is checked explicitly.
 * Output: caller-owned storage for 82 bytes (81 digits and a NUL terminator).
 * Input, output and status storage must be valid and must not overlap.
 * HPS_OK means the call succeeded; inspect status to distinguish solved/stalled.
 * On HPS_OK, output contains the current grid, including a stalled partial grid.
 * On other return codes, do not interpret status as a successful solve result.
 * Valid non-null outputs are first initialized to NUL / STALLED. A late failure
 * can follow a status change; ignore status unless the return code is HPS_OK.
 * Each call owns its state and caches, so independent calls may run concurrently.
 * A completed grid is validated before return; uniqueness is not checked.
 * No guessing, backtracking, explanations, or JSON output is provided.
 *
 * Preserving givens does not require a second stored puzzle in the hot loop:
 * an assignment is accepted only when its target value is zero. Per-call State
 * owns all temporary C++ objects, and exceptions are translated to these codes.
 */
HPS_API int32_t hps_solve_grid_batched_strategies(
    const char* puzzle, size_t length, char output[82], int32_t* status);

/* Same engine with the paper's fixed cost-aware strategy order. It changes only
 * the order of advanced checks, not their logical tests. A different path may
 * yield a different valid completion for a puzzle with multiple solutions. */
HPS_API int32_t hps_solve_grid_scheduled_strategies(
    const char* puzzle, size_t length, char output[82], int32_t* status);

#ifdef __cplusplus
}
#endif
#endif
