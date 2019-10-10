#!/bin/bash

DIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )

sbatch -N 1 -J strong1 -p normal ${DIR}/strong.sh 1
sbatch -N 2 -J strong2 -p normal ${DIR}/strong.sh 2
sbatch -N 4 -J strong4 -p normal ${DIR}/strong.sh 4
sbatch -N 8 -J strong8 -p normal ${DIR}/strong.sh 8
sbatch -N 16 -J strong16 -p normal ${DIR}/strong.sh 16
sbatch -N 32 -J strong32 -p normal ${DIR}/strong.sh 32
sbatch -N 64 -J strong64 -p normal ${DIR}/strong.sh 64
sbatch -N 128 -J strong128 -p normal ${DIR}/strong.sh 128
sbatch -N 256 -J strong256 -p normal ${DIR}/strong.sh 256
