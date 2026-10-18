#include "cli_test_support.hpp"

#include <iostream>

int main(int argc, char** argv) {
    using namespace cli_test;
    try {
        require(argc == 2, "usage: cli_output_test CLI_PATH");
        const fs::path executable = argv[1];
        struct Case {
            std::vector<std::string> options;
            std::string input;
            int code;
            std::string output;
            bool help = false;
        };
        const std::vector<Case> cases = {
            {{}, puzzle + "\n", 0, solution + "\n"},
            {{"--canonical"}, puzzle + "\n", 0, solution + "\n"},
            {{"--threads", "2"}, puzzle + "\n", 0, solution + "\n"},
            {{}, "bad\n", 1, "INVALID_INPUT\n"},
            {{}, std::string(81, '0') + "\n", 1, "STALLED\n"},
            {{"--help"}, "", 0, "", true},
        };
        for (const auto& test : cases) {
            const auto normal = run_cli(executable, test.options, test.input);
            require(normal.code == test.code && normal.error.empty(),
                    "normal invocation failed: " + normal.error);
            if (test.help) require(normal.output.rfind("usage:", 0) == 0, "help output missing");
            else require(normal.output == test.output, "normal output changed");

            // Include tiny buffered writes and help output, not just solved grids.
            const auto failed = run_cli(executable, test.options, test.input, true);
            require(failed.code == 2 && failed.error.find("failed to write output") != std::string::npos,
                    "output failure not reported: " + failed.error);
        }
        std::cout << cases.size() << " normal and " << cases.size()
                  << " failed-output cases passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
