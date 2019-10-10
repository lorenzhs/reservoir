#!/bin/bash
#SBATCH --ntasks-per-node=20
#SBATCH --time=00:30:00

set -e

DIR=${HOME}/res/benchmark

module load compiler/gnu/8
module load mpi/openmpi/4.0

nodes=${1:-1}
cores=$((nodes * 20))
smallsize=$((10240000  / cores))
largesize=$((102400000 / cores))
vlrgesize=$((1024000000 / cores))

echo "strong scaling with ${nodes} nodes / ${cores} cores"
echo "local batch sizes: small ${smallsize}; large ${largesize}; very large ${vlrgesize}"

# each instance should run for at most 2 minutes (10 iterations @ 10s + warmup 10s)
# 1 input, 2 instances (AMS, gather) per run -> 4 minutes
# 6 runs -> 24 minutes plus overheads, ca 30 minutes to be safe
$DIR/run.sh -n ${smallsize} -k  1000 -t 10 -G -i 10 -s 42
$DIR/run.sh -n ${smallsize} -k 10000 -t 10 -G -i 10 -s 42
$DIR/run.sh -n ${largesize} -k  1000 -t 10 -G -i 10 -s 1337
$DIR/run.sh -n ${largesize} -k 10000 -t 10 -G -i 10 -s 1337
$DIR/run.sh -n ${vlrgesize} -k  1000 -t 10 -G -i 10 -s 1234567
$DIR/run.sh -n ${vlrgesize} -k 10000 -t 10 -G -i 10 -s 1234567