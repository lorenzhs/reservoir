#!/bin/bash

set -e

echo "Usage: ./run.sh [args for res]"

DIR=${SLURM_SUBMIT_DIR:-.}

echo "Invocation: $0 $*"
echo "Running on $(hostname) on $(date) from ${DIR}"

mpirun --bind-to core --map-by core -report-bindings ${DIR}/benchmark/res $@
