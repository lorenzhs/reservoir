/*******************************************************************************
 * reservoir/select_helpers.hpp
 *
 * Selection helpers
 *
 * Copyright (C) 2019 Lorenz Hübschle-Schneider <lorenz@4z2.de>
 *
 * All rights reserved. Published under the GNU General Public License 3
 ******************************************************************************/

#pragma once
#ifndef RESERVOIR_SELECT_HELPERS_HEADER
#define RESERVOIR_SELECT_HELPERS_HEADER

#include <reservoir/aggregate.hpp>
#include <reservoir/logger.hpp>

#include <tlx/die/core.hpp>
#include <tlx/logger.hpp>

#include <boost/mpi.hpp>
#include <boost/serialization/vector.hpp>

#include <algorithm>
#include <functional>
#include <iomanip>
#include <limits>
#include <numeric>
#include <ostream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mpi = boost::mpi;

namespace reservoir::_detail {

struct fake_aggregate {
    double mean() const {
        return 0.0;
    }
    double stdev() const {
        return 0.0;
    }
};

template <bool enabled>
struct select_stats;

template <>
struct select_stats<false> {
    void reset_level() {}
    void next_level() {}
    void record(double) {}
    void record_total(double) {}
    void left() {}
    void right() {}
    void steal_metadata(const select_stats & /* other */) {}

    fake_aggregate depth;

    friend std::ostream &operator<<(std::ostream &os, const select_stats &) {
        return os << "\t<no ms_select stats>";
    }

    template <typename Archive>
    void serialize(Archive & /* ar */, const unsigned int /* version */) {}

    select_stats &operator+=(const select_stats & /* other*/) {
        return *this;
    }
};


template <>
struct select_stats<true> {
    void reset_level() {
        // level is 0-indexed, so correct it by adding 1
        depth.add(level + 1);
        level = -1;
    }

    void next_level() {
        level++;
        max = std::max(level, max);
    }

    void record(double time) {
        timers[level].add(time);
    }

    void record_total(double time) {
        total.add(time);
    }

    void record_size(size_t size) {
        size_t index = level_idx(level);
        if (index >= sizes.size()) {
            sizes.resize(index + 1);
        }
        sizes[index].add(static_cast<double>(size));
    }

    void left() {
        recleft.add(0);
    }

    void right() {
        recleft.add(1);
    }

    friend std::ostream &operator<<(std::ostream &os, const select_stats &s) {
        os << "\ttotal:   " << s.total;
        for (int i = 0; i <= s.max; i++) {
            os << "\n\tlevel " << i << ": " << s.timers.at(i);
        }
        if (s.recleft.count() > 0)
            os << "\n\trecursion % left: " << s.recleft;
        os << "\n\trecursion depth:  " << s.depth;
        os << "\n\tk small/large:    " << s.kcase;

        double norm = static_cast<double>(s.kcase.count()) / 100.0 * s.norm_factor;
        os << "\n\tpivot_idx oob: " << s.pidx_oob << " = " << s.pidx_oob / norm
           << "%, no pivot: " << s.no_pivot << " = " << s.no_pivot / norm << "%";
        os << "\n\tneg split pos: " << s.neg_split_pos << " = "
           << s.neg_split_pos / norm << "%, split pos oob: " << s.split_pos_oob
           << " = " << s.split_pos_oob / norm << "%";

        size_t total_calls = 0;
        for (const auto &x : s.sizes)
            total_calls += x.count();
        double norm1 = static_cast<double>(total_calls) / 100.0;
        os << "\n\tGlobal size unchanged: " << s.size_unchanged << " = "
           << s.size_unchanged / norm1 << "%; <2% change: " << s.tinychange
           << " = " << s.tinychange / norm1 << "%";

        os << "\n\tGlobal size by recursion level:";
        int maxlvl = s.idx_to_level(s.sizes.size() - 1).first;
        int width = static_cast<int>(std::log10(static_cast<double>(maxlvl))) + 1;
        for (size_t i = 0; i < s.sizes.size(); i++) {
            auto [minlvl, maxlvl] = s.idx_to_level(i);
            os << "\n\t\tlvl " << std::setw(width) << minlvl << "-"
               << std::setw(width) << maxlvl << ": " << s.sizes[i];
        }

        return os;
    }

    select_stats &operator+=(const select_stats &other) {
        total += other.total;
        recleft += other.recleft;
        depth += other.depth;
        kcase += other.kcase;
        for (const auto &[lvl, stats] : other.timers) {
            timers[lvl] += stats;
        }
        if (other.sizes.size() > sizes.size()) {
            sizes.resize(other.sizes.size());
        }
        for (size_t i = 0; i < other.sizes.size(); i++) {
            sizes[i] += other.sizes[i];
        }
        max = std::max(max, other.max);
        pidx_oob += other.pidx_oob;
        no_pivot += other.no_pivot;
        neg_split_pos += other.neg_split_pos;
        split_pos_oob += other.split_pos_oob;
        size_unchanged += other.size_unchanged;
        tinychange += other.tinychange;
        norm_factor = std::max(norm_factor, other.norm_factor);
        // don't change level
        return *this;
    }

    template <typename Archive>
    void serialize(Archive &ar, const unsigned int /* version */) {
        ar &total;
        ar &recleft;
        ar &depth;
        ar &kcase;
        ar &pidx_oob;
        ar &no_pivot;
        ar &neg_split_pos;
        ar &split_pos_oob;
        // don't send the details
        // ar &timers;
        // ar &max;
        // ar &level;
    }

    // ahem, hack to prevent output of PE 0 timers
    void steal_metadata(const select_stats &) {
        max = -1;
        level = -1;
    }

    constexpr size_t level_idx(int level) const {
        if (level < 10)
            return 0;
        else if (level < 30)
            return 1;
        else if (level < 50)
            return 2;
        else if (level < 75)
            return 3;
        else if (level < 500)
            return 4 + level / 100;
        else
            return 9 + level / 1000;
    }

    constexpr std::pair<int, int> idx_to_level(size_t index) const {
        switch (index) {
            case 0: return {0, 9};
            case 1: return {10, 29};
            case 2: return {30, 49};
            case 3: return {50, 74};
            case 4: return {75, 99};
            case 9: return {500, 999};
            default: break;
        }
        if (index < 9) {
            index -= 4;
            return {index * 100, (index + 1) * 100 - 1};
        } else {
            index -= 9;
            return {index * 1000, (index + 1) * 1000 - 1};
        }
    }

    tlx::Aggregate<double> total;
    tlx::Aggregate<double> recleft;
    tlx::Aggregate<double> depth, kcase;
    std::unordered_map<int, tlx::Aggregate<double>> timers;
    std::vector<tlx::Aggregate<double>> sizes;
    size_t pidx_oob = 0, no_pivot = 0, neg_split_pos = 0, split_pos_oob = 0,
           size_unchanged = 0, tinychange = 0;
    int max = -1;
    int level = -1;
    int norm_factor = 1;
};


template <typename Seq, typename Stats>
void dump_state(const Seq &seq, const Stats &stats, ssize_t min_idx,
                ssize_t max_idx, ssize_t local_size, ssize_t global_size,
                ssize_t split_pos, ssize_t kmin, ssize_t kmax,
                ssize_t global_rank, typename Seq::key_type pivot,
                const std::string &short_name, mpi::communicator &comm_) {
    std::stringstream elems;
    elems << "[";
    auto begin = seq.find_rank(min_idx), end = seq.find_rank(max_idx);
    while (begin != end) {
        elems << begin->first << ", ";
        ++begin;
    }
    elems << "]";
    spLOG1 << "level" << stats.level << "with global size" << global_size
           << "local size" << local_size << "=" << min_idx << "to" << max_idx
           << "k:" << kmin << ".." << kmax << "pivot:" << pivot
           << "global rank:" << global_rank << "split_pos:" << split_pos;
    spLOG1 << "level" << stats.level << "local keys:" << elems.str();
}


template <bool do_global_ops, typename Seq, typename Stats,
          typename Key = typename Seq::key_type, typename Iterator = typename Seq::const_iterator>
std::tuple<ssize_t, ssize_t, Iterator, Iterator>
get_bounds(const Seq &seq, Stats &stats_, Key pivot, ssize_t min_idx,
           ssize_t max_idx, Iterator min_it, Iterator max_it,
           mpi::communicator &comm_, const std::string &short_name,
           const bool debug) {
    const ssize_t local_size = max_idx - min_idx;
    ssize_t lb_pos, ub_pos;
    Iterator lb_it, ub_it;

    if (pivot == std::numeric_limits<Key>::min()) {
        stats_.no_pivot++;
        pLOG << "No PE found a viable pivot, using max_idx / " << max_idx - min_idx
             << " max_idx = " << max_idx << " min_idx = " << min_idx;
        if (local_size == 0) {
            pLOG << "have no elements :(";
            ub_pos = lb_pos = 0;
            ub_it = lb_it = min_it;
        } else {
            ub_pos = lb_pos = local_size;
            ub_it = lb_it = max_it;
            if (do_global_ops) {
                pivot = ub_it->first;
                pLOG << "new pivot: " << pivot;
            }
        }
        // select max of all pivots
        if constexpr (do_global_ops) {
            mpi::all_reduce(comm_, mpi::inplace(pivot), mpi::maximum<Key>());
            LOGR << "agreed on new pivot: " << pivot;
        }
        // in case of duplicates, we may get an index larger than max_idx
    } else if (pivot == std::numeric_limits<Key>::max()) {
        stats_.no_pivot++;
        LOGR << "No PE found a viable pivot, using begin";
        ub_pos = lb_pos = 0;
        ub_it = lb_it = min_it;
        if constexpr (do_global_ops) {
            if (local_size > 0)
                pivot = ub_it->first;
            pLOG << "new pivot: " << pivot;
            // select min of all pivots
            mpi::all_reduce(comm_, mpi::inplace(pivot), mpi::minimum<Key>());
        }
    } else {
        // position of smallest item *greater than* pivot
        std::tie(ub_pos, ub_it) = seq.rank_of_upper_bound(pivot);
        // position of smallest item *greater than or equal to* pivot
        std::tie(lb_pos, lb_it) = seq.rank_of_lower_bound(pivot);

        tlx_die_verbose_unless(ub_pos >= 0,
                               "Tree shit the bed: ub = "
                                   << ub_pos << " for pivot " << pivot
                                   << "; rank of = " << seq.rank_of(pivot).first
                                   << "lb = " << lb_pos);

        // use local indices
        ub_pos -= min_idx;
        lb_pos -= min_idx;

        // Check for degenerate cases
        if (ub_pos < 0) {
            stats_.neg_split_pos++;
            // all PEs chose an out-of-range pivot in case 2
            pLOG << "all global elements bigger than pivot: ub_pos = " << ub_pos
                 << " lb_pos = " << lb_pos << " for pivot " << pivot
                 << " min_idx = " << min_idx;
            ub_pos = lb_pos = 0;
            // TODO is this necessary?
            ub_it = lb_it = min_it;
        } else if (ub_pos > local_size) {
            stats_.split_pos_oob++;
            // all PEs chose an out-of-range pivot in case 1
            LOGR << "all global elements smaller than pivot";
            ub_pos = lb_pos = local_size;
            ub_it = lb_it = max_it;
        }

        if (lb_pos < 0) {
            stats_.neg_split_pos++;
            pLOG1 << "got negative lb pos " << lb_pos
                  << "but non-negative ub pos " << ub_pos << ", using 0";
            lb_pos = 0;
            lb_it = min_it;
        }
    }
    pLOG << "ub_pos = " << ub_pos << " lb_pos = " << lb_pos;

    return std::make_tuple(ub_pos, lb_pos, ub_it, lb_it);
}


template <typename Seq, typename Stats, typename Key = typename Seq::key_type,
          typename Iterator = typename Seq::const_iterator>
std::tuple<ssize_t, ssize_t, Iterator, Iterator>
get_bounds(const Seq &seq, Stats &stats_, Key pivot, ssize_t min_idx,
           ssize_t max_idx, mpi::communicator &comm_,
           const std::string &short_name, const bool debug) {
    Iterator min_it = seq.find_rank(min_idx), max_it = seq.find_rank(max_idx);
    return get_bounds<true>(seq, stats_, pivot, min_idx, max_idx, min_it,
                            max_it, comm_, short_name, debug);
}


std::pair<ssize_t, ssize_t> global_bound(const ssize_t ub_pos, const ssize_t lb_pos,
                                         const ssize_t global_size,
                                         mpi::communicator &comm_) {
    // Count how many elements are smaller than the pivot globally
    ssize_t global_lb, global_ub;
    std::pair<ssize_t, ssize_t> global_rank_pair,
        local_rank_pair = std::make_pair(lb_pos, ub_pos);
    mpi::all_reduce(comm_, reinterpret_cast<ssize_t *>(&local_rank_pair), 2,
                    reinterpret_cast<ssize_t *>(&global_rank_pair), std::plus<>());
    std::tie(global_lb, global_ub) = global_rank_pair;

    tlx_die_unless(0 <= global_lb && global_lb <= global_size);
    tlx_die_unless(0 <= global_ub && global_ub <= global_size);

    return std::make_pair(global_ub, global_lb);
}


template <typename Iterator>
std::pair<Iterator, ssize_t>
find_eq_pos(ssize_t global_ub, ssize_t ub_pos, Iterator ub_it,
            ssize_t global_lb, ssize_t lb_pos, Iterator lb_it, ssize_t min_idx,
            ssize_t target_count, mpi::communicator &comm_, const bool debug,
            const std::string &short_name) {
    if (global_lb + 1 >= global_ub) {
        LOGR << "Pivot is unique and the result: lb=" << global_lb
             << " ub=" << global_ub << " want " << target_count;
        // Depending on whether the upper or lower bound matched,
        // target_count is 0 or 1
        if (target_count == 0) {
            return std::make_pair(lb_it, min_idx + lb_pos);
        } else {
            spLOG << "LB:" << *lb_it << "UB:" << *ub_it << "returning UB";
            tlx_die_unless(target_count == 1);
            return std::make_pair(ub_it, min_idx + ub_pos);
        }
    }
    LOGR << "Pivot is not unique, figuring out duplicates";
    // We're *nearly* done, just have the duplicates to figure out.
    // The result's key is equal to the pivot, now we need to figure out
    // just how many of those to include per PE.  We are now considering
    // ranks min_idx + lb_pos to min_idx + ub_pos
    ssize_t my_count = ub_pos - lb_pos, prefsum;
    tlx_die_unless(my_count >= 0);
    // MPI_Scan is an inclusive prefix sum
    mpi::scan(comm_, my_count, prefsum, std::plus<>());
    spLOGC(debug || my_count > 0)
        << "Non-unique pivot, global lb:" << global_lb << "ub:" << global_ub
        << "have" << my_count << "locally, prefsum:" << prefsum;

    if (prefsum < target_count) {
        // return all
        return std::make_pair(ub_it, min_idx + ub_pos);
    } else if (prefsum - my_count > target_count) {
        // return none
        return std::make_pair(lb_it, min_idx + lb_pos);
    } else {
        // return some. Inclusive prefix sum -> re-add my_count
        ssize_t count = target_count - prefsum + my_count;
        spLOG << "Returning some:" << count << "of" << my_count;
        Iterator it = lb_it;
        std::advance(lb_it, count);
        return std::make_pair(it, min_idx + lb_pos + count);
    }
}

} // namespace reservoir::_detail

#endif // RESERVOIR_SELECT_HELPERS_HEADER
