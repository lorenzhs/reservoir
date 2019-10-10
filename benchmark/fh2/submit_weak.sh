#!/bin/bash

DIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )

sbatch -N 1 -J weak1 -p normal ${DIR}/weak.sh
sbatch -N 2 -J weak2 -p normal ${DIR}/weak.sh
sbatch -N 4 -J weak4 -p normal ${DIR}/weak.sh
sbatch -N 8 -J weak8 -p normal ${DIR}/weak.sh
sbatch -N 16 -J weak16 -p normal ${DIR}/weak.sh
sbatch -N 32 -J weak32 -p normal ${DIR}/weak.sh
sbatch -N 64 -J weak64 -p normal ${DIR}/weak.sh
sbatch -N 128 -J weak128 -p normal ${DIR}/weak.sh
sbatch -N 256 -J weak256 -p normal ${DIR}/weak.sh
