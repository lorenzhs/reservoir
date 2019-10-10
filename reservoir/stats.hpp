/*******************************************************************************
 * reservoir/stats.hpp
 *
 * Statistics helpers
 *
 * Copyright (C) 2019 Lorenz Hübschle-Schneider <lorenz@4z2.de>
 *
 * All rights reserved. Published under the GNU General Public License 3
 ******************************************************************************/


#pragma once
#ifndef RESERVOIR_STATS_HEADER
#define RESERVOIR_STATS_HEADER

#include <reservoir/aggregate.hpp>

#include <tlx/die/core.hpp>

#include <boost/serialization/unordered_map.hpp>

#include <algorithm>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace reservoir::_detail {

template <bool enabled>
struct res_stats;

template <>
struct res_stats<false> {
    void record(const std::string & /* key */, double /* value */) {}

    double get_throughput() const {
        return 0;
    }

    res_stats &operator+=(const res_stats & /* other */) {
        return *this;
    }

    template <typename Archive>
    void serialize(Archive & /* ar */, const unsigned int /* version */) {}

    friend std::ostream &operator<<(std::ostream &os, const res_stats &) {
        return os << "<no reservoir stats>";
    }
};

template <>
struct res_stats<true> {
    void record(const std::string &key, double value) {
        auto it = stats.find(key);
        if (it == stats.end()) {
            keyseq.push_back(key);
            maxlen = std::max(key.length(), maxlen);
            stats[key].add(value);
        } else {
            it->second.add(value);
        }
    }

    double get_throughput() const {
        auto it = stats.find("total");
        if (it != stats.end()) {
            return 1000.0 / it->second.avg();
        } else {
            return 0.0;
        }
    }

    res_stats &operator+=(const res_stats &other) {
        if (keyseq.empty()) {
            tlx_die_unless(stats.empty());
            stats = other.stats;
            keyseq = other.keyseq;
        } else {
            for (const auto &[key, agg] : other.stats) {
                stats[key] += agg;
            }
        }
        return *this;
    }

    bool has_key(const std::string &key) const {
        return stats.find(key) != stats.end();
    }

    const tlx::Aggregate<double>& operator[](const std::string &key) const {
        return stats.at(key);
    }

    template <typename Archive>
    void serialize(Archive &ar, const unsigned int /* version */) {
        // in our case, we don't need to send the keyseq, it's the same
        // everywhere and "stolen" on demand, see below
        ar &stats;
    }

    void steal_metadata(const res_stats &other) {
        keyseq = other.keyseq;
        maxlen = other.maxlen;
    }

    friend std::ostream &operator<<(std::ostream &os, const res_stats &s) {
        bool first = true;
        for (const auto &key : s.keyseq) {
            os << (first ? "\t" : "\n\t") << key << ": ";
            for (size_t i = key.length(); i < s.maxlen; i++) {
                os << ' ';
            }
            os << s.stats.at(key);
            first = false;
        }
        return os;
    }

    std::unordered_map<std::string, tlx::Aggregate<double>> stats;
    std::vector<std::string> keyseq; // to preserve order of keys
    size_t maxlen = 0; // length of longest key
};

} // namespace reservoir::_detail

#endif // RESERVOIR_STATS_HEADER
