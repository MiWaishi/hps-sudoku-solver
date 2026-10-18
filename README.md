# Fast Human-Strategy Sudoku Solver (HPS)

A C++ Sudoku solver designed to make human-style deductions fast. HPS uses 15 named logical strategies without guessing or backtracking, and focuses on reducing repeated work in strategy detection and candidate tracking.

## Approach

The solver represents candidates compactly and maintains indexes of where each digit can appear. It applies compatible deductions in batches, updates state incrementally, delays full-board validation until needed, caches some failed checks, and schedules strategies with attention to their cost. Independent puzzles can be processed across worker threads.

The 15 strategies span singles, subsets, intersections, fish patterns, coloring, wings, and chains. Examples include Naked and Hidden Singles, Naked and Hidden Pairs, X-Wing, Swordfish, Simple Coloring, Y-Wing, XYZ-Wing, and XY-Chain.

## Results

The evaluation used 10,000 distinct puzzles from four difficulty categories of the Sudoku Exchange puzzle bank. All were solvable by an earlier 15-strategy solver, so these results measure speed on supported puzzles, not coverage of arbitrary Sudoku puzzles. Returned grids were checked against expected answers.

| Configuration | Time for 10,000 puzzles | Speedup vs. serial baseline |
| --- | ---: | ---: |
| Serial C++ baseline | 16.717 s | 1.0× |
| Serial HPS | 0.475 s | 35.2× |
| HPS, 11 worker threads | 0.076 s | 220.4× |

Measurements were made on an Apple M3 Pro with release build settings. The 76 ms figure is batch throughput with 11 workers; it is not single-puzzle latency. Performance may differ on other machines or puzzle collections.

## Scope

HPS returns answers using its configured logical strategies. A puzzle requiring other techniques may cause it to stall. The reported benchmarks use an answer-only interface; generating full written explanations has not been benchmarked.

This repository currently contains a project overview. The implementation and research paper can be added here separately.
