/*******************************************************************************
 * reservoir/reservoir.hpp
 *
 * Distributed Reservoir Sampling
 *
 * Copyright (C) 2019 Lorenz Hübschle-Schneider <lorenz@4z2.de>
 *
 * All rights reserved. Published under the GNU General Public License 3
 ******************************************************************************/

#pragma once

#ifndef RESERVOIR_RESERVOIR_HEADER
#define RESERVOIR_RESERVOIR_HEADER

#include <reservoir/aggregate.hpp>
#include <reservoir/btree_multimap.hpp>
#include <reservoir/logger.hpp>
#include <reservoir/select_helpers.hpp>
#include <reservoir/stats.hpp>
#include <reservoir/timer.hpp>
#include <reservoir/util.hpp>

#include <tlx/define.hpp>
#include <tlx/die/core.hpp>

#include <boost/mpi.hpp>

#include <algorithm>
#include <utility>

namespace mpi = boost::mpi;

namespace reservoir {

template <typename Key, template <typename> typename select_t, typename RNG>
class reservoir {
public:
    static constexpr const char *short_name = "[res]";

    using key_type = Key;
    using reservoir_type = btree_multimap<double, key_type>;
    using select_type = select_t<reservoir_type>;

    static constexpr bool check = false;
    static constexpr bool debug = false;
    static constexpr bool time = true;

    reservoir(mpi::communicator &comm, size_t size, size_t seed)
        : select_(comm, seed + static_cast<size_t>(comm.size() + comm.rank())),
          rng_(seed + static_cast<size_t>(comm.rank())), comm_(comm),
          size_(size), threshold_(0.0), batch_id_(0) {
        LOGRC(check) << "Checking is active, things might be slow!";
    }

    // Iterator should dereference to (weight, id) pairs
    template <typename Iterator>
    void insert(Iterator begin, Iterator end) {
        timer t, t_total;

        pLOG << "batch " << batch_id_ << " beginning";

        // Step 1: process new items locally
        Iterator it = begin;
        size_t count = 0;
        if (threshold_ == 0.0) {
            size_t size_thresh = std::max(3 * size_ / 2, size_ + 500);
            while (it != end && reservoir_.size() < size_thresh) {
                // generate exponentially distributed variables
                double key = rng_.next_exponential(it->first);
                spLOG0 << "item" << *it << "key" << key;
                reservoir_.insert2(key, it->second);
                ++count;
                // catch a compiler bug in the subtree size compilation
                tlx_die_verbose_unless(reservoir_.size() == count,
                                       "WAAAH tree barfed for key "
                                           << key << "size=" << reservoir_.size()
                                           << "count=" << count);
                ++it;
            }
            spLOG0 << "first batch took" << t.get() << "ms";

            // Once we've processed the initial 1.5*size_ elements locally, do
            // some local thresholding whenever the size exceeds 1.1*size_
            size_thresh = std::max(11 * size_ / 10, size_ + 250);
            double local_threshold = 0;

            while (it != end) {
                // Every time the size increases by an integer multiple,
                // determine a new local threshold
                if (reservoir_.size() >= size_thresh) {
                    auto thresh_it = reservoir_.find_rank(size_);
                    local_threshold = thresh_it->first;
                    spLOG0 << "local threshold" << local_threshold
                          << "for reservoir size" << reservoir_.size()
                          << "splitter" << *thresh_it << "after seeing"
                          << it - begin << "elements";

                    // splitting is fast
                    reservoir_type keep, discard;
                    reservoir_.splitAt(keep, size_, thresh_it, discard);
                    reservoir_ = std::move(keep);
                }
                tlx_die_unless(local_threshold > 0);

                it = insert_skip<false>(it, end, local_threshold);
            }
            spLOG0 << "first round of insertions took" << t.get() << "ms";
        } else {
            while (it != end) {
                it = insert_skip<true>(it, end, threshold_);
            }
        }
        pLOG0 << "done processing items";
        if constexpr (time) {
            stats_.record("size", reservoir_.size());
            double t_insert = t.get();
            stats_.record("insert", t_insert);
            LOG0 << "RESULT op=insert pe=" << comm_.rank()
                 << " np=" << comm_.size() << " batchsize=" << end - begin
                 << " batch=" << batch_id_ << " samplesize=" << size_
                 << " time=" << t_insert;
            t.reset();
        }

        pLOG << "batch " << batch_id_ << " finding splitter...";

        // Step 2: find splitter
        auto [split_it, num_keep] = select_(reservoir_, size_);
        if constexpr (time) {
            double t_select = t.get();
            stats_.record("select", t_select);
            LOG0 << "RESULT op=select pe=" << comm_.rank()
                 << " np=" << comm_.size() << " batchsize=" << end - begin
                 << " batch=" << batch_id_ << " samplesize=" << size_
                 << " time=" << t_select;
            t.reset();
        }

        pLOG << "batch " << batch_id_ << " splitting...";

        // Step 3: split
        reservoir_type keep, discard;
        reservoir_.splitAt(keep, static_cast<size_t>(num_keep), split_it, discard);
        reservoir_ = std::move(keep);
        if constexpr (time) {
            double t_split = t.get();
            stats_.record("split", t_split);
            LOG0 << "RESULT op=split pe=" << comm_.rank()
                 << " np=" << comm_.size() << " batchsize=" << end - begin
                 << " batch=" << batch_id_ << " samplesize=" << size_
                 << " time=" << t_split;
            t.reset();
        }

        pLOG << "batch " << batch_id_ << " finding new threshold";

        // Step 4: determine value of new threshold
        double max_local =
            reservoir_.empty() ? 0.0 : std::prev(reservoir_.end())->first;
        threshold_ = mpi::all_reduce(comm_, max_local, mpi::maximum<double>());
        LOGR << "new threshold is " << threshold_;

        if constexpr (check) {
            reservoir_.verify();
            discard.verify();
            tlx_die_unless(static_cast<ssize_t>(reservoir_.size()) == num_keep);
        }

        if constexpr (time) {
            double t_threshold = t.get();
            stats_.record("threshold", t_threshold);
            LOG0 << "RESULT op=threshold pe=" << comm_.rank()
                 << " np=" << comm_.size() << " batchsize=" << end - begin
                 << " batch=" << batch_id_ << " samplesize=" << size_
                 << " time=" << t_threshold;
            t.reset();

            stats_.record("total", t_total.get());
        }

        pLOG << "batch " << batch_id_ << " done";

        ++batch_id_;
    }

    template <typename Callback>
    void sample(Callback &&callback) const {
        for (auto it = reservoir_.begin(); it != reservoir_.end(); ++it) {
            callback(*it);
        }
    }

    _detail::res_stats<time> &get_stats() {
        return stats_;
    }

    auto get_mss_stats() {
        return select_.get_stats();
    }

protected:
    template <size_t w, typename Iterator>
    constexpr double vec_sum(Iterator it) {
        if constexpr (w == 1) {
            return it->first;
        } else if constexpr (w == 3) {
            return it->first + (it + 1)->first + (it + 2)->first;
        } else {
            return vec_sum<w / 2>(it) + vec_sum<w / 2>(it + w / 2);
        }
    }

    template <bool far, typename Iterator>
    TLX_ATTRIBUTE_ALWAYS_INLINE Iterator insert_skip(Iterator it, Iterator end,
                                                     double threshold) {
        double skip = rng_.next_exponential(threshold);
        pLOG0 << "skip = " << skip;

        if constexpr (far) {
            // number of elements to skip at a time
            constexpr size_t w = 32;

            double sum = 0.0;
            // Check that the next w items (including this) appear before `end`
            Iterator curr_last = it + (w - 1);
            Iterator prev;
            while (curr_last < end && skip >= 0) {
                sum = vec_sum<w>(it);

                if constexpr (check) {
                    double s2 = 0.0;
                    for (size_t i = 0; i < w; i++) {
                        s2 += (it + i)->first;
                    }
                    tlx_die_unless(std::abs(s2 - sum) < 1e-10);
                }

                skip -= sum;
                prev = it;
                // Avoid advancing iterator twice, it might not be a random
                // access iterator
                it = curr_last + 1;
                curr_last += w;
            }
            if (skip < 0) {
                // undo jump
                it = prev;
                skip += sum;
            } else if (it >= end) {
                return end;
            }
        }

        while (++it != end && skip >= 0) {
            skip -= it->first;
        }
        if (it == end)
            return it;

        double minv = std::exp(-threshold * it->first);
        double r = rng_.next(minv, 1.0);
        double key = -std::log(r) / it->first;
        my_assert(key > 0);
        spLOG0 << "item" << *it << "minv" << minv << "r" << r << "key" << key;
        reservoir_.insert2(key, it->second);
        return it;
    }

    reservoir_type reservoir_;
    select_type select_;
    RNG rng_;
    mpi::communicator &comm_;
    size_t size_;
    double threshold_;

    size_t batch_id_;
    mutable _detail::res_stats<time> stats_;
};

} // namespace reservoir

#endif // RESERVOIR_RESERVOIR_HEADER
