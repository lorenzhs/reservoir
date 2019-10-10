/*******************************************************************************
 * reservoir/aggregate.hpp
 *
 * Stuff around tlx::Aggregate
 *
 * Copyright (C) 2019 Lorenz Hübschle-Schneider <lorenz@4z2.de>
 *
 * All rights reserved. Published under the GNU General Public License 3
 ******************************************************************************/

#pragma once
#ifndef RESERVOIR_AGGREGATE_HEADER
#define RESERVOIR_AGGREGATE_HEADER

#include <tlx/math/aggregate.hpp>

#include <boost/serialization/split_free.hpp>

#include <ostream>

namespace tlx {

template <typename T>
std::ostream &operator<<(std::ostream &os, const Aggregate<T> &x) {
    if (x.count() > 1) {
        os << "avg=" << x.avg() << " stdev=" << x.stdev()
           << " count=" << x.count() << " range=[" << x.min() << ".." << x.max()
           << "]";
    } else {
        os << x.avg();
    }
    return os;
}

} // namespace tlx

namespace boost::serialization {

template <typename Archive, typename T>
void save(Archive &ar, const tlx::Aggregate<T> &x, unsigned int) {
    auto f = [&](size_t count, double mean, double nvar, const T &min,
                 const T &max) {
        ar << count;
        ar << mean;
        ar << nvar;
        ar << min;
        ar << max;
    };
    const_cast<tlx::Aggregate<T> &>(x).serialize(f);
}

template <typename Archive, typename T>
void load(Archive &ar, tlx::Aggregate<T> &x, unsigned int) {
    size_t count;
    double mean, nvar;
    T min, max;
    ar >> count;
    ar >> mean;
    ar >> nvar;
    ar >> min;
    ar >> max;
    x = tlx::Aggregate<T>(count, mean, nvar, min, max);
}

template <typename Archive, typename T>
inline void serialize(Archive &ar, tlx::Aggregate<T> &x,
                      const unsigned int file_version) {
    split_free(ar, x, file_version);
}

} // namespace boost::serialization

#endif // RESERVOIR_AGGREGATE_HEADER
