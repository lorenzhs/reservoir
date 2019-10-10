/*******************************************************************************
 * reservoir/reservoir_gather.hpp
 *
 * Naive Distributed Reservoir Sampling
 *
 * Copyright (C) 2019 Lorenz Hübschle-Schneider <lorenz@4z2.de>
 *
 * All rights reserved. Published under the GNU General Public License 3
 ******************************************************************************/

#pragma once

#ifndef RESERVOIR_RESERVOIR_GATHER_HEADER
#define RESERVOIR_RESERVOIR_GATHER_HEADER

#include <reservoir/aggregate.hpp>
#include <reservoir/logger.hpp>
#include <reservoir/select_helpers.hpp>
#include <reservoir/stats.hpp>
#include <reservoir/timer.hpp>
#include <reservoir/util.hpp>

#include <tlx/define.hpp>
#include <tlx/die/core.hpp>

#include <boost/mpi.hpp>

#include <utility>
#include <vector>

namespace mpi = boost::mpi;

namespace reservoir {

namespace _detail {
struct gather_selection {
    static constexpr const char *name = "gather";
};
} // namespace _detail

template <typename Key, typename RNG>
class reservoir_gather {
public:
    static constexpr const char *short_name = "[res]";

    using key_type = Key;
    using select_type = _detail::gather_selection;

    static constexpr bool check = false;
    static constexpr bool debug = false;
    static constexpr bool time = true;

    reservoir_gather(mpi::communicator &comm, size_t size, size_t seed)
        : rng_(seed + static_cast<size_t>(comm.rank())), comm_(comm),
          size_(size), threshold_(0.0), batch_id_(0) {
        LOGRC(check) << "Checking is active, things might be slow!";
    }

    // Iterator should dereference to (weight, id) pairs
    template <typename Iterator>
    void insert(Iterator begin, Iterator end) {
        timer t, t_total;

        pLOG << "batch " << batch_id_ << " beginning";

        items_.clear();

        // Step 1: process new items locally
        Iterator it = begin;

        if (threshold_ == 0.0) {
            // Woop, just insert them all!
            while (it != end) {
                // generate exponentially distributed variables
                double key = rng_.next_exponential(it->first);
                spLOG0 << "item" << *it << "key" << key;
                items_.emplace_back(key, it->second);
                ++it;
            }
            spLOG0 << "first batch took" << t.get() << "ms";
        } else {
            while (it != end) {
                it = insert_skip<true>(it, end, threshold_);
            }
        }
        pLOG0 << "done processing items";

        // Step 1b: local selection
        if (items_.size() > size_) {
            // Select `size_` smallest items locally to avoid transmitting
            // unnecessarily many (and OOM at root)
            std::nth_element(items_.begin(), items_.begin() + size_ - 1,
                             items_.end());
            // Discard the rest
            items_.resize(size_);
        }

        if constexpr (time) {
            stats_.record("size", items_.size());
            double t_insert = t.get();
            stats_.record("insert", t_insert);
            LOG0 << "RESULT op=insert pe=" << comm_.rank()
                 << " np=" << comm_.size() << " batchsize=" << end - begin
                 << " batch=" << batch_id_ << " samplesize=" << size_
                 << " time=" << t_insert;
            t.reset();
        }

        pLOG << "batch " << batch_id_ << " gathering...";

        // Step 2: gather
        if (comm_.rank() == 0) {
            sizes_.resize(comm_.size());
        }
        mpi::gather(comm_, static_cast<int>(items_.size()), sizes_, 0);
        size_t old_size = all_items_.size();
        if (comm_.rank() == 0) {
            // Compute displacements. According to the MPI standard,
            // displacements are significant only at the root, but boost::mpi
            // wants to compute them at every PE if they're not provided...
            int nprocs = comm_.size(), aux = 0;
            displ_.resize(nprocs);
            for (int rank = 0; rank < nprocs; ++rank) {
                displ_[rank] = aux;
                aux += sizes_[rank];
            }
            // Allocate space for receiving all items at the end of the all_items_ vector
            all_items_.resize(old_size + aux);
        }
        mpi::gatherv(comm_, items_, all_items_.data() + old_size, sizes_,
                     displ_, 0);
        if constexpr (time) {
            double t_select = t.get();
            stats_.record("gather", t_select);
            LOG0 << "RESULT op=gather pe=" << comm_.rank()
                 << " np=" << comm_.size() << " batchsize=" << end - begin
                 << " batch=" << batch_id_ << " samplesize=" << size_
                 << " time=" << t_select;
            t.reset();
        }

        // Step 3: sequential selection
        sLOGR << "Have" << all_items_.size()
              << "items under consideration in batch" << batch_id_;
        if (comm_.rank() == 0) {
            // Select result sequentially at root
            std::nth_element(all_items_.begin(), all_items_.begin() + size_ - 1,
                             all_items_.end());
            // Discard the rest
            all_items_.resize(size_);
            threshold_ = all_items_[size_ - 1].first;
            tlx_die_unless(threshold_ > 0);
        }
        // Broadcast new threshold
        mpi::broadcast(comm_, threshold_, 0);
        sLOGR << "Threshold:" << threshold_ << "in batch" << batch_id_;

        if constexpr (time) {
            double t_split = t.get();
            stats_.record("select", t_split);
            LOG0 << "RESULT op=select pe=" << comm_.rank()
                 << " np=" << comm_.size() << " batchsize=" << end - begin
                 << " batch=" << batch_id_ << " samplesize=" << size_
                 << " time=" << t_split;
            t.reset();

            stats_.record("total", t_total.get());
        }

        pLOG << "batch " << batch_id_ << " done";

        ++batch_id_;
    }

    template <typename Callback>
    void sample(Callback &&callback) const {
        // only relevant at PE 0
        for (auto it = all_items_.begin(); it != all_items_.end(); ++it) {
            callback(it->second);
        }
    }

    _detail::res_stats<time> &get_stats() {
        return stats_;
    }

    auto get_mss_stats() {
        return _detail::select_stats<false>();
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
        items_.emplace_back(key, it->second);
        return it;
    }

    // all_items is significant only at PE 0
    std::vector<std::pair<double, Key>> items_, all_items_;
    std::vector<int> sizes_, displ_;
    RNG rng_;
    mpi::communicator &comm_;
    size_t size_;
    double threshold_;

    size_t batch_id_;
    mutable _detail::res_stats<time> stats_;
};

} // namespace reservoir

#endif // RESERVOIR_RESERVOIR_GATHER_HEADER
