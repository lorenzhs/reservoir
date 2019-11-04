#!/bin/bash
#SBATCH --ntasks-per-node=20
#SBATCH --time=01:58:00

set -e

DIR=${HOME}/res/benchmark

module load compiler/gnu/8
module load mpi/openmpi/4.0

# each instance should run for at most 6 minutes (10 iterations @ 30s + warmup 10s)
# 1 input, 3 instances (AMS, AMM8, gather) per run -> 18 minutes
# 6 runs -> 108 minutes plus overheads, ca 118 minutes to be safe
$DIR/run.sh -n   10000 -k  1000 -t 30 -4 -5 -6 -M -G -i 10 -s 1234567
$DIR/run.sh -n   10000 -k 10000 -t 30 -4 -5 -6 -M -G -i 10 -s 1234567
$DIR/run.sh -n  100000 -k  1000 -t 30 -4 -5 -6 -M -G -i 10 -s 42
$DIR/run.sh -n  100000 -k 10000 -t 30 -4 -5 -6 -M -G -i 10 -s 42
$DIR/run.sh -n 1000000 -k  1000 -t 30 -4 -5 -6 -M -G -i 10 -s 1337
$DIR/run.sh -n 1000000 -k 10000 -t 30 -4 -5 -6 -M -G -i 10 -s 1337
