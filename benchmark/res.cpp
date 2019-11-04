/*******************************************************************************
 * benchmark/res.cpp
 *
 * Distributed Reservoir Sampling
 *
 * Copyright (C) 2019 Lorenz Hübschle-Schneider <lorenz@4z2.de>
 *
 * All rights reserved. Published under the GNU General Public License 3
 ******************************************************************************/

// enable btree debug mode by uncommenting this line
// #define TLX_BTREE_DEBUG

#include <reservoir/aggregate.hpp>
#include <reservoir/ams_select.hpp>
#include <reservoir/ams_select_multi.hpp>
#include <reservoir/btree_multiset.hpp>
#include <reservoir/generators/select.hpp>
#include <reservoir/logger.hpp>
#include <reservoir/reservoir.hpp>
#include <reservoir/reservoir_gather.hpp>
#include <reservoir/timer.hpp>

#include <tlx/cmdline_parser.hpp>

#include <random>
#include <sstream>

#ifdef TLX_BTREE_DEBUG
#pragma message "B-Tree Debugging is enabled!"
#endif

// ugly stats printing macros
// clang-format off
#define PRINT_RESSTAT(outname, key)                                            \
       " " #outname "="                                                        \
    << (stats.res_stats.has_key(#key) ? stats.res_stats[#key].mean() : 0)      \
    << " " #outname "dev="                                                     \
    << (stats.res_stats.has_key(#key) ? stats.res_stats[#key].stdev() : 0)
#define PRINT_STAT(outname, key)                                               \
       " " #outname "=" << stats.key##_stats.mean()                            \
    << " " #outname "dev=" << stats.key##_stats.stdev()
// clang-format on


template <typename T, template <typename> typename select>
using res = reservoir::reservoir<T, select, reservoir::generators::select_t>;

template <typename T>
using res_gather =
    reservoir::reservoir_gather<T, reservoir::generators::select_t>;

template <int d>
struct amm_wrapper {
    template <typename T>
    using type = reservoir::ams_select_multi<T, d>;
};

struct arguments {
    size_t batch_size;
    size_t sample_size;
    size_t min_batches;
    size_t max_batches;
    size_t seed;
    double min_time;
    double max_time;
    int iterations;
    int warmup_its;
    bool verbose;

    friend std::ostream &operator<<(std::ostream &os, const arguments &args) {
        return os << "batch_size=" << args.batch_size
                  << " sample_size=" << args.sample_size << " seed=" << args.seed;
    }
};

template <typename res_stats_t, typename sel_stats_t>
struct stats_pack {
    res_stats_t res_stats;
    sel_stats_t sel_stats;
    tlx::Aggregate<double> gen_stats, batch_stats, total_stats, rounds_stats;

    void steal_common_metadata(const stats_pack &other) {
        res_stats.steal_metadata(other.res_stats);
        sel_stats.steal_metadata(other.sel_stats);
    }

    void add(const stats_pack &other, bool add_rounds) {
        res_stats += other.res_stats;
        sel_stats += other.sel_stats;
        gen_stats += other.gen_stats;
        batch_stats += other.batch_stats;
        total_stats += other.total_stats;
        if (add_rounds) {
            rounds_stats += other.rounds_stats;
        }
    }

    stats_pack &operator+=(const stats_pack &other) {
        add(other, true);
        return *this;
    }

    template <typename Archive>
    void serialize(Archive &ar, const unsigned int /* version */) {
        ar &res_stats;
        ar &sel_stats;
        ar &gen_stats;
        ar &batch_stats;
        ar &total_stats;
        ar &rounds_stats;
    }
};

template <typename reservoir_t, typename input_gen_t>
auto run(const arguments &args, input_gen_t &&input_gen,
         const std::string &input_name, mpi::communicator &comm_, bool log) {
    // logger config
    const std::string short_name = "[main]";

    // only log stuff if `log` is set (i.e., this is not a warmup)
    bool debug = log;

    LOGR << "Using " << input_name << " input generator";
    LOGR << "Using " << reservoir_t::select_type::name() << " selection";
    reservoir_t res(comm_, args.sample_size, args.seed);

    reservoir::generators::select_t rng(
        args.seed + static_cast<size_t>(2 * comm_.size() + comm_.rank()));
    LOGR << "Using " << decltype(rng)::name << " random generator";

    std::vector<std::pair<double, int>> input(args.batch_size);
    tlx::Aggregate<double> gen_stats, batch_stats;

    reservoir::timer t_batch, t_total;
    size_t round = 0;
    while (true) {
        // First, check that we don't run too many rounds
        if (round >= args.max_batches) {
            sLOGR << "Done after" << round << "batches (max reached) in"
                  << t_total.get() / 1000 << "seconds";
            break;
        }
        // Next, check that we don't run too long or short
        if (args.min_time > 0 || args.max_time > 0) {
            double time = t_total.get() / 1000.0;
            // time at PE 0 is what counts (to avoid synchronisation issues)
            mpi::broadcast(comm_, time, 0);

            if (time >= args.max_time) {
                sLOGR << "Done after" << time
                      << "seconds (max reached), handled" << round << "batches";
                break;
            }
            if (time >= args.min_time && round >= args.min_batches) {
                sLOGR << "Done after" << round << ">=" << args.min_batches
                      << "batches and" << time << ">=" << args.min_time
                      << "seconds, both minima met";
                break;
            }
        }

        comm_.barrier();
        t_batch.reset(); // don't measure initial barrier (why?)

        reservoir::timer t_gen;
        input_gen(rng, input, args.batch_size, round, comm_.rank());
        gen_stats.add(t_gen.get());
        comm_.barrier(); // todo remove?

        res.insert(input.begin(), input.end());

        res.sample([&](const auto &) { /* just discard it */ });
        batch_stats.add(t_batch.get_and_reset());
        ++round;
    }
    double total = t_total.get();

    // Print statistics in an orderly manner
    std::stringstream s;
    auto res_stats = res.get_stats();
    auto sel_stats = res.get_mss_stats();
    tlx::Aggregate<double> total_stats, rounds_stats;
    total_stats.add(total);
    rounds_stats.add(round);

    using stats_t = stats_pack<decltype(res_stats), decltype(sel_stats)>;
    stats_t global_stats;
    stats_t my_stats = {res_stats,   sel_stats,   gen_stats,
                        batch_stats, total_stats, rounds_stats};
    global_stats.steal_common_metadata(my_stats);
    // hack to prevent printing of per-level selection stats for PE0 and copying
    // them as global stats
    my_stats.steal_common_metadata(my_stats);

    if (comm_.rank() == 0) {
        auto print = [&](int pe, const stats_t &stats) {
            if (pe >= 0) {
                LOG << "PE " << pe << " res stats, " << round << " rounds:";
                LOG << stats.res_stats;
                LOG << "PE " << pe << " mss stats:";
                LOG << stats.sel_stats;
                LOG << "PE " << pe << " gen stats: " << stats.gen_stats;
                LOG << "PE " << pe << " batch stats: " << stats.batch_stats;
                LOG << "PE " << pe << " total stats: " << stats.total_stats;
                LOG << "";
            } else {
                const double tp = stats.res_stats.get_throughput();

                LOG << "RESULT type=it np=" << comm_.size()
                    << " tpp=" << tp * args.batch_size
                    << " tpt=" << tp * args.batch_size * comm_.size()
                    << PRINT_RESSTAT(total, total) << PRINT_RESSTAT(tins, insert)
                    << PRINT_RESSTAT(tsel, select) << PRINT_RESSTAT(tsplit, split)
                    << PRINT_RESSTAT(tthresh, threshold)
                    << PRINT_RESSTAT(tgather, gather)
                    << PRINT_RESSTAT(rsize, size) << PRINT_STAT(tgen, gen)
                    << PRINT_STAT(tbatch, batch) << PRINT_STAT(titer, total)
                    << PRINT_STAT(rounds, rounds)
                    << " recdepth=" << stats.sel_stats.depth.mean()
                    << " recdepthdev=" << stats.sel_stats.depth.stdev() << " "
                    << args << " input=" << input_name
                    << " selection=" << reservoir_t::select_type::name();

                LOG << "Arguments: " << args << "; ran for " << round << " rounds";
                LOG << "Global res stats using "
                    << reservoir_t::select_type::name() << " selection:";
                sLOG << "\tThroughput:" << tp
                     << "batches/s =" << tp * args.batch_size << "items/s per PE,"
                     << tp * args.batch_size * comm_.size() << "items/s total";
                LOG << stats.res_stats;

                LOG << "Global sel stats:";
                LOG << stats.sel_stats;

                LOG << "Global gen stats: " << stats.gen_stats;
                LOG << "Global batch stats: " << stats.batch_stats;
                LOG << "Global total stats: " << stats.total_stats;
            }
        };

        global_stats += my_stats;
        if (log && args.verbose)
            print(0, my_stats);

        for (int r = 1; r < comm_.size(); ++r) {
            stats_t stats;
            stats.steal_common_metadata(my_stats);
            comm_.recv(r, 0, stats);
            global_stats.add(stats, false);
            if (log && args.verbose)
                print(r, stats);
        }

        if (comm_.size() > 1 || !args.verbose)
            print(-1, global_stats);

        sLOG << "stats printing took" << t_batch.get() << "ms";
    } else {
        comm_.send(0, 0, my_stats);
    }

    return global_stats;
}

// args is copied because we modify the seed -- don't change this
template <typename reservoir_t, typename input_gen_t>
void benchmark(arguments args, input_gen_t &&input_gen,
               const std::string &input_name, mpi::communicator &comm_) {
    const std::string short_name = "[meta]";
    using stats_type =
        decltype(run<reservoir_t>(args, input_gen, input_name, comm_, true));
    stats_type stats;

    for (int iter = -args.warmup_its; iter < args.iterations; iter++) {
        sLOGR1 << "Starting iteration" << iter + 1 << "of" << args.iterations
               << "with" << reservoir_t::select_type::name() << "selection,"
               << input_name << "input";
        reservoir::timer timer;
        auto it_stats =
            run<reservoir_t>(args, input_gen, input_name, comm_, iter >= 0);
        if (iter >= 0) { // discard warmups
            stats += it_stats;
        }
        // every run consumes up to 3p seeds
        args.seed += 3 * comm_.size();

        LOGC(iter >= 0 && comm_.rank() == 0)
            << "\n[meta] run took " << timer.get() << "ms\n"
            << "==========================================================\n";
    }

    if (comm_.rank() == 0) {
        double tp = stats.res_stats.get_throughput() * args.batch_size;

        LOG1 << "RESULT type=agg np=" << comm_.size() << " tpp=" << tp
             << " tpt=" << tp * comm_.size() << PRINT_RESSTAT(total, total)
             << PRINT_RESSTAT(tins, insert) << PRINT_RESSTAT(tsel, select)
             << PRINT_RESSTAT(tsplit, split) << PRINT_RESSTAT(tthresh, threshold)
             << PRINT_RESSTAT(tgather, gather) << PRINT_RESSTAT(rsize, size)
             << PRINT_STAT(tgen, gen) << PRINT_STAT(tbatch, batch)
             << PRINT_STAT(titer, total) << PRINT_STAT(rounds, rounds)
             << " recdepth=" << stats.sel_stats.depth.mean()
             << " recdepthdev=" << stats.sel_stats.depth.stdev() << " " << args
             << " input=" << input_name
             << " selection=" << reservoir_t::select_type::name();
    }

    if (args.iterations > 1 && comm_.rank() == 0) {
        LOG1 << "Overall reservoir statistics using "
             << reservoir_t::select_type::name() << " selection, " << input_name
             << " input:";
        double tp = stats.res_stats.get_throughput();
        sLOG1 << "\tThroughput:" << tp << "batches/s =" << tp * args.batch_size
              << "items/s per PE," << tp * args.batch_size * comm_.size()
              << "items/s total";
        LOG1 << stats.res_stats;
        LOG1 << "Overall selection statistics for "
             << reservoir_t::select_type::name() << ":";
        LOG1 << stats.sel_stats;
        LOG1 << "Overall gen stats: " << stats.gen_stats;
        LOG1 << "Overall batch stats: " << stats.batch_stats;
        LOG1 << "Overall total stats: " << stats.total_stats;
        LOG1 << "Overall #rounds stats: " << stats.rounds_stats;
        LOG1 << "\n==========================================================";
        LOG1 << "==========================================================\n";
    }
}

int main(int argc, char *argv[]) {
    mpi::environment env(argc, argv);
    mpi::communicator comm_;

    sLOGC(comm_.rank() == 0) << "Running with" << comm_.size() << "PEs";

    tlx::CmdlineParser clp;

    size_t batch_size = 1000, sample_size = 100, min_batches = 1,
           max_batches = -1, seed = 0;
    int iterations = 1;
    double min_time = -1, max_time = 600, mean_offset = 0.0, batch_weight = 1.0,
           rank_weight = 0.0, stdev_offset = 10.0, np_weight = 0.0;
    bool verbose = false, no_warmup = false, no_ams = false, no_amm8 = false,
         no_amm16 = false, no_amm32 = false, no_amm64 = false,
         no_gather = false, no_uniform = false, no_gauss = false;
    // bool no_mss_naive = false;
    clp.add_size_t('n', "batchsize", batch_size, "batch size");
    clp.add_size_t('k', "samples", sample_size, "number of samples");
    clp.add_int('i', "iterations", iterations, "number of iterations");

    clp.add_size_t('b', "minbatches", min_batches, "number of batches");
    clp.add_size_t('B', "maxbatches", max_batches, "max number of batches");
    clp.add_double('t', "mintime", min_time,
                   "minimum number of seconds to run per iteration");
    clp.add_double('T', "maxtime", max_time,
                   "maximum number of seconds to run per iteration");

    clp.add_double('m', "mean", mean_offset, "mean of gaussian input");
    clp.add_double('w', "batchweight", batch_weight, "weight of batch ID on mean");
    clp.add_double('x', "rankweight", rank_weight, "weight of PE rank on mean");
    clp.add_double('y', "stdev", stdev_offset, "standard deviation constant term");
    clp.add_double('z', "npweight", np_weight, "stdev weight of #PEs");

    clp.add_size_t('s', "seed", seed, "seed (0 for random)");
    clp.add_bool('v', "verbose", verbose, "verbose");
    clp.add_bool('W', "no-warmup", no_warmup, "don't do a warmup run");

    clp.add_bool('3', "no-amm8", no_amm8, "don't run ams-multi8");
    clp.add_bool('4', "no-amm16", no_amm16, "don't run ams-multi16");
    clp.add_bool('5', "no-amm32", no_amm32, "don't run ams-multi32");
    clp.add_bool('6', "no-amm64", no_amm64, "don't run ams-multi64");

    clp.add_bool('A', "no-ams", no_ams, "don't run ams-select");
    clp.add_bool('X', "no-gather", no_gather,
                 "don't run naive gathering algorithm");

    clp.add_bool('U', "no-uniform", no_uniform, "don't run uniform input");
    clp.add_bool('G', "no-gauss", no_gauss, "don't run gauss");

    if (!clp.process(argc, argv)) {
        return -1;
    }
    if (seed == 0) {
        if (comm_.rank() == 0)
            seed = std::random_device{}();
        mpi::broadcast(comm_, seed, 0);
    }
    if (comm_.rank() == 0)
        clp.print_result();

    int warmup_its = no_warmup ? 0 : 1;
    const arguments args = {batch_size, sample_size, min_batches, max_batches,
                            seed,       min_time,    max_time,    iterations,
                            warmup_its, verbose};

    std::vector<double> aux;
    auto uniform_gen = [&aux](auto &rng, auto &input, size_t count,
                              size_t round, int /* rank */) {
        rng.generate_block(aux, count, true);
        const size_t id_offset = round * count;

        for (size_t i = 0; i < count; i++) {
            input[i] =
                std::make_pair(aux[i] * 100.0, static_cast<int>(id_offset + i));
        }
    };

    // immediately compute final standard deviation, it's the same every time
    const double stdev = stdev_offset + np_weight * comm_.size();
    // abs(gaussian) input (to avoid negative weights)
    auto gauss_gen = [&](auto &rng, auto &input, size_t count, size_t round,
                         int rank) {
        const double mean = mean_offset + batch_weight * round + rank_weight * rank;
        const size_t id_offset = round * count;

        rng.generate_gaussian_block(mean, stdev, aux, count);

        for (size_t i = 0; i < count; i++) {
            input[i] =
                std::make_pair(std::abs(aux[i]), static_cast<int>(id_offset + i));
        }
    };

    std::string gauss_name =
        "gauss(" + std::to_string(mean_offset) + "+" +
        std::to_string(batch_weight) + "i+" + std::to_string(rank_weight) + "r," +
        std::to_string(stdev_offset) + "+p*" + std::to_string(np_weight) + ")";

    if (!no_ams) {
        if (!no_uniform)
            benchmark<res<int, reservoir::ams_select>>(args, uniform_gen, "uni",
                                                       comm_);
        if (!no_gauss)
            benchmark<res<int, reservoir::ams_select>>(args, gauss_gen,
                                                       gauss_name, comm_);
    }

    if (!no_amm8) {
        if (!no_uniform)
            benchmark<res<int, amm_wrapper<8>::type>>(args, uniform_gen, "uni",
                                                      comm_);
        if (!no_gauss)
            benchmark<res<int, amm_wrapper<8>::type>>(args, gauss_gen,
                                                      gauss_name, comm_);
    }

    if (!no_amm16) {
        if (!no_uniform)
            benchmark<res<int, amm_wrapper<16>::type>>(args, uniform_gen, "uni",
                                                       comm_);
        if (!no_gauss)
            benchmark<res<int, amm_wrapper<16>::type>>(args, gauss_gen,
                                                       gauss_name, comm_);
    }

    if (!no_amm32) {
        if (!no_uniform)
            benchmark<res<int, amm_wrapper<32>::type>>(args, uniform_gen, "uni",
                                                       comm_);
        if (!no_gauss)
            benchmark<res<int, amm_wrapper<32>::type>>(args, gauss_gen,
                                                       gauss_name, comm_);
    }

    if (!no_amm64) {
        if (!no_uniform)
            benchmark<res<int, amm_wrapper<64>::type>>(args, uniform_gen, "uni",
                                                       comm_);
        if (!no_gauss)
            benchmark<res<int, amm_wrapper<64>::type>>(args, gauss_gen,
                                                       gauss_name, comm_);
    }

    if (!no_gather) {
        if (!no_uniform)
            benchmark<res_gather<int>>(args, uniform_gen, "uni", comm_);
        if (!no_gauss)
            benchmark<res_gather<int>>(args, gauss_gen, gauss_name, comm_);
    }
}
