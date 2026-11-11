/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_ASSET_PATHS_H
#define UNISCOPE_D2_ASSET_PATHS_H

#include <string>

namespace uniscope::d2 {

class AssetPaths {
public:
    static void setRoot(std::string root);
    static const std::string& root();

    static std::string resolve(const std::string& relativePath);
    static bool exists(const std::string& relativePath);
};

} // namespace uniscope::d2

#endif
