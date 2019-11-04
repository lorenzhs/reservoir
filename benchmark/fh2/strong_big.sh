#!/bin/bash
#SBATCH --ntasks-per-node=20
#SBATCH --time=01:00:00

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

# each instance should run for at most 6 minutes (10 iterations @ 30s + warmup 30s)
# 1 input, 3 instances (AMS, AMM8, gather) per run -> 18 minutes
# 3 runs -> 54 minutes plus overheads, ca 60 minutes to be safe
$DIR/run.sh -n ${smallsize} -k 100000 -t 30 -4 -5 -6 -M -G -i 10 -s 42
$DIR/run.sh -n ${largesize} -k 100000 -t 30 -4 -5 -6 -M -G -i 10 -s 1337
$DIR/run.sh -n ${vlrgesize} -k 100000 -t 30 -4 -5 -6 -M -G -i 10 -s 1234567
