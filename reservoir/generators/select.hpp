/*******************************************************************************
 * reservoir/rng/stl.hpp
 *
 * Copyright (C) 2018-2019 Lorenz Hübschle-Schneider <lorenz@4z2.de>
 *
 * All rights reserved. Published under the BSD-2 license in the LICENSE file.
 ******************************************************************************/

#pragma once
#ifndef RESERVOIR_GENERATORS_SELECT_HEADER
#define RESERVOIR_GENERATORS_SELECT_HEADER

#include "dSFMT.hpp"
#include "stl.hpp"
#ifdef RESERVOIR_HAVE_MKL
#include "mkl.hpp"
#endif

namespace reservoir::generators {

struct select {
    // dSFMT is at least twice as fast as std::mt19937_64 for large blocks with
    // gcc, and more using clang. It's practically never slower, so prefer it.
    // For Gaussian deviates, MKL is 5x faster, so prefer it if we have
    // it. There's no real differences between MKL and dSFMT for uniform
    // deviates.
    #ifdef RESERVOIR_HAVE_MKL
    using type = mkl;
    #else
    using type = dSFMT;
    #endif
};

using select_t = select::type;

} // namespace reservoir::generators

#endif // RESERVOIR_GENERATORS_SELECT_HEADER
