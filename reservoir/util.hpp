/*******************************************************************************
 * reservoir/util.hpp
 *
 * Utilities
 *
 * Copyright (C) 2019 Lorenz Hübschle-Schneider <lorenz@4z2.de>
 *
 * All rights reserved. Published under the GNU General Public License 3
 ******************************************************************************/

#pragma once
#ifndef RESERVOIR_UTIL_HEADER
#define RESERVOIR_UTIL_HEADER

#include <tlx/die/core.hpp>

namespace reservoir {

#ifdef NDEBUG
#define my_assert(x)                                                           \
    do {                                                                       \
    } while (0)
#else
#define my_assert(x)                                                           \
    do {                                                                       \
        tlx_die_unless(x);                                                     \
    } while (0)
#endif


#ifndef RESERVOIR_ATTRIBUTE_NOINLINE
#if defined(_MSC_VER)
#define RESERVOIR_ATTRIBUTE_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) && __GNUC__ > 3 // clang/gcc
#define RESERVOIR_ATTRIBUTE_NOINLINE __attribute__((__noinline__))
#else
#define RESERVOIR_ATTRIBUTE_NOINLINE
#endif
#endif // infdef RESERVOIR_ATTRIBUTE_NOINLINE

} // namespace reservoir

#endif // RESERVOIR_UTIL_HEADER
