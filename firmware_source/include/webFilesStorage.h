#pragma once

#include <cstdint>
#include <string>
#include <vector>

class WebFilesStorage {
public:
    struct FileInfo {
        std::string name;
        uint64_t size = 0;
    };

    static constexpr const char* kRoot = "/sdcard";
    static constexpr const char* kUploadTemp = "/sdcard/.diptyx_web_upload.part";

    bool list(std::vector<FileInfo>& files) const;
    bool exists(const std::string& name) const;
    bool isValidBookName(const std::string& name) const;

    bool beginUpload(const std::string& name, uint64_t expectedSize);
    bool writeUpload(const void* data, size_t size);
    bool finishUpload(const std::string& name, bool replaceExisting);
    void abortUpload();

    bool deleteBook(const std::string& name) const;
    bool openBookForRead(const std::string& name, void*& fileHandle, uint64_t& size) const;
    size_t readBook(void* fileHandle, void* buffer, size_t size) const;
    void closeBook(void* fileHandle) const;

    uint64_t totalBytes() const;
    uint64_t freeBytes() const;

private:
    bool removeMetadata(const std::string& name) const;
    bool buildPath(const std::string& name, std::string& path) const;
    bool buildMetadataPath(const std::string& base, const std::string& name, std::string& path) const;

    void* uploadFile_ = nullptr;
};
