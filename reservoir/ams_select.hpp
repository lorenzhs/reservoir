/*******************************************************************************
 * reservoir/ams_select.hpp
 *
 * Approximate selection from sorted sequences (in this case, mostly B-trees)
 *
 * Copyright (C) 2019 Lorenz Hübschle-Schneider <lorenz@4z2.de>
 *
 * All rights reserved. Published under the GNU General Public License 3
 ******************************************************************************/

#pragma once
#ifndef RESERVOIR_AMS_SELECT_HEADER
#define RESERVOIR_AMS_SELECT_HEADER

#include <reservoir/aggregate.hpp>
#include <reservoir/logger.hpp>
#include <reservoir/select_helpers.hpp>
#include <reservoir/timer.hpp>
#include <reservoir/util.hpp>

#include <tlx/die/core.hpp>
#include <tlx/math/aggregate.hpp>

#include <boost/mpi.hpp>

#include <algorithm>
#include <functional>
#include <limits>
#include <ostream>
#include <random>
#include <utility>
#include <vector>

namespace mpi = boost::mpi;

namespace reservoir {

template <typename Seq>
class ams_select {
public:
    static constexpr const char *name = "ams-select";
    static constexpr const char *short_name = "[ams]";

    using Iterator = typename Seq::const_iterator;
    using Cmp = typename Seq::key_compare;
    using Key = typename Seq::key_type;
    using Elem = typename Seq::value_type;

    // pair of iterator and local rank
    using Result = std::pair<Iterator, ssize_t>;

    static constexpr bool debug = false;
    static constexpr bool check = false;
    static constexpr bool time = true;

    // Construct a selector using a given communicator and seed.  The seed needs
    // to be identical on every PE (member of the communicator)!
    ams_select(mpi::communicator &comm, size_t seed)
        : comm_(comm), rng_(seed) {}

    // wrapper for kmin = kmax
    Result operator()(Seq &seq, size_t k) {
        return operator()(seq, k, k);
    }

    Result operator()(const Seq &seq, const size_t kmin, const size_t kmax) {
        timer total_timer;

        sLOGR << "Selecting between" << kmin << "and" << kmax << "with"
              << comm_.size() << "PEs";
        if (kmin > kmax || kmax == 0) {
            sLOGR << "aborting: kmin =" << kmin << "kmax =" << kmax;
            return std::make_pair(seq.begin(), 0);
        }
        if constexpr (check) {
            seq.verify();
            size_t size = seq.size();
            mpi::all_reduce(comm_, mpi::inplace(size), std::plus<>());
            sLOGR << "Checking size: want at least" << kmin << "have" << size;
            tlx_die_unless(kmin <= size);
        }

        // calculate global size
        size_t size = seq.size();
        mpi::all_reduce(comm_, mpi::inplace(size), std::plus<>());
        LOGR << "global size: " << size;
        // clang-format off
        tlx_die_verbose_unless(kmin <= size,
                               "Cannot select " << kmin << " to " << kmax
                               << " smallest out of " << size << " items; have "
                               << seq.size() << " at PE " << comm_.rank());
        // clang-format on

        auto res = select(seq, kmin, kmax, 0, seq.size(), size);

        if constexpr (check) {
            size_t result_size = res.second;
            mpi::all_reduce(comm_, mpi::inplace(result_size), std::plus<>());
            tlx_die_verbose_unless(kmin <= result_size && kmax >= result_size,
                                   "Expected between " << kmin << " and " << kmax
                                                       << " got " << result_size);
        }

        if constexpr (debug) {
            comm_.barrier();
            auto it = seq.begin();
            std::vector<Elem> vec;
            while (it != res.first) {
                vec.emplace_back(*it++);
            }
            size_t size = vec.size();
            pLOG << "local result " << size << " elements";
            // double-check output size
            mpi::all_reduce(comm_, mpi::inplace(size), std::plus<>());
            tlx_die_unless(size >= kmin && size <= kmax);
        }

        stats_.record_total(total_timer.get());
        stats_.reset_level();
        pLOGC(time && debug) << "stats: " << stats_;
        return res;
    }

    _detail::select_stats<time> &get_stats() {
        return stats_;
    }

protected:
    Result select(const Seq &seq, const ssize_t kmin, const ssize_t kmax,
                  const ssize_t min_idx, const ssize_t max_idx,
                  const ssize_t global_size) {
        stats_.next_level(); // debug timings
        if (comm_.rank() == 0)
            stats_.record_size(global_size);
        timer_.reset();

        tlx_die_verbose_unless(max_idx >= min_idx,
                               "Expected max_idx >= min_idx, got max_idx = "
                                   << max_idx << " min_idx = " << min_idx);
        tlx_die_unless(kmin <= kmax && kmin <= global_size);

        const ssize_t local_size = max_idx - min_idx;
        spLOG << "kmin =" << kmin << "kmax =" << kmax
              << "global_size =" << global_size << "local range:" << min_idx
              << max_idx << "size" << local_size;

        Key pivot;

        if (kmin == 1 || kmax == 1) {
            pivot = std::numeric_limits<Key>::max();
            if (local_size > 0) {
                pivot = get_key(seq.find_rank(min_idx));
            }
            pLOG << "Aborting at level " << stats_.level << " with kmin = " << kmin
                 << " kmax = " << kmax << " local minimum: " << pivot;
            mpi::all_reduce(comm_, mpi::inplace(pivot), mpi::minimum<Key>());

            auto [ub_pos, ub_it] = seq.rank_of_upper_bound(pivot);
            // slightly cheaty, normally we subtract min_idx from ub_pos
            if (static_cast<ssize_t>(ub_pos) < min_idx) {
                ub_pos = min_idx;
                ub_it = seq.find_rank(min_idx);
            }
            pLOG << "pivot = " << pivot << " pos " << ub_pos;

            stats_.record(timer_.get());
            return std::make_pair(ub_it, ub_pos);
        }

        if (kmin < global_size - kmax) {
            stats_.kcase.add(0);
            double p =
                1.0 - std::pow((kmin - 1.0) / kmax, 1.0 / (kmax - kmin + 1));
            sLOGR << "Case 1, p =" << p << "base" << (kmin - 1.0) / kmax
                  << "exponent" << 1.0 / (kmax - kmin + 1);
            tlx_die_unless(0 <= p && p <= 1);

            std::geometric_distribution<ssize_t> pidx_dist(p);
            ssize_t pivot_idx = pidx_dist(rng_);
            tlx_die_unless(pivot_idx >= 0);

            if (pivot_idx < local_size) {
                pivot = get_key(seq.find_rank(min_idx + pivot_idx));
            } else {
                pivot = std::numeric_limits<Key>::max();
                stats_.pidx_oob++;
            }
            spLOG << "chose pivot index" << pivot_idx << "value" << pivot
                  << "for local size" << local_size;
            // use smallest local pivot as global pivot
            mpi::all_reduce(comm_, mpi::inplace(pivot), mpi::minimum<Key>());
        } else {
            stats_.kcase.add(1);
            double p =
                1.0 - std::pow((global_size - kmax) / (global_size - kmin + 1.0),
                               1.0 / (kmax - kmin + 1));
            sLOGR << "Case 2, p =" << p << "base"
                  << (global_size - kmax) / (global_size - kmin + 1.0)
                  << "exponent" << 1.0 / (kmax - kmin + 1);
            tlx_die_unless(0 <= p && p <= 1);

            std::geometric_distribution<ssize_t> pidx_dist(p);
            ssize_t pivot_idx = pidx_dist(rng_);
            tlx_die_unless(pivot_idx >= 0);

            if (pivot_idx < local_size) {
                pivot = get_key(seq.find_rank(max_idx - pivot_idx - 1));
            } else {
                pLOG << "pivot idx " << pivot_idx << " OOB, >= " << local_size;
                pivot = std::numeric_limits<Key>::min();
                stats_.pidx_oob++;
            }
            spLOG << "chose pivot index" << pivot_idx << "value" << pivot
                  << "for local size" << local_size;
            // use largest local pivot as global pivot
            mpi::all_reduce(comm_, mpi::inplace(pivot), mpi::maximum<Key>());
        }
        LOGR << "pivot value = " << pivot;

        auto [ub_pos, lb_pos, ub_it, lb_it] = _detail::get_bounds(
            seq, stats_, pivot, min_idx, max_idx, comm_, short_name, debug);
        auto [global_ub, global_lb] =
            _detail::global_bound(ub_pos, lb_pos, global_size, comm_);

        sLOGRC(debug || global_ub > global_lb + 1)
            << "have" << global_lb << "smaller than," << global_ub
            << "leq to pivot of" << global_size << "want" << kmin << "to" << kmax;

        stats_.record(timer_.get());

        if (global_ub < kmin) {
            // recurse on elements larger than pivot
            stats_.right();
            sLOGR << "recursion: right; global_ub =" << global_ub
                  << "lb =" << global_lb << "; ub <" << kmin
                  << "= kmin for pivot =" << pivot;
            if (global_ub == 0)
                stats_.size_unchanged++;
            else if (global_ub * 50 <= global_size || global_ub <= 5)
                stats_.tinychange++;
            return select(seq, kmin - global_ub, kmax - global_ub,
                          min_idx + ub_pos, max_idx, global_size - global_ub);
        } else if (global_lb > kmax) {
            // recurse on elements smaller than pivot
            stats_.left();
            sLOGR << "recursion: left; global_ub =" << global_ub
                  << "lb = " << global_lb << "; lb > " << kmax
                  << "= kmax for pivot =" << pivot;
            if (global_lb == global_size)
                stats_.size_unchanged++;
            else if ((global_size - global_lb) * 50 <= global_size ||
                     (global_size - global_lb) <= 5)
                stats_.tinychange++;
            return select(seq, kmin, kmax, min_idx, min_idx + lb_pos, global_lb);
        } else {
            // Nearly done; result key is equal to pivot, but exact split is tbd
            return _detail::find_eq_pos(global_ub, ub_pos, ub_it, global_lb,
                                        lb_pos, lb_it, min_idx, kmin - global_lb,
                                        comm_, debug, short_name);
        }
    }

    constexpr Key get_key(const Iterator &it) {
        if constexpr (std::is_same_v<Key, Elem>) {
            return *it;
        } else {
            return it->first;
        }
    }

    mpi::communicator &comm_;
    std::mt19937_64 rng_;
    mutable _detail::select_stats<time> stats_;
    mutable timer timer_;
};

} // namespace reservoir

#endif // RESERVOIR_AMS_SELECT_HEADER
