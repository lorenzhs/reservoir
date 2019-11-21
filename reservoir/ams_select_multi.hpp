/*******************************************************************************
 * reservoir/ams_select_multi.hpp
 *
 * Approximate selection from sorted sequences (in this case, mostly B-trees)
 *
 * Copyright (C) 2019 Lorenz Hübschle-Schneider <lorenz@4z2.de>
 *
 * All rights reserved. Published under the GNU General Public License 3
 ******************************************************************************/

#pragma once
#ifndef RESERVOIR_AMS_SELECT_MULTI_HEADER
#define RESERVOIR_AMS_SELECT_MULTI_HEADER

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
#include <tuple>
#include <utility>
#include <vector>

namespace mpi = boost::mpi;

namespace reservoir {

template <typename Seq, int d = 16>
class ams_select_multi {
public:
    static constexpr const char short_name[] = "[amm]";

    // this is too bothersome to do as constexpr
    static const std::string name() {
        return "ams-multi-" + std::to_string(d);
    }

    using Iterator = typename Seq::const_iterator;
    using Cmp = typename Seq::key_compare;
    using Key = typename Seq::key_type;
    using Elem = typename Seq::value_type;

    // pair of iterator and local rank
    using Result = std::pair<Iterator, ssize_t>;

    // Upper/lower bound iterator and an index
    struct Bound {
        ssize_t ub_pos, lb_pos;
        Iterator ub_it, lb_it;

        Bound() = default;
        Bound(const std::tuple<ssize_t, ssize_t, Iterator, Iterator> &tup) {
            std::tie(ub_pos, lb_pos, ub_it, lb_it) = tup;
        }
    };

    static constexpr bool debug = false;
    static constexpr bool check = false;
    static constexpr bool time = true;

    // Construct a selector using a given communicator and seed.  The seed must
    // be different on every PE (member of the communicator)!
    ams_select_multi(mpi::communicator &comm, size_t seed)
        : comm_(comm), rng_(seed) {
        // divide stats counts by d
        stats_.norm_factor = d;
    }

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

        // allocate space for pivots
        pivots_.resize(d);
        bounds_.resize(d);
        gbounds_.resize(2 * d);

        auto res = select(seq, kmin, kmax, 0, seq.size(), size);

        if constexpr (check) {
            size_t result_size = res.second;
            spLOG << "result size contribution:" << result_size;
            mpi::all_reduce(comm_, mpi::inplace(result_size), std::plus<>());
            sLOGR << "Result size:" << result_size << "kmin:" << kmin
                  << "kmax:" << kmax;
            tlx_die_unless(kmin <= result_size && kmax >= result_size);
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

        Iterator min_it = seq.find_rank(min_idx), max_it = seq.find_rank(max_idx);

        if (kmin == 1 || kmax == 1) {
            Key pivot = std::numeric_limits<Key>::max();
            if (local_size > 0) {
                pivot = get_key(min_it);
            }
            pLOG << "Aborting at level " << stats_.level << " with kmin = " << kmin
                 << " kmax = " << kmax << " local minimum: " << pivot;
            mpi::all_reduce(comm_, mpi::inplace(pivot), mpi::minimum<Key>());

            auto [ub_pos, ub_it] = seq.rank_of_upper_bound(pivot);
            // slightly cheaty, normally we subtract min_idx from ub_pos
            if (static_cast<ssize_t>(ub_pos) < min_idx) {
                ub_pos = min_idx;
                ub_it = min_it;
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
            for (int i = 0; i < d; i++) {
                ssize_t pivot_idx = pidx_dist(rng_);
                tlx_die_unless(pivot_idx >= 0);

                if (pivot_idx < local_size) {
                    pivots_[i] = get_key(seq.find_rank(min_idx + pivot_idx));
                } else {
                    pivots_[i] = std::numeric_limits<Key>::max();
                    stats_.pidx_oob++;
                }
                spLOG << "chose pivot index" << pivot_idx << "value" << pivots_[i]
                      << "for local size" << local_size << "pivot" << i;
            }
            // use smallest of local pivots as global pivots
            mpi::all_reduce(comm_, mpi::inplace(pivots_.data()), d,
                            mpi::minimum<Key>());
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
            for (int i = 0; i < d; i++) {
                ssize_t pivot_idx = pidx_dist(rng_);
                tlx_die_unless(pivot_idx >= 0);

                if (pivot_idx < local_size) {
                    pivots_[i] = get_key(seq.find_rank(max_idx - pivot_idx - 1));
                } else {
                    pivots_[i] = std::numeric_limits<Key>::min();
                    stats_.pidx_oob++;
                }
                spLOG << "chose pivot index" << pivot_idx << "value" << pivots_[i]
                      << "for local size" << local_size << "pivot" << i;
            }

            // use largest of local pivots as global pivots
            mpi::all_reduce(comm_, mpi::inplace(pivots_.data()), d,
                            mpi::maximum<Key>());
        }
        LOGR << "pivot values = " << pivots_;

        for (int i = 0; i < d; i++) {
            bounds_[i] = _detail::get_bounds<false>(
                seq, stats_, pivots_[i], min_idx, max_idx, min_it, max_it,
                comm_, short_name, debug);
            gbounds_[ubidx(i)] = bounds_[i].ub_pos;
            gbounds_[lbidx(i)] = bounds_[i].lb_pos;
        }

        mpi::all_reduce(comm_, mpi::inplace(gbounds_.data()), 2 * d, std::plus<>());

        sLOGR << "global_size =" << global_size << "want" << kmin << "to"
              << kmax << "got bounds (ub,lb)" << gbounds_;

        // dummy
        int best_ub_idx = -1, best_lb_idx = -1;
        ssize_t best_ub_diff = std::numeric_limits<ssize_t>::max(),
                best_lb_diff = best_ub_diff;

        for (int i = 0; i < d; i++) {
            ssize_t global_ub = gbounds_[ubidx(i)], global_lb = gbounds_[lbidx(i)];
            if (global_ub >= kmin && global_lb <= kmax) {
                // we're good, just figure out the duplicates
                sLOGR << "Success! Bound" << i << "is perfect:" << global_lb
                      << "ub" << global_ub << "kmin" << kmin << "kmax" << kmax;
                if (global_lb < kmin) {
                    // Discard all items equal to the pivot
                    // XXX TODO sure we don't need find_eq_pos for this?
                    stats_.record(timer_.get());
                    return std::make_pair(bounds_[i].ub_it,
                                          min_idx + bounds_[i].ub_pos);
                }

                Result result = _detail::find_eq_pos(
                    global_ub, bounds_[i].ub_pos, bounds_[i].ub_it, global_lb,
                    bounds_[i].lb_pos, bounds_[i].lb_it, min_idx,
                    kmin - global_lb, comm_, debug, short_name);

                stats_.record(timer_.get());
                return result;
            }

            if (global_ub < kmin) {
                ssize_t diff = kmin - global_ub;
                if (diff < best_ub_diff) {
                    sLOGR << "Pair" << i << "improves UB:" << global_ub << "<"
                          << kmin << "diff" << diff << "<" << best_ub_diff;
                    best_ub_diff = diff;
                    best_ub_idx = i;
                }
            }

            if (global_lb > kmax) {
                ssize_t diff = global_lb - kmax;
                if (diff < best_lb_diff) {
                    sLOGR << "Pair" << i << "improves LB:" << global_lb << ">"
                          << kmax << "diff" << diff << "<" << best_lb_diff;
                    best_lb_diff = diff;
                    best_lb_idx = i;
                }
            }
        }

        sLOGR << "Narrowed it to within" << best_ub_diff << "of kmin with pivot"
              << best_ub_idx << "and" << best_lb_diff << "of kmax with"
              << best_lb_idx;

        ssize_t new_min_idx = min_idx, new_max_idx = max_idx, new_kmin = kmin,
                new_kmax = kmax, new_global_size = global_size;
        if (best_ub_idx >= 0) {
            new_min_idx += bounds_[best_ub_idx].ub_pos;
            ssize_t global_ub = gbounds_[ubidx(best_ub_idx)];
            new_kmin -= global_ub;
            new_kmax -= global_ub;
            new_global_size -= global_ub;
        }
        if (best_lb_idx >= 0) {
            new_max_idx = min_idx + bounds_[best_lb_idx].lb_pos;
            ssize_t global_lb = gbounds_[lbidx(best_lb_idx)];
            new_global_size -= (global_size - global_lb);
        }
        tlx_die_unless(new_global_size > 0);
        tlx_die_unless(new_global_size <= global_size);

        sLOGR << "New kmin:" << new_kmin << "kmax:" << new_kmax
              << "size:" << new_global_size;

        // Record magnitude of size change in statistics
        if (new_global_size == global_size)
            stats_.size_unchanged++;
        else if ((global_size - new_global_size) * 50 <= global_size ||
                 (global_size - new_global_size) <= 5)
            stats_.tinychange++;

        stats_.record(timer_.get());
        return select(seq, new_kmin, new_kmax, new_min_idx, new_max_idx,
                      new_global_size);
    }

    constexpr Key get_key(const Iterator &it) {
        if constexpr (std::is_same_v<Key, Elem>) {
            return *it;
        } else {
            return it->first;
        }
    }

    constexpr ssize_t ubidx(int i) {
        return 2 * i;
    }
    constexpr ssize_t lbidx(int i) {
        return 2 * i + 1;
    }

    mpi::communicator &comm_;
    std::mt19937_64 rng_;
    std::vector<Key> pivots_;
    std::vector<Bound> bounds_;
    std::vector<ssize_t> gbounds_;
    mutable _detail::select_stats<time> stats_;
    mutable timer timer_;
};

} // namespace reservoir

#endif // RESERVOIR_AMS_SELECT_MULTI_HEADER
