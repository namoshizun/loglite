#ifndef LOGLITE_HARVESTERS_FILE_HPP_
#define LOGLITE_HARVESTERS_FILE_HPP_

#include "base.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <format>
#include <thread>

namespace loglite::harvesters {

using namespace std::chrono_literals;

// ── FileHarvester ──────────────────────────────────────────────────────────────
//
// Tails a file (like `tail -F`): opens at EOF, reads new lines as they arrive,
// detects rotation (inode change) and truncation.  Each line is parsed as JSON
// and pushed to the Backlog.

class FileHarvester final : public Harvester {
   public:
    FileHarvester(std::string name, std::filesystem::path path, Backlog& backlog)
        : Harvester(std::move(name), backlog), path_(std::move(path)) {}

    void Start() override {
        thread_ = std::jthread{[this](std::stop_token st) { run(st); }};
        log::info(std::format("FileHarvester '{}' started: tailing {}", name_, path_.string()));
    }

    void Stop() override {
        thread_.request_stop();
        thread_.join();
        log::info(std::format("FileHarvester '{}' stopped", name_));
    }

   private:
    void run(std::stop_token st) {
        // Wait for the file to exist.
        while (!std::filesystem::exists(path_)) {
            if (st.stop_requested()) return;
            log::debug(std::format("FileHarvester '{}': waiting for {}", name_, path_.string()));
            std::this_thread::sleep_for(5s);
        }

        // Seek to end of current file.
        auto inode = get_inode(path_);
        auto offset = std::filesystem::file_size(path_);

        while (!st.stop_requested()) {
            if (!std::filesystem::exists(path_)) {
                std::this_thread::sleep_for(500ms);
                continue;
            }

            auto new_inode = get_inode(path_);
            auto new_size = std::filesystem::file_size(path_);

            if (new_inode != inode) {
                log::info(std::format("FileHarvester '{}': file rotated, reopening", name_));
                inode = new_inode;
                offset = 0;
            } else if (new_size < offset) {
                log::warn(std::format("FileHarvester '{}': file truncated, resetting", name_));
                offset = 0;
            }

            if (new_size == offset) {
                std::this_thread::sleep_for(500ms);
                continue;
            }

            // Read new lines.
            std::ifstream file{path_, std::ios::binary};
            file.seekg(static_cast<std::streamoff>(offset));

            std::string line;
            while (std::getline(file, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty()) process_line(line);
            }
            // std::getline leaves eofbit|failbit set after exhausting the file,
            // which makes tellg() return -1. Clear those bits first so that
            // tellg() reports the actual stream position (= end of new data).
            // But if if the seek truly failed, leave offset
            // unchanged rather than wrapping it to SIZE_MAX.
            file.clear();
            if (auto pos = file.tellg(); pos != std::streampos{-1})
                offset = static_cast<std::uintmax_t>(pos);
        }
    }

    void process_line(const std::string& line) {
        try {
            auto entry = nlohmann::json::parse(line);
            if (!entry.contains("timestamp")) {
                // Add UTC timestamp if missing.
                auto now = std::chrono::system_clock::now();
                entry["timestamp"] = std::format("{:%Y-%m-%dT%H:%M:%SZ}", now);
            }
            Ingest(std::move(entry));
        } catch (const nlohmann::json::parse_error&) {
            log::warn(std::format("FileHarvester '{}': failed to parse line: {}", name_, line));
        }
    }

    static std::uintmax_t get_inode(const std::filesystem::path& p) {
#if defined(_WIN32)
        return 0;  // inodes not meaningful on Windows
#else
        struct stat st{};
        ::stat(p.c_str(), &st);
        return static_cast<std::uintmax_t>(st.st_ino);
#endif
    }

    std::filesystem::path path_;
    std::jthread thread_;
};

}  // namespace loglite::harvesters

#endif  // LOGLITE_HARVESTERS_FILE_HPP_
