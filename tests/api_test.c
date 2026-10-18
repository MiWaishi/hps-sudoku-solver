#include "hps.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
/* Accumulate failures instead of stopping at the first check, so one test run
 * reports multiple contract problems. Nothing relies on assert/NDEBUG. */
static void check(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        ++failures;
    }
}

typedef int32_t (*Solver)(const char*, size_t, char*, int32_t*);

static void check_solver(Solver solve) {
    /* Both schedules have the same public contract. Include solved, stalled,
     * malformed and contradictory inputs; success alone is not enough. */
    const char puzzle[] =
        "530070000600195000098000060800060003400803001700020006060000280000419005000080079";
    const char solution[] =
        "534678912672195348198342567859761423426853791713924856961537284287419635345286179";
    char output[82];
    int32_t status = 0;
    check(solve(puzzle, 81, output, &status) == HPS_OK, "solve failed");
    check(status == HPS_SOLVED && strcmp(output, solution) == 0, "wrong solution");
    check(solve(solution, 81, output, &status) == HPS_OK, "solved input rejected");
    check(status == HPS_SOLVED && strcmp(output, solution) == 0, "solved input changed");

    /* An empty puzzle is locally legal but has no deduction under these rules.
     * It must return HPS_OK + HPS_STALLED, not INVALID_INPUT or SOLVED. */
    char empty[82];
    memset(empty, '0', 81);
    empty[81] = '\0';
    check(solve(empty, 81, output, &status) == HPS_OK, "empty puzzle rejected");
    check(status == HPS_STALLED && strcmp(output, empty) == 0, "empty puzzle not stalled");
    /* Invalid pointers are distinguished from a readable but malformed puzzle.
     * The test supplies NULL only where the implementation promises to reject it. */
    check(solve(NULL, 81, output, &status) == HPS_INVALID_ARGUMENT, "null puzzle accepted");
    check(solve(puzzle, 81, NULL, &status) == HPS_INVALID_ARGUMENT, "null output accepted");
    check(solve(puzzle, 81, output, NULL) == HPS_INVALID_ARGUMENT, "null status accepted");
    check(solve("bad", 3, output, &status) == HPS_INVALID_INPUT, "bad length accepted");
    check(output[0] == '\0' && status == HPS_STALLED, "error output not initialized");

    empty[0] = 'x';
    check(solve(empty, 81, output, &status) == HPS_INVALID_INPUT, "non-digit accepted");
    empty[0] = empty[1] = '1';
    check(solve(empty, 81, output, &status) == HPS_INVALID_INPUT, "conflict accepted");
    memset(empty, '0', 81);
    /* No duplicate givens, but r1c9 has no candidate: row 1 forbids 1..8
     * and the 9 at r2c9 forbids the last digit. Initialization must reject it. */
    memcpy(empty, "123456780", 9);
    empty[17] = '9';
    check(solve(empty, 81, output, &status) == HPS_INVALID_INPUT, "empty candidates accepted");
}

int main(void) {
    check_solver(hps_solve_grid_batched_strategies);
    check_solver(hps_solve_grid_scheduled_strategies);
    return failures ? 1 : 0;
}
