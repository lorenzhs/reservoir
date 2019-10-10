/*******************************************************************************
 * reservoir/timer.hpp
 *
 * A simple timer
 *
 * Copyright (C) 2019 Lorenz Hübschle-Schneider <lorenz@4z2.de>
 *
 * All rights reserved. Published under the GNU General Public License 3
 ******************************************************************************/

#pragma once
#ifndef RESERVOIR_TIMER_HEADER
#define RESERVOIR_TIMER_HEADER

#include <chrono>

namespace reservoir {

/**
 * A flexible timer.
 *
 * resolution is the precision of the timing, while scaling_factor
 * is the factor by which the output will be scaled. The default is to
 * return milliseconds with microsecond precision.
 */
template <typename resolution = std::chrono::microseconds,
          int scaling_factor = 1000, typename return_type = double>
struct base_timer {
    base_timer() {
        reset();
    }

    void reset() {
        start = std::chrono::system_clock::now();
    }

    return_type get() const {
        auto duration = std::chrono::duration_cast<resolution>(
            std::chrono::system_clock::now() - start);
        return static_cast<return_type>(duration.count()) / scaling_factor;
    }

    return_type get_and_reset() {
        auto t = get();
        reset();
        return t;
    }

private:
    std::chrono::system_clock::time_point start;
};

/// A timer that is accurate to microseconds, formatted as milliseconds
using timer = base_timer<std::chrono::microseconds, 1000, double>;
/// A timer that is accurate to milliseconds, formatted as seconds
using sec_timer = base_timer<std::chrono::milliseconds, 1000, double>;

} // namespace reservoir

#endif // RESERVOIR_TIMER_HEADER
