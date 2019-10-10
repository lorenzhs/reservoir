#!/bin/bash
#SBATCH --ntasks-per-node=20
#SBATCH --time=00:30:00

set -e

DIR=${HOME}/res/benchmark

module load compiler/gnu/8
module load mpi/openmpi/4.0

# each instance should run for at most 2 minutes (10 iterations @ 10s + warmup 10s)
# 2 inputs, 2 instances (AMS, gather) per run -> 8 minutes
# 3 runs -> 24 minutes plus overheads, ca 30 minutes to be safe
$DIR/run.sh -n   10000 -k 100000 -t 10 -i 10 -s 1234567
$DIR/run.sh -n  100000 -k 100000 -t 10 -i 10 -s 42
$DIR/run.sh -n 1000000 -k 100000 -t 10 -i 10 -s 1337
