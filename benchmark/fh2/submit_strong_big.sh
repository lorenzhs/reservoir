#!/bin/bash

DIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )

sbatch -N 1 -J strongbig1 -p normal ${DIR}/strong_big.sh 1
sbatch -N 2 -J strongbig2 -p normal ${DIR}/strong_big.sh 2
sbatch -N 4 -J strongbig4 -p normal ${DIR}/strong_big.sh 4
sbatch -N 8 -J strongbig8 -p normal ${DIR}/strong_big.sh 8
sbatch -N 16 -J strongbig16 -p normal ${DIR}/strong_big.sh 16
sbatch -N 32 -J strongbig32 -p normal ${DIR}/strong_big.sh 32
sbatch -N 64 -J strongbig64 -p normal ${DIR}/strong_big.sh 64
sbatch -N 128 -J strongbig128 -p normal ${DIR}/strong_big.sh 128
sbatch -N 256 -J strongbig256 -p normal ${DIR}/strong_big.sh 256
