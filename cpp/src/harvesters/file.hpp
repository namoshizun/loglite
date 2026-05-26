#ifndef LOGLITE_HARVESTERS_FILE_HPP_
#define LOGLITE_HARVESTERS_FILE_HPP_

#include "base.hpp"
#include "../utils.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>
#include <sys/stat.h>
#include <sys/types.h>

namespace loglite::harvesters {

// ── FileHarvester ──────────────────────────────────────────────────────────────
//
// Tails a file, similar to `tail -F`.
//
// Behavior:
//   - Initial open starts at EOF by default.
//   - New files after rotation are read from the beginning.
//   - Complete newline-delimited JSON objects are parsed and ingested.
//   - Partial lines are buffered across polling iterations.
//   - Overlong lines are dropped.
//   - Truncation and rotation are detected.
//   - Old file is briefly drained after rotation before switching.
//   - Filesystem errors are handled without killing the worker thread.

class FileHarvester final : public Harvester {
   public:
    struct Options {
        // How often to poll when the file exists.
        std::chrono::milliseconds poll_interval{500};

        // How often to retry while the file is missing.
        std::chrono::milliseconds missing_file_interval{5000};

        // How long to keep draining the old file after path identity changes.
        // This reduces the chance of losing final writes to the rotated file.
        std::chrono::milliseconds rotation_drain_grace{2000};

        // Read buffer size for file I/O.
        std::size_t read_buffer_size{64 * 1024};

        // Maximum accepted JSON line size.
        std::size_t max_line_bytes{1024 * 1024};

        // On the first successful open, start at EOF.
        // After rotation/recreation, the new file is always read from offset 0.
        bool start_at_end{true};

        // Ignore blank lines.
        bool ignore_empty_lines{true};

        // If a file is rotated/truncated/shutdown while a partial line is buffered,
        // try to process that partial line.
        bool flush_partial_line_on_close{true};
    };

    FileHarvester(std::string name, std::filesystem::path path, Backlog& backlog)
        : Harvester(std::move(name), backlog), path_(std::move(path)) {}

    FileHarvester(std::string name, std::filesystem::path path, Backlog& backlog, Options options)
        : Harvester(std::move(name), backlog),
          path_(std::move(path)),
          options_(normalize_options(options)) {}

    void Start() override {
        std::lock_guard lock(lifecycle_mutex_);

        if (thread_.joinable()) {
            log::WARN("FileHarvester '{}': Start() called while already running", name_);
            return;
        }

        thread_ = std::jthread{[this](std::stop_token st) noexcept { run_safely(st); }};

        log::INFO("FileHarvester '{}' started: tailing {}", name_, path_.string());
    }

    void Stop() override {
        std::lock_guard lock(lifecycle_mutex_);

        if (!thread_.joinable()) {
            return;
        }

        if (thread_.get_id() == std::this_thread::get_id()) {
            // Avoid self-join deadlock. The jthread destructor will still request
            // stop/join when owned elsewhere; this path is defensive.
            thread_.request_stop();
            return;
        }

        thread_.request_stop();
        thread_.join();

        log::INFO("FileHarvester '{}' stopped", name_);
    }

   private:
    struct FileIdentity {
        std::uint64_t a{};
        std::uint64_t b{};
        std::uint64_t c{};

        friend bool operator==(const FileIdentity&, const FileIdentity&) = default;
    };

    struct OpenFile {
        std::ifstream stream;
        FileIdentity identity{};
        std::uintmax_t offset{};
        std::string pending_line;
        bool dropping_overlong_line{false};
    };

    static Options normalize_options(Options options) {
        if (options.poll_interval.count() <= 0) {
            options.poll_interval = std::chrono::milliseconds{500};
        }

        if (options.missing_file_interval.count() <= 0) {
            options.missing_file_interval = std::chrono::milliseconds{5000};
        }

        if (options.read_buffer_size == 0) {
            options.read_buffer_size = 64 * 1024;
        }

        if (options.max_line_bytes == 0) {
            options.max_line_bytes = 1024 * 1024;
        }

        return options;
    }

    void run_safely(std::stop_token st) noexcept {
        try {
            run(st);
        } catch (const std::exception& e) {
            log::ERROR("FileHarvester '{}': worker terminated unexpectedly: {}", name_, e.what());
        } catch (...) {
            log::ERROR("FileHarvester '{}': worker terminated due to unknown exception", name_);
        }
    }

    void run(std::stop_token st) {
        std::optional<OpenFile> current;
        bool first_open = true;

        std::vector<char> buffer(options_.read_buffer_size);

        while (!st.stop_requested()) {
            if (!current.has_value()) {
                const bool open_at_end = first_open && options_.start_at_end;

                current = open_file(open_at_end);

                if (!current.has_value()) {
                    log::DEBUG("FileHarvester '{}': waiting for {}", name_, path_.string());
                    responsive_sleep(st, options_.missing_file_interval);
                    continue;
                }

                first_open = false;

                log::INFO("FileHarvester '{}': opened {} at offset {}", name_, path_.string(),
                          current->offset);
            }

            const auto bytes_read = read_available(*current, buffer, st);

            if (st.stop_requested()) {
                break;
            }

            const auto path_identity = current_path_identity();

            if (!path_identity.has_value()) {
                // The path disappeared. Keep the already-open stream alive briefly
                // so final bytes written to the old file can still be consumed.
                log::WARN("FileHarvester '{}': path disappeared: {}", name_, path_.string());

                drain_for_grace_period(*current, buffer, st);

                close_file(*current, "path disappeared");
                current.reset();

                responsive_sleep(st, options_.poll_interval);
                continue;
            }

            if (*path_identity != current->identity) {
                log::INFO("FileHarvester '{}': file rotated/replaced: {}", name_, path_.string());

                drain_for_grace_period(*current, buffer, st);

                close_file(*current, "rotation");
                current.reset();

                // Next loop opens the new file from beginning.
                continue;
            }

            const auto size = safe_file_size(path_);

            if (size.has_value() && *size < current->offset) {
                log::WARN("FileHarvester '{}': file truncated, resetting offset", name_);

                flush_or_drop_pending_line(*current, "truncation");

                current->offset = 0;
                current->dropping_overlong_line = false;

                current->stream.clear();
                current->stream.seekg(0, std::ios::beg);
            }

            if (bytes_read == 0) {
                responsive_sleep(st, options_.poll_interval);
            }
        }

        if (current.has_value()) {
            close_file(*current, "shutdown");
        }
    }

    std::optional<OpenFile> open_file(bool at_end) {
        const auto identity_before_open = current_path_identity();
        if (!identity_before_open.has_value()) {
            return std::nullopt;
        }

        auto file = std::ifstream{path_, std::ios::binary};
        if (!file.is_open()) {
            log::WARN("FileHarvester '{}': failed to open {}", name_, path_.string());
            return std::nullopt;
        }

        const auto identity_after_open = current_path_identity();
        if (!identity_after_open.has_value()) {
            return std::nullopt;
        }

        if (*identity_before_open != *identity_after_open) {
            // Path changed while opening. Retry on next polling iteration.
            log::WARN("FileHarvester '{}': file changed while opening {}", name_, path_.string());
            return std::nullopt;
        }

        std::uintmax_t offset = 0;

        if (at_end) {
            const auto size = safe_file_size(path_);
            if (!size.has_value()) {
                return std::nullopt;
            }
            offset = *size;
        }

        if (!seek_to(file, offset)) {
            log::WARN("FileHarvester '{}': failed to seek {} to offset {}", name_, path_.string(),
                      offset);
            return std::nullopt;
        }

        OpenFile opened;
        opened.stream = std::move(file);
        opened.identity = *identity_after_open;
        opened.offset = offset;

        return opened;
    }

    std::size_t read_available(OpenFile& file, std::vector<char>& buffer, std::stop_token st) {
        std::size_t total_read = 0;

        if (!file.stream.is_open()) {
            return 0;
        }

        if (!seek_to(file.stream, file.offset)) {
            log::WARN("FileHarvester '{}': failed to seek to offset {}", name_, file.offset);
            return 0;
        }

        while (!st.stop_requested()) {
            file.stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto n = file.stream.gcount();

            if (n <= 0) {
                break;
            }

            const auto bytes = static_cast<std::size_t>(n);
            total_read += bytes;
            file.offset += static_cast<std::uintmax_t>(bytes);

            consume_bytes(file, std::string_view{buffer.data(), bytes});

            if (bytes < buffer.size()) {
                break;
            }
        }

        return total_read;
    }

    void consume_bytes(OpenFile& file, std::string_view bytes) {
        for (char ch : bytes) {
            if (ch == '\n') {
                finish_line(file);
                continue;
            }

            if (file.dropping_overlong_line) {
                continue;
            }

            if (file.pending_line.size() >= options_.max_line_bytes) {
                log::WARN("FileHarvester '{}': dropping overlong line, max_line_bytes={}", name_,
                          options_.max_line_bytes);

                file.pending_line.clear();
                file.dropping_overlong_line = true;
                continue;
            }

            file.pending_line.push_back(ch);
        }
    }

    void finish_line(OpenFile& file) {
        if (file.dropping_overlong_line) {
            file.pending_line.clear();
            file.dropping_overlong_line = false;
            return;
        }

        if (!file.pending_line.empty() && file.pending_line.back() == '\r') {
            file.pending_line.pop_back();
        }

        if (!file.pending_line.empty() || !options_.ignore_empty_lines) {
            process_line_safely(file.pending_line);
        }

        file.pending_line.clear();
    }

    void drain_for_grace_period(OpenFile& file, std::vector<char>& buffer, std::stop_token st) {
        using Clock = std::chrono::steady_clock;

        const auto deadline = Clock::now() + options_.rotation_drain_grace;

        while (!st.stop_requested()) {
            const auto bytes_read = read_available(file, buffer, st);

            if (Clock::now() >= deadline) {
                break;
            }

            if (bytes_read == 0) {
                responsive_sleep(st,
                                 std::min(options_.poll_interval, std::chrono::milliseconds{100}));
            }
        }
    }

    void close_file(OpenFile& file, std::string_view reason) {
        flush_or_drop_pending_line(file, reason);

        if (file.stream.is_open()) {
            file.stream.close();
        }
    }

    void flush_or_drop_pending_line(OpenFile& file, std::string_view reason) {
        if (file.dropping_overlong_line) {
            file.pending_line.clear();
            file.dropping_overlong_line = false;
            return;
        }

        if (file.pending_line.empty()) {
            return;
        }

        if (options_.flush_partial_line_on_close) {
            log::WARN("FileHarvester '{}': flushing partial line on {}", name_, reason);
            process_line_safely(file.pending_line);
        } else {
            log::WARN("FileHarvester '{}': dropping partial line on {}", name_, reason);
        }

        file.pending_line.clear();
    }

    void process_line_safely(const std::string& line) noexcept {
        try {
            auto entry = nlohmann::json::parse(line);

            if (!entry.is_object()) {
                log::WARN("FileHarvester '{}': dropping non-object JSON line: {}", name_,
                          escaped_preview(line));
                return;
            }

            if (!entry.contains("timestamp") || !entry["timestamp"].is_string() ||
                entry["timestamp"].get_ref<const std::string&>().empty()) {
                entry["timestamp"] = format_utc(std::chrono::system_clock::now());
            }

            Ingest(std::move(entry));
        } catch (const nlohmann::json::parse_error& e) {
            log::WARN("FileHarvester '{}': failed to parse JSON line: {}; line={}", name_, e.what(),
                      escaped_preview(line));
        } catch (const std::exception& e) {
            log::ERROR("FileHarvester '{}': failed to process line: {}; line={}", name_, e.what(),
                       escaped_preview(line));
        } catch (...) {
            log::ERROR("FileHarvester '{}': failed to process line due to unknown error; line={}",
                       name_, escaped_preview(line));
        }
    }

    std::optional<FileIdentity> current_path_identity() const {
        std::error_code ec;

        if (!std::filesystem::exists(path_, ec) || ec) {
            return std::nullopt;
        }

        if (!std::filesystem::is_regular_file(path_, ec) || ec) {
            return std::nullopt;
        }

        return get_file_identity(path_);
    }

    static std::optional<std::uintmax_t> safe_file_size(const std::filesystem::path& path) {
        std::error_code ec;
        const auto size = std::filesystem::file_size(path, ec);

        if (ec) {
            return std::nullopt;
        }

        return size;
    }

    static bool seek_to(std::ifstream& file, std::uintmax_t offset) {
        if (offset > static_cast<std::uintmax_t>(std::numeric_limits<std::streamoff>::max())) {
            return false;
        }

        file.clear();
        file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);

        return static_cast<bool>(file);
    }

    static void responsive_sleep(std::stop_token st, std::chrono::milliseconds duration) {
        using Clock = std::chrono::steady_clock;

        const auto deadline = Clock::now() + duration;

        while (!st.stop_requested()) {
            const auto now = Clock::now();

            if (now >= deadline) {
                return;
            }

            const auto remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);

            std::this_thread::sleep_for(std::min(remaining, std::chrono::milliseconds{50}));
        }
    }

    static std::string escaped_preview(std::string_view s, std::size_t max_bytes = 512) {
        std::string out;
        out.reserve(std::min(s.size(), max_bytes) + 32);

        const auto n = std::min(s.size(), max_bytes);

        for (std::size_t i = 0; i < n; ++i) {
            const unsigned char c = static_cast<unsigned char>(s[i]);

            switch (c) {
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (std::isprint(c)) {
                    out.push_back(static_cast<char>(c));
                } else {
                    constexpr char hex[] = "0123456789ABCDEF";
                    out += "\\x";
                    out.push_back(hex[(c >> 4) & 0x0F]);
                    out.push_back(hex[c & 0x0F]);
                }
                break;
            }
        }

        if (s.size() > max_bytes) {
            out += "...";
        }

        return out;
    }

    static std::optional<FileIdentity> get_file_identity(const std::filesystem::path& path) {
        struct stat st{};

        if (::stat(path.c_str(), &st) != 0) {
            return std::nullopt;
        }

        if (!S_ISREG(st.st_mode)) {
            return std::nullopt;
        }

        return FileIdentity{
            static_cast<std::uint64_t>(st.st_dev),
            static_cast<std::uint64_t>(st.st_ino),
            0,
        };
    }

    std::filesystem::path path_;
    Options options_{};

    std::mutex lifecycle_mutex_;
    std::jthread thread_;
};

}  // namespace loglite::harvesters

#endif  // LOGLITE_HARVESTERS_FILE_HPP_