#include "webFilesStorage.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "esp_log.h"

static const char* TAG = "WEB_FILES_STORAGE";

namespace {
constexpr size_t kMaxWebFilename = 120;
constexpr uint64_t kMinFreeBytesSafety = 4096;

bool hasEpubExtension(const std::string& name) {
    if (name.empty() || name[0] == '.') return false;
    if (name.size() < 5) return false;
    const std::string ext = name.substr(name.size() - 5);
    return strcasecmp(ext.c_str(), ".epub") == 0;
}
}

bool WebFilesStorage::isValidBookName(const std::string& name) const {
    if (name.empty() || name.size() > kMaxWebFilename) return false;
    if (!hasEpubExtension(name)) return false;
    if (name == "." || name == "..") return false;

    for (unsigned char c : name) {
        if (c < 0x20 || c == 0x7f) return false;
        if (c == '/' || c == '\\') return false;
        if (c == '"') return false;
    }

    return true;
}

bool WebFilesStorage::buildPath(const std::string& name, std::string& path) const {
    if (!isValidBookName(name)) return false;
    path = std::string(kRoot) + "/" + name;
    return true;
}

bool WebFilesStorage::buildMetadataPath(const std::string& base,
                                         const std::string& name,
                                         std::string& path) const {
    if (!isValidBookName(name)) return false;
    path = base + "/" + name + ".json";
    return true;
}

bool WebFilesStorage::list(std::vector<FileInfo>& files) const {
    files.clear();

    DIR* dir = opendir(kRoot);
    if (!dir) {
        ESP_LOGE(TAG, "Unable to open %s: %s", kRoot, strerror(errno));
        return false;
    }

    struct dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        const std::string name(entry->d_name);
        if (!isValidBookName(name)) continue;

        std::string path;
        if (!buildPath(name, path)) continue;

        struct stat st{};
        if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;

        files.push_back({name, static_cast<uint64_t>(st.st_size)});
    }

    closedir(dir);

    std::sort(files.begin(), files.end(), [](const FileInfo& a, const FileInfo& b) {
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });

    return true;
}

bool WebFilesStorage::exists(const std::string& name) const {
    std::string path;
    if (!buildPath(name, path)) return false;

    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool WebFilesStorage::beginUpload(const std::string& name, uint64_t expectedSize) {
    if (!isValidBookName(name)) return false;
    if (uploadFile_ != nullptr) return false;

    const uint64_t free = freeBytes();
    if (expectedSize > 0 && (free < expectedSize || free - expectedSize < kMinFreeBytesSafety)) {
        ESP_LOGW(TAG, "Rejecting upload: requested=%llu free=%llu",
                 static_cast<unsigned long long>(expectedSize),
                 static_cast<unsigned long long>(free));
        return false;
    }

    // A stale partial upload is never useful and may have survived a reset.
    unlink(kUploadTemp);

    FILE* file = fopen(kUploadTemp, "wb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to create upload temp file: %s", strerror(errno));
        return false;
    }

    uploadFile_ = file;
    return true;
}

bool WebFilesStorage::writeUpload(const void* data, size_t size) {
    if (uploadFile_ == nullptr || data == nullptr || size == 0) return false;

    FILE* file = static_cast<FILE*>(uploadFile_);
    return fwrite(data, 1, size, file) == size;
}

bool WebFilesStorage::finishUpload(const std::string& name, bool replaceExisting) {
    if (uploadFile_ == nullptr || !isValidBookName(name)) return false;

    FILE* file = static_cast<FILE*>(uploadFile_);
    if (fflush(file) != 0) {
        ESP_LOGE(TAG, "fflush() failed for upload");
        fclose(file);
        uploadFile_ = nullptr;
        unlink(kUploadTemp);
        return false;
    }

    const int fd = fileno(file);
    if (fd >= 0) fsync(fd);
    if (fclose(file) != 0) {
        uploadFile_ = nullptr;
        unlink(kUploadTemp);
        return false;
    }
    uploadFile_ = nullptr;

    std::string destination;
    if (!buildPath(name, destination)) {
        unlink(kUploadTemp);
        return false;
    }

    const bool alreadyExists = exists(name);
    if (alreadyExists && !replaceExisting) {
        unlink(kUploadTemp);
        return false;
    }

    std::string backup;
    bool backupCreated = false;
    if (alreadyExists) {
        backup = destination + ".diptyx_old";
        unlink(backup.c_str());
        if (rename(destination.c_str(), backup.c_str()) != 0) {
            ESP_LOGE(TAG, "Unable to stage existing book: %s", strerror(errno));
            unlink(kUploadTemp);
            return false;
        }
        backupCreated = true;
    }

    if (rename(kUploadTemp, destination.c_str()) != 0) {
        ESP_LOGE(TAG, "Unable to commit upload: %s", strerror(errno));
        if (backupCreated) rename(backup.c_str(), destination.c_str());
        unlink(kUploadTemp);
        return false;
    }

    if (backupCreated) unlink(backup.c_str());

    // A replaced ebook must not keep metadata for the old binary.
    if (alreadyExists) removeMetadata(name);
    return true;
}

void WebFilesStorage::abortUpload() {
    if (uploadFile_ != nullptr) {
        fclose(static_cast<FILE*>(uploadFile_));
        uploadFile_ = nullptr;
    }
    unlink(kUploadTemp);
}

bool WebFilesStorage::removeMetadata(const std::string& name) const {
    bool removedAny = false;
    std::string path;

    if (buildMetadataPath("/sdcard/book_data", name, path)) {
        if (unlink(path.c_str()) == 0) removedAny = true;
    }
    if (buildMetadataPath("/littlefs/books", name, path)) {
        if (unlink(path.c_str()) == 0) removedAny = true;
    }
    return removedAny;
}

bool WebFilesStorage::deleteBook(const std::string& name) const {
    std::string path;
    if (!buildPath(name, path)) return false;

    struct stat st{};
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;

    if (unlink(path.c_str()) != 0) {
        ESP_LOGE(TAG, "Failed to delete %s: %s", path.c_str(), strerror(errno));
        return false;
    }

    removeMetadata(name);
    return true;
}

bool WebFilesStorage::openBookForRead(const std::string& name,
                                       void*& fileHandle,
                                       uint64_t& size) const {
    fileHandle = nullptr;
    size = 0;

    std::string path;
    if (!buildPath(name, path)) return false;

    FILE* file = fopen(path.c_str(), "rb");
    if (!file) return false;

    if (fseeko(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    const off_t end = ftello(file);
    if (end < 0 || fseeko(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }

    fileHandle = file;
    size = static_cast<uint64_t>(end);
    return true;
}

size_t WebFilesStorage::readBook(void* fileHandle, void* buffer, size_t size) const {
    if (fileHandle == nullptr || buffer == nullptr || size == 0) return 0;
    return fread(buffer, 1, size, static_cast<FILE*>(fileHandle));
}

void WebFilesStorage::closeBook(void* fileHandle) const {
    if (fileHandle != nullptr) fclose(static_cast<FILE*>(fileHandle));
}

uint64_t WebFilesStorage::totalBytes() const {
    struct statvfs vfs{};
    if (statvfs(kRoot, &vfs) != 0) return 0;
    return static_cast<uint64_t>(vfs.f_blocks) * vfs.f_frsize;
}

uint64_t WebFilesStorage::freeBytes() const {
    struct statvfs vfs{};
    if (statvfs(kRoot, &vfs) != 0) return 0;
    return static_cast<uint64_t>(vfs.f_bavail) * vfs.f_frsize;
}
