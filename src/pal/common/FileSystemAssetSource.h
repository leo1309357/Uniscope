/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_PAL_FILE_SYSTEM_ASSET_SOURCE_H
#define UNISCOPE_PAL_FILE_SYSTEM_ASSET_SOURCE_H

#include "uniscope/pal/Assets.h"

#include <string>

namespace uniscope::pal {

// Desktop asset source: logical paths resolve to files under a root directory.
class FileSystemAssetSource final : public IAssetSource {
public:
    explicit FileSystemAssetSource(std::string rootPath);

    bool exists(const std::string& logicalPath) const override;
    bool readText(const std::string& logicalPath, std::string* out) const override;
    bool readBytes(const std::string& logicalPath, std::vector<uint8_t>* out) const override;
    std::unique_ptr<IAssetStream> openStream(const std::string& logicalPath) const override;
    bool listDirectory(const std::string& logicalPath, std::vector<std::string>* out) const override;
    bool resolveToOsPath(const std::string& logicalPath, std::string* out) const override;
    const char* kindName() const override { return "filesystem"; }

private:
    std::string resolve(const std::string& logicalPath) const;

    std::string rootPath_;
};

} // namespace uniscope::pal

#endif
