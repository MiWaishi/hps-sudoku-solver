#include "cli_test_support.hpp"

#include <iostream>

int main(int argc, char** argv) {
    using namespace cli_test;
    try {
        require(argc == 2, "usage: cli_input_test CLI_PATH");
        const fs::path executable = argv[1];

        const auto check = [&](const std::vector<std::string>& options, int code,
                               const std::string& output, const std::string& input = "bad\n") {
            const auto result = run_cli(executable, options, input);
            require(result.code == code && result.output == output,
                    "unexpected file-input result: " + result.error);
            require((code == 2) == !result.error.empty(), "unexpected input diagnostic");
        };

        TemporaryDirectory fixtures;
        const auto single = fixtures.path() / "one puzzle.txt";
        write_file(single, puzzle + "\n");
        const std::vector<std::vector<std::string>> modes = {
            {}, {"--canonical"}, {"--threads", "2"}, {"--canonical", "--threads", "2"}};
        for (const auto& mode : modes) {
            std::vector<std::string> options{"--input", single.string()};
            options.insert(options.end(), mode.begin(), mode.end());
            // Bad stdin is deliberate: --input must replace it, not append it.
            check(options, 0, solution + "\n");
        }
        check({}, 0, solution + "\n", puzzle + "\n");

        const auto mixed = fixtures.path() / "mixed puzzles.txt";
        write_file(mixed, "\r\n\t" + puzzle + " \r\n" + solution + "\t" +
                         std::string(81, '0') + "\r\nbad\r\n");
        const std::string expected = solution + "\n" + solution + "\nSTALLED\nINVALID_INPUT\n";
        check({"--input", mixed.string()}, 1, expected);
        check({"--input", mixed.string(), "--threads", "2"}, 1, expected);

        const auto empty = fixtures.path() / "empty.txt";
        write_file(empty, " \n\t");
        check({"--input", empty.string()}, 2, "");
        check({"--input", (fixtures.path() / "missing.txt").string()}, 2, "");
        check({"--input", fixtures.path().string()}, 2, "");
        check({"--input"}, 2, "");
        check({"--input", ""}, 2, "");

        const auto failed = run_cli(executable, {"--input", single.string()}, "", true);
        require(failed.code == 2 && failed.error.find("failed to write output") != std::string::npos,
                "file mode concealed an output error");
        std::cout << "text-file, stdin, threading, and I/O error checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
