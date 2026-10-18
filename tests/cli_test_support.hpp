#pragma once

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

// POSIX-only integration-test helpers. No shell, interpreter, or test framework
// is used. File-backed streams avoid pipe-buffer deadlocks when capturing output.
namespace cli_test {
namespace fs = std::filesystem;

inline const std::string puzzle =
    "530070000600195000098000060800060003400803001700020006060000280000419005000080079";
inline const std::string solution =
    "534678912672195348198342567859761423426853791713924856961537284287419635345286179";

inline void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

inline void check_posix(int code, const char* operation) {
    // posix_spawn functions return error numbers directly rather than in errno.
    if (code) throw std::system_error(code, std::generic_category(), operation);
}

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto pattern = (fs::temp_directory_path() / "hps CLI test XXXXXX").string();
        std::vector<char> name(pattern.begin(), pattern.end());
        name.push_back('\0');
        const char* created = ::mkdtemp(name.data());
        if (!created) throw std::system_error(errno, std::generic_category(), "mkdtemp");
        path_ = created;
    }
    ~TemporaryDirectory() {
        // Delete only the unique directory created by this object. Cleanup must
        // not throw while reporting a failed assertion or process launch.
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

inline void write_file(const fs::path& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary);
    output << content;
    output.flush();
    require(bool(output), "cannot write test fixture: " + path.string());
}

inline std::string read_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "cannot read captured output: " + path.string());
    std::string content{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    require(!input.bad(), "failed to read captured output: " + path.string());
    return content;
}

class SpawnFiles {
public:
    SpawnFiles() { check_posix(::posix_spawn_file_actions_init(&actions_), "spawn actions init"); }
    ~SpawnFiles() { ::posix_spawn_file_actions_destroy(&actions_); }
    SpawnFiles(const SpawnFiles&) = delete;
    SpawnFiles& operator=(const SpawnFiles&) = delete;
    void open(int descriptor, const fs::path& path, int flags) {
        check_posix(::posix_spawn_file_actions_addopen(
            &actions_, descriptor, path.c_str(), flags, 0600), "spawn stream open");
    }
    const posix_spawn_file_actions_t* get() const { return &actions_; }

private:
    posix_spawn_file_actions_t actions_;
};

inline int wait_for_child(pid_t child) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    bool reaped = false;
    try {
        for (;;) {
            int status = 0;
            const pid_t result = ::waitpid(child, &status, WNOHANG);
            if (result == child) {
                reaped = true;
                require(WIFEXITED(status), "CLI terminated by a signal");
                return WEXITSTATUS(status);
            }
            if (result < 0 && errno != EINTR) {
                const int code = errno;
                reaped = code == ECHILD;
                throw std::system_error(code, std::generic_category(), "waitpid");
            }
            require(std::chrono::steady_clock::now() < deadline, "CLI timed out after 30 seconds");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    } catch (...) {
        // A timeout or failed wait must not leave a background solver behind.
        // Never signal an already-reaped PID, which could have been reused.
        if (!reaped) {
            ::kill(child, SIGKILL);
            while (::waitpid(child, nullptr, 0) < 0 && errno == EINTR) {}
        }
        throw;
    }
}

struct Result {
    int code;
    std::string output;
    std::string error;
};

inline Result run_cli(const fs::path& executable, const std::vector<std::string>& options,
                      const std::string& input = "bad\n", bool unwritable_output = false) {
    TemporaryDirectory capture;
    const auto input_path = capture.path() / "stdin.txt";
    const auto output_path = capture.path() / "stdout.txt";
    const auto error_path = capture.path() / "stderr.txt";
    write_file(input_path, input);

    SpawnFiles streams;
    streams.open(STDIN_FILENO, input_path, O_RDONLY);
    // Opening /dev/null read-only gives a valid descriptor whose writes fail
    // deterministically, including on macOS where /dev/full is unavailable.
    streams.open(STDOUT_FILENO, unwritable_output ? fs::path("/dev/null") : output_path,
                 unwritable_output ? O_RDONLY : O_WRONLY | O_CREAT | O_TRUNC);
    streams.open(STDERR_FILENO, error_path, O_WRONLY | O_CREAT | O_TRUNC);

    std::vector<std::string> arguments{fs::absolute(executable).string()};
    arguments.insert(arguments.end(), options.begin(), options.end());
    std::vector<char*> argv;
    for (auto& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);

    pid_t child = -1;
    check_posix(::posix_spawn(&child, argv[0], streams.get(), nullptr, argv.data(), environ),
                "launch CLI");
    const int code = wait_for_child(child);
    return {code, unwritable_output ? std::string() : read_file(output_path), read_file(error_path)};
}
} // namespace cli_test
