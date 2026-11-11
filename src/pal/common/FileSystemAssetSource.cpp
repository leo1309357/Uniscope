/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "FileSystemAssetSource.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <utility>

namespace uniscope::pal {

namespace {

class FileAssetStream final : public IAssetStream {
public:
    explicit FileAssetStream(std::ifstream stream, std::size_t size)
        : stream_(std::move(stream)), size_(size) {}

    bool read(void* dst, size_t bytes) override {
        if (dst == nullptr || bytes == 0) {
            return bytes == 0;
        }
        stream_.read(static_cast<char*>(dst), static_cast<std::streamsize>(bytes));
        return stream_.good() || static_cast<size_t>(stream_.gcount()) == bytes;
    }

    bool seek(size_t offset) override {
        stream_.clear();
        stream_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        return stream_.good();
    }

    size_t size() const override { return size_; }

private:
    std::ifstream stream_;
    std::size_t size_;
};

} // namespace

FileSystemAssetSource::FileSystemAssetSource(std::string rootPath)
    : rootPath_(std::move(rootPath)) {}

std::string FileSystemAssetSource::resolve(const std::string& logicalPath) const {
    if (rootPath_.empty()) {
        return logicalPath;
    }
    std::filesystem::path path = std::filesystem::u8path(rootPath_);
    path /= std::filesystem::u8path(logicalPath);
    return path.u8string();
}

bool FileSystemAssetSource::exists(const std::string& logicalPath) const {
    std::error_code error;
    return std::filesystem::exists(std::filesystem::u8path(resolve(logicalPath)), error)
        && !std::filesystem::is_directory(std::filesystem::u8path(resolve(logicalPath)), error);
}

bool FileSystemAssetSource::readText(const std::string& logicalPath, std::string* out) const {
    if (out == nullptr) {
        return false;
    }
    std::ifstream stream(
        std::filesystem::u8path(resolve(logicalPath)),
        std::ios::binary);
    if (!stream.is_open()) {
        return false;
    }
    out->assign(
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>());
    return true;
}

bool FileSystemAssetSource::readBytes(
    const std::string& logicalPath,
    std::vector<uint8_t>* out) const {
    if (out == nullptr) {
        return false;
    }
    const std::string path = resolve(logicalPath);
    std::ifstream stream(std::filesystem::u8path(path), std::ios::binary);
    if (!stream.is_open()) {
        return false;
    }
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(std::filesystem::u8path(path), error);
    out->clear();
    if (!error && size > 0) {
        out->resize(static_cast<std::size_t>(size));
        stream.read(reinterpret_cast<char*>(out->data()), static_cast<std::streamsize>(size));
        return static_cast<std::uintmax_t>(stream.gcount()) == size;
    }
    out->assign(
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>());
    return true;
}

std::unique_ptr<IAssetStream> FileSystemAssetSource::openStream(
    const std::string& logicalPath) const {
    const std::string path = resolve(logicalPath);
    std::ifstream stream(std::filesystem::u8path(path), std::ios::binary);
    if (!stream.is_open()) {
        return nullptr;
    }
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(std::filesystem::u8path(path), error);
    return std::make_unique<FileAssetStream>(
        std::move(stream),
        error ? 0u : static_cast<std::size_t>(size));
}

bool FileSystemAssetSource::listDirectory(
    const std::string& logicalPath,
    std::vector<std::string>* out) const {
    if (out == nullptr) {
        return false;
    }
    out->clear();
    const std::filesystem::path root = std::filesystem::u8path(resolve(logicalPath));
    std::error_code error;
    if (!std::filesystem::is_directory(root, error) || error) {
        return false;
    }
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(root, error)) {
        if (error) {
            break;
        }
        out->push_back(entry.path().filename().u8string());
    }
    return true;
}

bool FileSystemAssetSource::resolveToOsPath(
    const std::string& logicalPath,
    std::string* out) const {
    if (out == nullptr) {
        return false;
    }
    *out = resolve(logicalPath);
    return true;
}

} // namespace uniscope::pal
