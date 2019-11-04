# Communication-Efficient Weighted Reservoir Sampling

This is the code to accompany the paper "Communication-Efficient (Weighted) Reservoir Sampling" by Peter Sanders and me. A [preprint](https://arxiv.org/abs/1910.11069) is available online.

## License

Some of the components used herein have specific licenses:

- The B+ tree (`reservoir/btree*.hpp`) is licensed under the Boost Software License.
- The dSFMT generator (`reservoir/generators/dSFMT_internal.{c,h}pp`) is licensed under the 3-clause BSD license.
- The error handling code for the MKL generator (`reservoir/generators/mkl.cpp`) is owned by Intel Corporation.

The rest is licensed under the GNU General Public License (GPL) v3.
