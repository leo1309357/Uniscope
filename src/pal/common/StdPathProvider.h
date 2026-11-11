/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_PAL_STD_PATH_PROVIDER_H
#define UNISCOPE_PAL_STD_PATH_PROVIDER_H

#include "uniscope/pal/Paths.h"

#include <string>
#include <vector>

namespace uniscope::pal {

// True when a probe file can actually be created in the directory. The
// permission bits are not enough on Windows: the effective access depends on
// the ACL of every parent and on the token the process runs under, which is
// exactly the difference between a portable install and one under Program
// Files.
bool isWritableDirectory(const std::string& directory);

// Copies what an earlier build left in the per-user folder into `destination`
// the first time a portable root is used: existing files always win, `cache/`
// is skipped because it is rebuilt on demand, and the per-user copy is left in
// place as a backup. A marker file keeps later launches from walking the old
// tree again.
void adoptUserData(const std::string& destination, const std::vector<std::string>& sources);

// Default path provider. It keeps the deployment layout the game has always
// used: resources/ and data/ are discovered by walking up from the executable,
// and settings/saves live in the per-user directory.
class StdPathProvider final : public IPathProvider {
public:
    // sandboxRoot: Android internal files dir (empty elsewhere).
    StdPathProvider(AppIdentity identity, std::string sandboxRoot = {});

    AppIdentity identity() const override { return identity_; }
    AppPaths paths() const override;
    std::string userDataPath(const std::string& relative) const override;
    bool ensureDirectory(const std::string& absolutePath) const override;

private:
    void initialize() const;

    AppIdentity identity_;
    std::string sandboxRoot_;
    mutable AppPaths paths_;
    mutable bool initialized_ = false;
};

} // namespace uniscope::pal

#endif
