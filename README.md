# 10,000 Sudoku Puzzles in 76 Milliseconds: Engineering a Fast Human-Strategy Solver

**Manuscript status:** Submitted for publication; not yet published. No journal acceptance or DOI is claimed here.

## Research background

Human-strategy Sudoku solvers recognize logical patterns rather than guess values and backtrack. Their performance depends not only on the strategies they support, but also on how those strategies are implemented. Repeatedly scanning unchanged cells, reconstructing candidate information, and building deduction records that are never applied can make an otherwise simple solve loop expensive.

The paper studies how to reduce this repeated work while retaining a fixed set of 15 human strategies. Its solver, HPS, combines compact candidate masks, maintained candidate-position indexes, direct action output, batched application of compatible actions, incremental updates, exact failure caches, delayed validation, and a fixed cost-aware strategy order. Independent puzzles can also be distributed across worker threads. None of these mechanisms introduces guessing or backtracking.

On an Apple M3 Pro, the paper reports a mean runtime of 16.717 seconds for the serial C++ baseline, 0.475 seconds for serial HPS, and 76 milliseconds for HPS with eleven workers, over the same 10,000-puzzle corpus. These are results from the paper's frozen implementation and controlled timing harness, not a runtime guarantee for the command-line program provided here.

This directory provides a standalone C++17 implementation of the optimized answer-only solver, together with a C API and tests. The paper's evaluation dataset is not included while the manuscript is under review. The solver returns a completed grid or a stopping status, does not generate step-by-step explanations, and never consults stored answers while solving.

## Requirements and build

You need CMake 3.16 or newer, a GCC/Clang compiler supporting C++17, and a C99 compiler for the C API test. Building, running, and testing require no Python interpreter or third-party test framework.

The code has been verified on macOS with Apple Clang on ARM64. It uses GCC/Clang bit-operation builtins; MSVC support is not claimed.

Run the following commands from the directory containing this README and `CMakeLists.txt`, regardless of what that directory is named. The examples place build products in a sibling directory named `hps-build`; you can substitute another build directory outside the source tree.

```sh
cmake -S . -B ../hps-build -DCMAKE_BUILD_TYPE=Release
cmake --build ../hps-build --parallel
```

The main outputs are:

- `../hps-build/hps_final_cli`: the command-line program.
- `../hps-build/libhps_final.dylib`: the shared library on macOS; normally `libhps_final.so` on Linux.

## Quick start

Solve a puzzle through standard input:

```sh
printf '%s\n' '530070000600195000098000060800060003400803001700020006060000280000419005000080079' | ../hps-build/hps_final_cli
```

To solve your own puzzle file with eleven worker threads:

```sh
../hps-build/hps_final_cli --input my-puzzles.txt --threads 11
```

Save the results to a file:

```sh
../hps-build/hps_final_cli --input my-puzzles.txt --threads 11 > answers.txt
```

Redirection creates or overwrites `answers.txt` in the current directory. Diagnostic messages go to standard error rather than the answers file.

## Command-line options

```text
hps_final_cli [--input PATH] [--canonical] [--threads 0..256]
```

| Option | Meaning |
| --- | --- |
| `--input PATH` | Read puzzles from a text file. If omitted, read standard input. |
| `--threads N` | Number of worker threads, from 0 to 256; default: 0. |
| `--canonical` | Use the original strategy order rather than the default fixed cost-aware order. |
| `--help` | Display usage information. |

Relative paths are resolved against the current working directory, not the executable's location. Quote paths containing spaces. When `--input` is specified, the program reads only that file and ignores standard input.

Both strategy orders use the same 15 strategies. Only the order of advanced checks changes; neither mode performs speculative search.

### Parallel execution

Parallelism is across puzzles, not within one puzzle. Each worker solves a puzzle independently, then takes another task. Adding workers does not parallelize a single-puzzle input.

- `--threads 0`: solve serially in the calling thread; this is the default.
- `--threads 1`: create one worker, which still solves puzzles one at a time.
- `--threads N`: create N workers, each with its own puzzle state and caches.

The program reads all inputs before solving and writes results after all workers finish. Output always follows input order, regardless of completion order. The upper limit of 256 is a defensive bound, not a recommended worker count.

## Input format

Each puzzle is a contiguous string of 81 ASCII digits in row-major order, with `0` representing an empty cell. One puzzle per line is recommended; spaces, tabs, and blank lines may also separate puzzles. Headers, comments, dots, and JSON are not supported.

For example:

```text
530070000600195000098000060800060003400803001700020006060000280000419005000080079
```

Standard-input redirection is also supported:

```sh
../hps-build/hps_final_cli < my-puzzles.txt
```

For interactive input, enter puzzles and then send EOF with Ctrl+D in a macOS/Linux terminal. Solving starts after input ends.

The paper's evaluation dataset is not distributed in this repository while the manuscript is under review. You can supply your own puzzles in the format above; the paper's reported timing is specific to its evaluation workload and is not a benchmark for arbitrary inputs.

A missing, unreadable, or empty input file produces a diagnostic and exit code 2. An individually malformed puzzle produces `INVALID_INPUT`; the remaining puzzles are still processed.

## Output and exit codes

Each input puzzle produces one output line. A solved puzzle produces its complete 81-digit grid:

```text
534678912672195348198342567859761423426853791713924856961537284287419635345286179
```

Otherwise, the line contains a status:

| Output | Meaning |
| --- | --- |
| `STALLED` | None of the configured strategies can make progress. This does not establish that the puzzle has no solution. |
| `INVALID_INPUT` | Invalid length or characters, or a detected local contradiction in the initial board. |
| `BUDGET_EXCEEDED` | The defensive solve-round limit was reached. |
| `ERROR` | An internal solve error; the failed board is not exported as a valid answer. |

The CLI prints only `STALLED` for a stalled puzzle, not its partial grid. Completed grids are validated before return; uniqueness is not checked.

Process exit codes are separate from these per-puzzle statuses:

| Exit code | Meaning |
| --- | --- |
| 0 | Every input puzzle was solved, or help was displayed successfully. |
| 1 | Results were written, but at least one puzzle was unsolved or encountered a per-puzzle error. |
| 2 | A command-line, input-file, read, or output-write error occurred. |

Output is explicitly flushed and checked before returning, so a failed write is not reported as success. Check the exit code when saving or processing results.

## Tests and validation

```sh
ctest --test-dir ../hps-build --output-on-failure
```

All tests are native C/C++ programs. Two tests cover the C API, input validation, candidate masks, position indexes, state updates, and failure caching. On POSIX systems, two additional C++ integration tests launch the CLI directly and check text-file input, threaded output order, and output-write errors. All four tests run through CTest without an interpreter or the original research repository.

The CLI tests share `tests/cli_test_support.hpp`, which creates temporary fixtures, captures process output, and enforces a 30-second timeout per child process. The research-only frozen-comparison script and evaluation dataset are not included in this distribution.

The implementation has passed Release and UBSan tests and comparison with the frozen implementation. In the authors' evaluation, all 10,000 puzzles matched the expected answers in serial and parallel execution. The evaluation dataset is not included here, so that full comparison cannot currently be reproduced from this repository alone. These checks provide correctness evidence, not a new performance measurement or an exhaustive proof for every possible board. The CLI also performs file parsing, worker creation, and terminal/file output, so its end-to-end runtime is not the paper's timed solve interval.

## C API

The public header, [include/hps.h](include/hps.h), exposes two single-puzzle functions:

- `hps_solve_grid_batched_strategies`: the original strategy order.
- `hps_solve_grid_scheduled_strategies`: the fixed cost-aware order.

Example:

```c
#include "hps.h"
#include <stdio.h>

int main(void) {
    const char puzzle[] =
        "530070000600195000098000060800060003400803001700020006060000280000419005000080079";
    char grid[82];
    int32_t status;
    int32_t code = hps_solve_grid_scheduled_strategies(puzzle, 81, grid, &status);
    if (code != HPS_OK || status != HPS_SOLVED) return 1;
    puts(grid);
    return 0;
}
```

Configure the caller's header search path and link against the `hps_final` shared library. Input, output, and status storage must not overlap, and the output buffer must hold at least 82 bytes. `HPS_OK` means the call succeeded, not necessarily that the puzzle was solved: check `status` as well. On a stalled call, the API returns a partial grid containing zeros, unlike the CLI's status-only output.

Each API call independently solves one puzzle and does not accept a worker-count parameter. Callers may arrange concurrent calls themselves; the CLI's puzzle-level parallel wrapper is implemented in `src/main.cpp`.

## Code guide

| File | Purpose |
| --- | --- |
| `src/main.cpp` | Input, options, puzzle-level workers, and ordered output. |
| `src/solver.cpp` | The detect, batch, update, and delayed-validation loop. |
| `src/state.hpp`, `src/state.cpp` | Filled values, candidates, position indexes, and incremental updates. |
| `src/digit_set.hpp`, `src/position_range.hpp`, `src/cell_mask.hpp` | Digit masks, position iteration, and board-wide cell sets. |
| `src/detectors.hpp`, `src/detectors.cpp` | Strategies, actions, and detector dispatch. |
| `src/subsets.cpp` | Naked/hidden subset detection and exact failure caches. |
| `src/patterns.cpp` | Pointing/Claiming, Fish, and Wing detection. |
| `src/chains.cpp` | Simple Coloring and bounded XY-Chain detection. |
| `tests/api_test.c`, `tests/state_test.cpp` | C interface, state, detector, and cache checks. |
| `tests/cli_input_test.cpp`, `tests/cli_output_test.cpp` | Native CLI input/output regression tests on POSIX systems. |
| `tests/cli_test_support.hpp` | Shared process-launch, capture, timeout, and fixture helpers. |

The 15 strategies are Naked Single, Hidden Single, Naked Pair, Hidden Pair, Pointing, Claiming, Naked Triple, Hidden Triple, Naked Quad, X-Wing, Swordfish, Simple Coloring, Y-Wing, XYZ-Wing, and XY-Chain. XY-Chain retains the paper implementation's 4–8-cell search bound; it is not an arbitrary-length chain search.
