#include "stdio_transport.hpp"
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace mcp {
namespace {
[[noreturn]] void system_error(const char* operation) {
    throw std::system_error(errno, std::generic_category(), operation);
}

void close_fd(int& fd) noexcept {
    if (fd >= 0) ::close(fd);
    fd = -1;
}

struct Pipe {
    int read = -1;
    int write = -1;
    Pipe() {
        int fds[2];
        if (::pipe(fds) < 0) system_error("pipe");
        read = fds[0]; write = fds[1];
        try {
            // Keep descriptors above stderr even if the caller closed stdin/stdout.
            for (int* fd : {&read, &write}) {
                if (*fd <= STDERR_FILENO) {
                    const int replacement = fcntl(*fd, F_DUPFD, 3);
                    if (replacement < 0) system_error("duplicate pipe descriptor");
                    close_fd(*fd);
                    *fd = replacement;
                }
                if (fcntl(*fd, F_SETFD, FD_CLOEXEC) < 0) system_error("pipe close-on-exec");
            }
        } catch (...) { close_fd(read); close_fd(write); throw; }
    }
    ~Pipe() { close_fd(read); close_fd(write); }
    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;
};

void nonblocking(int fd) {
    const int flags = fcntl(fd, F_GETFL);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) system_error("nonblocking pipe");
}

void wait_ready(int fd, short events, StdioTransport::Deadline until) {
    for (;;) {
        const auto left = until - StdioTransport::Clock::now();
        if (left <= StdioTransport::Clock::duration::zero()) throw std::runtime_error("MCP transport timed out");
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(left).count() + 1;
        pollfd descriptor{fd, events, 0};
        const int ready = poll(&descriptor, 1, static_cast<int>(std::min<long long>(milliseconds, INT_MAX)));
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) system_error("poll");
        if (ready == 0) continue;
        if (descriptor.revents & POLLNVAL) throw std::runtime_error("MCP pipe is closed");
        // Let read/write distinguish buffered data, EOF, and EPIPE after HUP/ERR.
        if (descriptor.revents & (events | POLLHUP | POLLERR)) return;
    }
}

// Suppress only this thread's write-generated SIGPIPE; don't alter global handlers.
ssize_t write_without_sigpipe(int fd, const void* data, std::size_t size) {
    sigset_t blocked, previous, pending;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGPIPE);
    const int result = pthread_sigmask(SIG_BLOCK, &blocked, &previous);
    if (result != 0) throw std::system_error(result, std::generic_category(), "block SIGPIPE");
    sigpending(&pending);
    const bool already_pending = sigismember(&pending, SIGPIPE) == 1;
    const auto written = ::write(fd, data, size);
    const int saved_errno = errno;
    if (written < 0 && saved_errno == EPIPE && !already_pending) {
        sigpending(&pending);
        if (sigismember(&pending, SIGPIPE) == 1) {
            int signal;
            sigwait(&blocked, &signal);
        }
    }
    pthread_sigmask(SIG_SETMASK, &previous, nullptr);
    errno = saved_errno;
    return written;
}

bool wait_for_exit(pid_t pid, std::chrono::milliseconds duration) noexcept {
    const auto until = StdioTransport::Clock::now() + duration;
    do {
        const auto result = waitpid(pid, nullptr, WNOHANG);
        if (result == pid || (result < 0 && errno == ECHILD)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (StdioTransport::Clock::now() < until);
    return false;
}
}

StdioTransport::StdioTransport(const config::McpServerConfig& config) : timeout_ms_(config.timeout_ms) {
    if (config.command.empty() || timeout_ms_ <= 0) throw std::invalid_argument("Invalid MCP transport configuration");
    // Prepare all strings before fork; the child only uses async-signal-safe calls.
    std::map<std::string, std::string> environment;
    for (char** current = environ; *current; ++current) {
        const std::string item(*current);
        const auto equal = item.find('=');
        if (equal != std::string::npos) environment[item.substr(0, equal)] = item.substr(equal + 1);
    }
    for (const auto& [key, value] : config.env) environment[key] = value;
    std::vector<std::string> env_strings;
    for (const auto& [key, value] : environment) env_strings.push_back(key + "=" + value);
    std::vector<char*> envp;
    for (auto& item : env_strings) envp.push_back(item.data());
    envp.push_back(nullptr);
    std::vector<std::string> arguments{config.command};
    arguments.insert(arguments.end(), config.args.begin(), config.args.end());
    std::vector<char*> argv;
    for (auto& arg : arguments) argv.push_back(arg.data());
    argv.push_back(nullptr);
    const std::string cwd = config.cwd.string();
    std::vector<std::string> candidates;
    if (config.command.find('/') != std::string::npos) candidates.push_back(config.command);
    else {
        const std::string path = environment.contains("PATH") ? environment.at("PATH") : "/usr/bin:/bin";
        std::size_t start = 0;
        do {
            const auto end = path.find(':', start);
            const auto dir = path.substr(start, end - start);
            candidates.push_back((dir.empty() ? "." : dir) + "/" + config.command);
            if (end == std::string::npos) break;
            start = end + 1;
        } while (true);
    }
    std::vector<const char*> executables;
    for (const auto& candidate : candidates) executables.push_back(candidate.c_str());
    const auto candidate_count = executables.size();
    const auto candidate_data = executables.data();
    const auto argument_data = argv.data();
    const auto environment_data = envp.data();
    const auto working_directory = cwd.c_str();
    const bool change_directory = !cwd.empty();

    Pipe to_child, from_child, launch_error;
    const auto until = deadline();
    pid_ = fork();
    if (pid_ < 0) system_error("fork MCP server");
    if (pid_ == 0) {
        // Errors cross a CLOEXEC pipe; successful exec closes it automatically.
        int error = 0;
        if (setpgid(0, 0) < 0 ||
            (change_directory && chdir(working_directory) < 0) ||
            dup2(to_child.read, STDIN_FILENO) < 0 || dup2(from_child.write, STDOUT_FILENO) < 0) {
            error = errno;
        } else {
            ::close(to_child.read); ::close(to_child.write);
            ::close(from_child.read); ::close(from_child.write);
            ::close(launch_error.read);
            // stderr remains inherited, never mixed with protocol stdout.
            for (std::size_t i = 0; i < candidate_count; ++i) {
                execve(candidate_data[i], argument_data, environment_data);
                if (errno != ENOENT && errno != ENOTDIR && errno != EACCES) { error = errno; break; }
                if (errno == EACCES) error = EACCES;
            }
            if (!error) error = ENOENT;
        }
        const char* bytes = reinterpret_cast<const char*>(&error);
        std::size_t sent = 0;
        while (sent < sizeof(error)) {
            const auto count = ::write(launch_error.write, bytes + sent, sizeof(error) - sent);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) break;
            sent += static_cast<std::size_t>(count);
        }
        _exit(127);
    }
    // Parent and child both establish the group to avoid a startup cleanup race.
    setpgid(pid_, pid_);
    close_fd(to_child.read); close_fd(from_child.write); close_fd(launch_error.write);
    input_ = to_child.write; to_child.write = -1;
    output_ = from_child.read; from_child.read = -1;
    try {
        nonblocking(input_); nonblocking(output_); nonblocking(launch_error.read);
        int error = 0;
        std::size_t received = 0;
        while (received < sizeof(error)) {
            wait_ready(launch_error.read, POLLIN, until);
            const auto count = ::read(launch_error.read, reinterpret_cast<char*>(&error) + received, sizeof(error) - received);
            if (count < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            if (count < 0) system_error("read MCP launch status");
            if (count == 0) {
                if (received != 0) throw std::runtime_error("Incomplete MCP launch error");
                break;
            }
            received += static_cast<std::size_t>(count);
        }
        if (received) throw std::system_error(error, std::generic_category(), "launch MCP server " + config.name);
    } catch (...) { close(); throw; }
}

StdioTransport::~StdioTransport() { close(); }

StdioTransport::Deadline StdioTransport::deadline() const {
    return Clock::now() + std::chrono::milliseconds(timeout_ms_);
}

void StdioTransport::send(const nlohmann::json& message, Deadline until) {
    if (!is_open()) throw std::runtime_error("MCP transport is closed");
    if (!message.is_object()) throw std::invalid_argument("MCP message must be a JSON object");
    std::string bytes = message.dump();
    if (bytes.size() > max_message_bytes) throw std::invalid_argument("MCP message exceeds 1 MiB limit");
    bytes.push_back('\n');
    try {
        std::size_t sent = 0;
        while (sent < bytes.size()) {
            wait_ready(input_, POLLOUT, until);
            const auto count = write_without_sigpipe(input_, bytes.data() + sent, bytes.size() - sent);
            if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
            if (count < 0) system_error("write MCP message");
            if (count == 0) throw std::runtime_error("MCP write made no progress");
            sent += static_cast<std::size_t>(count);
        }
    } catch (...) { close(); throw; }
}

nlohmann::json StdioTransport::receive(Deadline until) {
    if (!is_open()) throw std::runtime_error("MCP transport is closed");
    try {
        for (;;) {
            const auto newline = pending_.find('\n');
            if (newline != std::string::npos) {
                if (newline > max_message_bytes) throw std::runtime_error("MCP message exceeds 1 MiB limit");
                auto message = nlohmann::json::parse(pending_.substr(0, newline));
                pending_.erase(0, newline + 1);
                if (!message.is_object()) throw std::runtime_error("MCP message must be a JSON object");
                return message;
            }
            if (pending_.size() > max_message_bytes) throw std::runtime_error("MCP message exceeds 1 MiB limit");
            wait_ready(output_, POLLIN, until);
            char buffer[4096];
            const auto count = ::read(output_, buffer, sizeof(buffer));
            if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
            if (count < 0) system_error("read MCP message");
            if (count == 0) throw std::runtime_error(pending_.empty() ? "MCP server closed stdout" : "MCP server closed stdout mid-message");
            pending_.append(buffer, static_cast<std::size_t>(count));
        }
    } catch (...) { close(); throw; }
}

void StdioTransport::close() noexcept {
    close_fd(input_);
    close_fd(output_);
    pending_.clear();
    if (pid_ <= 0) return;
    using namespace std::chrono_literals;
    if (!wait_for_exit(pid_, 100ms)) {
        kill(-pid_, SIGTERM);
        // Also signal the child if process-group setup failed during startup.
        kill(pid_, SIGTERM);
        if (!wait_for_exit(pid_, 200ms)) {
            kill(-pid_, SIGKILL);
            kill(pid_, SIGKILL);
            while (waitpid(pid_, nullptr, 0) < 0 && errno == EINTR) {}
        }
    }
    pid_ = -1;
}
}
