#include "hps.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

/* This executable is an input/output wrapper, not another Sudoku engine.
 *
 * Phase 1: parse options and read puzzles from a text file or standard input.
 * Phase 2: solve each puzzle via the same C API, inline or on worker threads.
 * Phase 3: join workers, then print one ordered result line per input token.
 *
 * Each process creates its workers once for the supplied input collection;
 * this is not a long-lived thread-pool/session API. It intentionally prints
 * statuses instead of JSON and does not measure or claim paper benchmark times.
 */
namespace {
// One output slot per input. Workers never append to or resize this container,
// and each slot is written by exactly one worker before the main thread reads it.
struct Output {
    std::array<char, 82> grid{};
    int32_t code = HPS_INTERNAL_ERROR;
    int32_t status = HPS_STALLED;
};

unsigned parse_workers(const std::string& text) {
    // Reject signs, fractions, and empty values before numeric conversion.
    // The 256 cap is a defensive input bound, not a recommendation to launch
    // that many threads or a statement about the machine's useful parallelism.
    if (text.empty() || !std::all_of(text.begin(), text.end(),
        [](char value) { return value >= '0' && value <= '9'; }))
        throw std::invalid_argument("worker count must be an integer from 0 to 256");
    const auto workers = std::stoul(text);
    if (workers > 256) throw std::invalid_argument("worker count exceeds 256");
    return static_cast<unsigned>(workers);
}

const char* error_name(const Output& result) {
    // Check the call-level code before the algorithm's status. Invalid input
    // may leave status initialized to STALLED, but must print INVALID_INPUT.
    if (result.code == HPS_INVALID_INPUT) return "INVALID_INPUT";
    if (result.code != HPS_OK) return "ERROR";
    if (result.status == HPS_STALLED) return "STALLED";
    return "BUDGET_EXCEEDED";
}

void flush_output() {
    // Writes may stay buffered, and iostream failures do not throw by default.
    // Flush before choosing the exit code so a failed write cannot look successful.
    std::cout.flush();
    if (!std::cout) throw std::runtime_error("failed to write output");
}
} // namespace

int main(int argc, char** argv) {
    try {
        // The default is the paper's cost-aware order, with no worker threads.
        // Changing the worker count does not change the per-puzzle solve logic.
        unsigned workers = 0;
        std::string input_path;
        auto solve = hps_solve_grid_scheduled_strategies;
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--canonical") {
                solve = hps_solve_grid_batched_strategies;
            } else if (argument == "--threads" && index + 1 < argc) {
                workers = parse_workers(argv[++index]);
            } else if (argument == "--input" && index + 1 < argc) {
                input_path = argv[++index];
                if (input_path.empty()) throw std::invalid_argument("input file path is empty");
            } else if (argument == "--help") {
                std::cout << "usage: hps_final_cli [--input PATH] [--canonical] [--threads 0..256]\n"
                    "Read whitespace-separated 81-digit puzzles from PATH, or stdin if omitted.\n"
                    "Write one solution or status per puzzle, in input order.\n"
                    "0 workers (default) runs in the calling thread.\n";
                flush_output();
                return 0;
            } else {
                throw std::invalid_argument("unknown or incomplete option: " + argument);
            }
        }

        // Read input completely before launching workers. The immutable puzzle
        // strings and pre-sized result slots remain at stable addresses.
        // An explicit file replaces stdin; relative paths use the working directory.
        std::ifstream input_file;
        std::istream* input = &std::cin;
        if (!input_path.empty()) {
            input_file.open(input_path);
            if (!input_file) throw std::runtime_error("cannot open input file: " + input_path);
            input = &input_file;
        }
        std::vector<std::string> puzzles;
        // operator>> accepts newlines/spaces and treats each token as one puzzle.
        // Content validation stays in the C API, which returns a per-puzzle
        // error rather than aborting the remaining input when one token is bad.
        for (std::string puzzle; *input >> puzzle;) puzzles.push_back(std::move(puzzle));
        // EOF is normal; an I/O failure must not silently solve just a prefix.
        if (input->bad() || (input->fail() && !input->eof()))
            throw std::runtime_error("failed to read input" +
                (input_path.empty() ? std::string(" from stdin") : " file: " + input_path));
        if (puzzles.empty()) throw std::invalid_argument(input_path.empty()
            ? "no puzzles on stdin" : "no puzzles in input file: " + input_path);
        std::vector<Output> outputs(puzzles.size());
        std::atomic<size_t> next{0};
        const auto work = [&] {
            for (;;) {
                // The counter only allocates distinct task indices. Relaxed
                // ordering is enough: it does not publish results or state.
                const size_t index = next.fetch_add(1, std::memory_order_relaxed);
                // fetch_add returns the OLD index; every worker receives a
                // distinct one. Finishing a short puzzle lets that worker take
                // another task instead of waiting behind a fixed chunk of work.
                if (index >= puzzles.size()) return;
                auto& result = outputs[index];
                // The C API creates a fresh State/cache per call. Only the
                // topology and peer masks are shared, and they are read-only.
                result.code = solve(puzzles[index].data(), puzzles[index].size(),
                                    result.grid.data(), &result.status);
            }
        };

        std::vector<std::thread> threads;
        // If thread creation fails, join already-started workers before unwinding.
        try {
            for (unsigned index = 0; index < workers; ++index) threads.emplace_back(work);
        } catch (...) {
            for (auto& thread : threads) thread.join();
            throw;
        }
        // 0 means execute on this thread, not "skip all puzzles". join() is
        // the synchronization boundary before reading any worker-written slots.
        if (workers == 0) work();
        for (auto& thread : threads) thread.join();

        // Print only after all workers finish, in original input-index order.
        // Completion order can differ without interleaving or reordering output.
        bool failed = false;
        for (const auto& result : outputs) {
            if (result.code == HPS_OK && result.status == HPS_SOLVED) {
                std::cout << result.grid.data() << '\n';
            } else {
                std::cout << error_name(result) << '\n';
                failed = true;
            }
        }
        flush_output();
        // A valid invocation with any unsolved/bad puzzle returns 1 after
        // printing ALL result lines. Option or output failures below return 2.
        return failed ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
