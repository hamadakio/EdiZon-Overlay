#include "cheat_downloader.hpp"
#include "cheat.hpp"
#include "utils.hpp"

#include <download_funcs.hpp>
#include <path_funcs.hpp>
#include <json_funcs.hpp>
#include <cJSON.h>

#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace edz::cheat {

    static std::string toHex16(u64 val) {
        char buffer[17];
        snprintf(buffer, sizeof(buffer), "%016lX", val);
        return std::string(buffer);
    }

    bool CheatDownloader::isInternetConnected() {
        if (R_FAILED(nifmInitialize(NifmServiceType_User)))
            return false;
        NifmInternetConnectionType type;
        u32 strength;
        NifmInternetConnectionStatus status;
        bool connected = R_SUCCEEDED(nifmGetInternetConnectionStatus(&type, &strength, &status)) &&
                         (status == NifmInternetConnectionStatus_Connected);
        nifmExit();
        return connected;
    }

    std::string CheatDownloader::getCheatDirectory(u64 titleId) {
        return "/atmosphere/contents/" + toHex16(titleId) + "/cheats/";
    }

    std::vector<std::string> CheatDownloader::getLocalCheatBuildIDs(u64 titleId) {
        std::vector<std::string> results;
        std::string dirPath = getCheatDirectory(titleId);

        DIR *dir = opendir(dirPath.c_str());
        if (!dir) return results;

        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name.length() > 4 && name.substr(name.length() - 4) == ".txt") {
                std::string bid = name.substr(0, name.length() - 4);
                std::transform(bid.begin(), bid.end(), bid.begin(), ::toupper);
                results.push_back(bid);
            }
        }
        closedir(dir);
        return results;
    }

    bool CheatDownloader::copyLocalCheatToCurrent(u64 titleId, const std::string& srcBuildId, u64 currentBuildId) {
        std::string dirPath = getCheatDirectory(titleId);
        std::string srcPath = dirPath + srcBuildId + ".txt";
        std::string dstPath = dirPath + toHex16(currentBuildId) + ".txt";

        if (!ult::isFile(srcPath)) return false;

        ult::createDirectory(dirPath);
        ult::copyFileOrDirectory(srcPath, dstPath);

        CheatManager::forceReloadFromDisk();
        return true;
    }

    DownloadResult CheatDownloader::downloadCheatsForGame(u64 titleId, u64 buildId) {
        DownloadResult res;

        if (titleId == 0 || buildId == 0) {
            res.status = CheatDownloadStatus::NoGameRunning;
            res.message = "No game currently running!";
            return res;
        }

        if (!isInternetConnected()) {
            res.status = CheatDownloadStatus::NoInternet;
            res.message = "No internet connection!";
            return res;
        }

        std::string titleHex = toHex16(titleId);
        std::string buildHex = toHex16(buildId);

        std::string cheatsDir = getCheatDirectory(titleId);
        ult::createDirectory(cheatsDir);

        std::string tempJsonPath = cheatsDir + "db_temp.json";
        std::string url = "https://raw.githubusercontent.com/HamletDuFromage/switch-cheats-db/master/cheats/" + titleHex + ".json";

        // Download JSON
        if (!ult::downloadFile(url, tempJsonPath, false, true)) {
            res.status = CheatDownloadStatus::DownloadFailed;
            res.message = "Could not download cheats from database.";
            return res;
        }

        // Parse JSON
        ult::json_t* rawJson = ult::readJsonFromFile(tempJsonPath);
        if (!rawJson) {
            ult::deleteFileOrDirectory(tempJsonPath);
            res.status = CheatDownloadStatus::ParseFailed;
            res.message = "Failed to parse cheat database JSON.";
            return res;
        }

        cJSON* root = reinterpret_cast<cJSON*>(rawJson);

        cJSON* buildObj = nullptr;
        cJSON* child = root->child;
        while (child != nullptr) {
            if (child->string) {
                std::string key = child->string;
                if (key != "attribution") {
                    std::string upperKey = key;
                    std::transform(upperKey.begin(), upperKey.end(), upperKey.begin(), ::toupper);
                    res.availableBuildIds.push_back(upperKey);

                    if (upperKey == buildHex) {
                        buildObj = child;
                    }
                }
            }
            child = child->next;
        }

        if (!buildObj) {
            cJSON_Delete(root);
            ult::deleteFileOrDirectory(tempJsonPath);
            res.status = CheatDownloadStatus::BuildIdNotFound;
            res.message = "Cheats for Build ID " + buildHex + " not found in DB.";
            return res;
        }

        std::string cheatContent = "";
        int count = 0;
        cJSON* cheatItem = buildObj->child;
        while (cheatItem != nullptr) {
            if (cJSON_IsString(cheatItem) && cheatItem->valuestring != nullptr) {
                cheatContent += cheatItem->valuestring;
                if (!cheatContent.empty() && cheatContent.back() != '\n')
                    cheatContent += "\n";
                count++;
            }
            cheatItem = cheatItem->next;
        }

        cJSON_Delete(root);
        ult::deleteFileOrDirectory(tempJsonPath);

        if (cheatContent.empty()) {
            res.status = CheatDownloadStatus::BuildIdNotFound;
            res.message = "No cheats inside Build ID object.";
            return res;
        }

        std::string targetTxtPath = cheatsDir + buildHex + ".txt";
        ult::createTextFile(targetTxtPath, cheatContent);

        // Reload Atmosphere cheat engine
        CheatManager::forceReloadFromDisk();

        res.status = CheatDownloadStatus::Success;
        res.cheatsCount = count;
        res.message = "Loaded " + std::to_string(count) + " cheats for version " + buildHex + "!";
        return res;
    }

    DownloadResult CheatDownloader::applyCheatsFromDbVersion(u64 titleId, const std::string& sourceBuildIdInDb, u64 targetBuildId) {
        DownloadResult res;
        if (!isInternetConnected()) {
            res.status = CheatDownloadStatus::NoInternet;
            res.message = "No internet connection!";
            return res;
        }

        std::string titleHex = toHex16(titleId);
        std::string targetBuildHex = toHex16(targetBuildId);
        std::string cheatsDir = getCheatDirectory(titleId);
        ult::createDirectory(cheatsDir);

        std::string tempJsonPath = cheatsDir + "db_temp.json";
        std::string url = "https://raw.githubusercontent.com/HamletDuFromage/switch-cheats-db/master/cheats/" + titleHex + ".json";

        if (!ult::downloadFile(url, tempJsonPath, false, true)) {
            res.status = CheatDownloadStatus::DownloadFailed;
            res.message = "Download failed.";
            return res;
        }

        ult::json_t* rawJson = ult::readJsonFromFile(tempJsonPath);
        if (!rawJson) {
            ult::deleteFileOrDirectory(tempJsonPath);
            res.status = CheatDownloadStatus::ParseFailed;
            res.message = "JSON parse failed.";
            return res;
        }

        cJSON* root = reinterpret_cast<cJSON*>(rawJson);
        cJSON* buildObj = nullptr;
        cJSON* child = root->child;
        while (child != nullptr) {
            if (child->string) {
                std::string key = child->string;
                std::string upperKey = key;
                std::transform(upperKey.begin(), upperKey.end(), upperKey.begin(), ::toupper);
                if (upperKey == sourceBuildIdInDb) {
                    buildObj = child;
                    break;
                }
            }
            child = child->next;
        }

        if (!buildObj) {
            cJSON_Delete(root);
            ult::deleteFileOrDirectory(tempJsonPath);
            res.status = CheatDownloadStatus::BuildIdNotFound;
            res.message = "Selected version not found in database.";
            return res;
        }

        std::string cheatContent = "";
        int count = 0;
        cJSON* cheatItem = buildObj->child;
        while (cheatItem != nullptr) {
            if (cJSON_IsString(cheatItem) && cheatItem->valuestring != nullptr) {
                cheatContent += cheatItem->valuestring;
                if (!cheatContent.empty() && cheatContent.back() != '\n')
                    cheatContent += "\n";
                count++;
            }
            cheatItem = cheatItem->next;
        }

        cJSON_Delete(root);
        ult::deleteFileOrDirectory(tempJsonPath);

        std::string targetTxtPath = cheatsDir + targetBuildHex + ".txt";
        ult::createTextFile(targetTxtPath, cheatContent);

        CheatManager::forceReloadFromDisk();

        res.status = CheatDownloadStatus::Success;
        res.cheatsCount = count;
        res.message = "Applied " + std::to_string(count) + " cheats from " + sourceBuildIdInDb + "!";
        return res;
    }

}

