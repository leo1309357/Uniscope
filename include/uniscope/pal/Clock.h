/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_PAL_CLOCK_H
#define UNISCOPE_PAL_CLOCK_H

namespace uniscope::pal {

class IClock {
public:
    virtual ~IClock() = default;
    virtual double nowSeconds() const = 0; // 单调递增，不受暂停影响
};

class StdClock final : public IClock {
public:
    double nowSeconds() const override;
};

} // namespace uniscope::pal

#endif
