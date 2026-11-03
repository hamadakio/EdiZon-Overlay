#pragma once

#if __has_include(<switch.h>)
#include <switch.h>
#else
#include <stdint.h>
typedef uint32_t Result;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#endif

#include <string>
#include <vector>

namespace edz::cheat {

    enum class CheatDownloadStatus {
        Success,
        NoGameRunning,
        NoInternet,
        DownloadFailed,
        ParseFailed,
        BuildIdNotFound,
        WriteFailed
    };

    struct DownloadResult {
        CheatDownloadStatus status;
        std::vector<std::string> availableBuildIds;
        std::string message;
        int cheatsCount = 0;
    };

    class CheatDownloader {
    public:
        // Checks whether the console is connected to the internet
        static bool isInternetConnected();

        // Gets the Atmosphere contents cheat directory for a title
        static std::string getCheatDirectory(u64 titleId);

        // Gets list of local cheat Build IDs for a title (.txt filenames without extension)
        static std::vector<std::string> getLocalCheatBuildIDs(u64 titleId);

        // Copies a local cheat file from another Build ID to current Build ID and reloads
        static bool copyLocalCheatToCurrent(u64 titleId, const std::string& srcBuildId, u64 currentBuildId);

        // Downloads cheats matching the exact Title ID and current running Build ID
        static DownloadResult downloadCheatsForGame(u64 titleId, u64 buildId);

        // Fallback: downloads/extracts cheats from DB for a selected Build ID and writes them as targetBuildId
        static DownloadResult applyCheatsFromDbVersion(u64 titleId, const std::string& sourceBuildIdInDb, u64 targetBuildId);
    };

}
