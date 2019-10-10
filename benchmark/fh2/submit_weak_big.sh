#!/bin/bash

DIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )

sbatch -N 1 -J weakbig1 -p normal ${DIR}/weak_big.sh
sbatch -N 2 -J weakbig2 -p normal ${DIR}/weak_big.sh
sbatch -N 4 -J weakbig4 -p normal ${DIR}/weak_big.sh
sbatch -N 8 -J weakbig8 -p normal ${DIR}/weak_big.sh
sbatch -N 16 -J weakbig16 -p normal ${DIR}/weak_big.sh
sbatch -N 32 -J weakbig32 -p normal ${DIR}/weak_big.sh
sbatch -N 64 -J weakbig64 -p normal ${DIR}/weak_big.sh
sbatch -N 128 -J weakbig128 -p normal ${DIR}/weak_big.sh
sbatch -N 256 -J weakbig256 -p normal ${DIR}/weak_big.sh
