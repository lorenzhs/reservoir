/*******************************************************************************
 * reservoir/generators/dSFMT.hpp
 *
 * Generate random deviates using dSFMT
 *
 * Copyright (C) 2018-2019 Lorenz Hübschle-Schneider <lorenz@4z2.de>
 *
 * All rights reserved. Published under the GNU General Public License 3
 ******************************************************************************/

#pragma once
#ifndef RESERVOIR_GENERATORS_DSFMT_HEADER
#define RESERVOIR_GENERATORS_DSFMT_HEADER

#include <reservoir/generators/dSFMT_internal.hpp>
#include <reservoir/util.hpp>

#include <tlx/define.hpp>
#include <tlx/logger.hpp>

#ifdef RESERVOIR_HAVE_MKL
#include <mkl.h>
#include <mkl_vsl.h>
#endif

#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace reservoir::generators {

/*!
 * A wrapper around dSFMT
 */
class dSFMT {
public:
    static constexpr bool debug = true;
    static const char *name;

    explicit dSFMT(size_t seed) {
        _dSFMT::dsfmt_init_gen_rand(&dsfmt_, seed);
    }

    //! non-copyable: delete copy-constructor
    dSFMT(const dSFMT &) = delete;
    //! non-copyable: delete assignment operator
    dSFMT &operator=(const dSFMT &) = delete;
    //! move-constructor: default
    dSFMT(dSFMT &&) = default;
    //! move-assignment operator: default
    dSFMT &operator=(dSFMT &&) = default;

    //! Re-seed the dsfmt
    void seed(size_t seed) {
        _dSFMT::dsfmt_init_gen_rand(&dsfmt_, seed);
        // Reset all counters, too
        block_id_ = 0;
        block_size_ = 0;
        index_ = 0;
    }

    //! Minimum number of elements that needs to be generated at a time
    size_t minimum_block_size() const {
        return _dSFMT::dsfmt_get_min_array_size();
    }

    //! Minimum number of elements that needs to be generated at a time for
    //! reasonable performance
    size_t minimum_reasonable_block_size() const {
        return minimum_block_size();
    }

    //! Generate `size` [0,1) doubles in `output` or (0, 1] if left_open is set
    RESERVOIR_ATTRIBUTE_NOINLINE
    void generate_block(std::vector<double> &output, size_t size,
                        bool left_open = false) {
        // Ensure minimum block size (normally 382)
        const size_t min_size = _dSFMT::dsfmt_get_min_array_size();
        if (size < min_size) {
            sLOG << "dSFMT: requested fewer than" << min_size
                 << "deviates, namely" << size;
            size = min_size;
        }
        // resize if the output vector is too small
        if (size > output.size()) {
            output.resize(size);
        }
        if (left_open)
            _dSFMT::dsfmt_fill_array_open_close(&dsfmt_, output.data(), size);
        else
            _dSFMT::dsfmt_fill_array_close_open(&dsfmt_, output.data(), size);
    }

    //! Generate `size` [0,1) doubles in `output` or (0, 1] if left_open is set
    RESERVOIR_ATTRIBUTE_NOINLINE
    void generate_block(double *arr, size_t size, bool left_open = false) {
        constexpr bool debug = false;

        // Ensure minimum block size (normally 382)
        const size_t min_size = _dSFMT::dsfmt_get_min_array_size();
        if (size < min_size) {
            sLOG << "dSFMT: requested fewer than" << min_size
                 << "deviates, namely" << size << " -- fallback to next()";
            for (size_t i = 0; i < size; i++) {
                arr[i] = next();
            }
            return;
        }

        if (left_open)
            _dSFMT::dsfmt_fill_array_open_close(&dsfmt_, arr, size);
        else
            _dSFMT::dsfmt_fill_array_close_open(&dsfmt_, arr, size);
    }

    //! Generate `size` log(uniform double) values
    void generate_log_block(double *output, size_t size) {
        // Generate left-open block, i.e. 0-exclusive, 1-inclusive
        generate_block(output, size, true);

#ifdef RESERVOIR_HAVE_MKL
        MKL_INT count = static_cast<MKL_INT>(size);
        // TODO check whether MKL supports inplace log
        vdLn(count, output, output);
#else
        for (size_t i = 0; i < size; i++) {
            output[i] = std::log(output[i]);
        }
#endif
    }

    //! Generate `size` log(uniform double) values
    void generate_log_block(std::vector<double> &output, size_t size) {
        // Generate left-open block, i.e. 0-exclusive, 1-inclusive
        generate_block(output, size, true); // resizes if needed

#ifdef RESERVOIR_HAVE_MKL
        MKL_INT count = static_cast<MKL_INT>(size);
        // TODO check whether MKL supports inplace log
        vdLn(count, output.data(), output.data());
#else
        for (size_t i = 0; i < size; i++) {
            output[i] = std::log(output[i]);
        }
#endif
    }

    //! Get a single [0,1) double. Computes increasingly large blocks internally
    //! so that this is fast.  May block while next block is generated.
    TLX_ATTRIBUTE_ALWAYS_INLINE
    double next() {
        if (TLX_UNLIKELY(index_ >= block_size_)) {
            if (block_id_ > 2 && ((block_id_ + 1) & block_id_) == 0) {
                // block_id_ + 1 is a power of two. We appear to need a lot of
                // random numbers, increase the blocksize to reduce RNG overhead
                block_size_ *= 2;
            }
            block_size_ = std::max(block_size_, minimum_reasonable_block_size());
            // generate_block takes care of resizing the vector for us
            generate_block(randblock_, block_size_);
            index_ = 0;
            block_id_++;
        }
        return randblock_[index_++];
    }

    //! Get a single log((0,1]) double. Computes increasingly large blocks internally
    //! so that this is fast.  May block while next block is generated.
    TLX_ATTRIBUTE_ALWAYS_INLINE
    double next_log() {
        if (TLX_UNLIKELY(logindex_ >= logblock_size_)) {
            if (logblock_id_ > 2 && ((logblock_id_ + 1) & logblock_id_) == 0) {
                // logblock_id_ + 1 is a power of two. We appear to need a lot of
                // random numbers, increase the blocksize to reduce RNG overhead
                logblock_size_ *= 2;
            }
            logblock_size_ =
                std::max(logblock_size_, minimum_reasonable_block_size());
            // generate_log_block takes care of resizing the vector for us
            generate_log_block(logblock_, logblock_size_);
            logindex_ = 0;
            logblock_id_++;
        }
        return logblock_[logindex_++];
    }

    TLX_ATTRIBUTE_ALWAYS_INLINE
    double next_exponential(double lambda) {
        return -next_log() / lambda;
    }

    //! Generate a uniform double from [min, max)
    TLX_ATTRIBUTE_ALWAYS_INLINE
    double next(double min, double max) {
        return next() * (max - min) + min;
    }

    //! Generate a uniform integer from [min, max] (i.e., both inclusive)
    template <typename int_t>
    TLX_ATTRIBUTE_ALWAYS_INLINE int_t next_int(int_t min, int_t max) {
        return next() * (max - min + 1) + min;
    }

    //! Bernoulli trial with success probability p
    TLX_ATTRIBUTE_ALWAYS_INLINE
    bool next_bernoulli(double p) {
        assert(0 <= p && p <= 1);
        return next() < p;
    }

    //! Bernoulli trial with success probability cutoff/max
    TLX_ATTRIBUTE_ALWAYS_INLINE
    bool next_bernoulli(double cutoff, double max) {
        assert(0 <= cutoff && cutoff <= max);
        return next() * max < cutoff;
    }

    //! Generate a normally distributed value. This needs two uniform deviates,
    //! if you need more than one, look at next_two_gaussians.
    TLX_ATTRIBUTE_ALWAYS_INLINE
    double next_gaussian(double mean, double stdev) {
        double U = next(), V = next();
        double a = stdev * std::sqrt(-2 * std::log(U));
        double b = 2 * M_PI * V;

        return mean + a * std::cos(b);
    }

    //! Generate two independent normally distributed values
    TLX_ATTRIBUTE_ALWAYS_INLINE
    std::pair<double, double> next_two_gaussians(double mean, double stdev) {
        double U = next(), V = next();
        double a = stdev * std::sqrt(-2 * std::log(U));
        double b = 2 * M_PI * V;

        U = mean + a * std::cos(b);
        V = mean + a * std::sin(b);
        return std::make_pair(U, V);
    }

    //! Generate `size` uniform integers from [min, max] (i.e., both inclusive)
    template <typename int_t>
    void generate_int_block(int_t min, int_t max, int_t *arr, size_t size) {
        for (size_t i = 0; i < size; ++i) {
            arr[i] = next_int(min, max);
        }
    }

    //! Generate `size` uniform integers from [min, max] (i.e., both inclusive)
    template <typename int_t>
    void generate_int_block(int_t min, int_t max, std::vector<int_t> &output,
                            size_t size) {
        if (size > output.size()) {
            output.resize(size);
        }
        generate_int_block(min, max, output.data(), size);
    }

    //! Generate `size` geometrically integers with parameter p
    template <typename int_t>
    void generate_geometric_block(double p, int_t *arr, size_t size) {
        const double denominator = std::log(1.0 - p);
        for (size_t i = 0; i < size; ++i) {
            arr[i] = std::log(next()) / denominator;
        }
    }

    //! Generate `size` geometrically integers with parameter p
    template <typename int_t>
    void generate_geometric_block(double p, std::vector<int_t> &output,
                                  size_t size) {
        if (size > output.size()) {
            output.resize(size);
        }
        generate_geometric_block(p, output.data(), size);
    }

    //! Generate `size` exponentially distributed integers with rate `lambda`
    //! and displacement `displacement`
    void generate_exponential_block(double lambda, double *arr, size_t size) {
        generate_block(arr, size);

        for (size_t i = 0; i < size; ++i) {
            arr[i] = -std::log(arr[i]) / lambda;
        }
    }

    //! Generate `size` exponentially distributed integers with rate `lambda`
    //! and displacement `displacement`
    void generate_exponential_block(double lambda, std::vector<double> &output,
                                    size_t size) {
        if (size > output.size()) {
            output.resize(size);
        }
        generate_exponential_block(lambda, output.data(), size);
    }

    //! Generate `size` normally distributed integers with mean `mean` and
    //! standard deviation `stdev` using the Box-Muller (2) method.  Faster
    //! methods exist (e.g. g++'s std::normal_distribution)
    void generate_gaussian_block(double mean, double stdev, double *arr,
                                 size_t size) {
        // this method generates two at a time, handle the last element
        // differently if size is odd
        const bool odd = (size % 2 == 1);
        if (odd)
            --size;

        // first generate uniform values
        generate_block(arr, size);

        for (size_t i = 0; i < size; i += 2) {
            double U = arr[i], V = arr[i + 1];
            double a = stdev * std::sqrt(-2 * std::log(U));
            double b = 2 * M_PI * V;

            arr[i] = mean + a * std::cos(b);
            arr[i + 1] = mean + a * std::sin(b);
        }

        if (odd) {
            arr[size - 1] = next_gaussian(mean, stdev);
        }
    }

    //! Generate `size` normally distributed integers with mean `mean` and
    //! standard deviation `stdev` using the Box-Muller (2) method.  Faster
    //! methods exist (e.g. g++'s std::normal_distribution)
    void generate_gaussian_block(double mean, double stdev,
                                 std::vector<double> &output, size_t size) {
        // only even sizes are supported
        if (size % 2 == 1)
            ++size;

        if (size > output.size()) {
            output.resize(size);
        }
        generate_gaussian_block(mean, stdev, output.data(), size);
    }

    //! Alias for next()
    TLX_ATTRIBUTE_ALWAYS_INLINE
    double operator()() {
        return next();
    }

private:
    _dSFMT::dsfmt_t dsfmt_;
    std::vector<double> randblock_;
    std::vector<double> logblock_;
    size_t index_ = 0, block_size_ = 0, block_id_ = 0;
    size_t logindex_ = 0, logblock_size_ = 0, logblock_id_ = 0;
};

} // namespace reservoir::generators

#endif // RESERVOIR_GENERATORS_DSFMT_HEADER
