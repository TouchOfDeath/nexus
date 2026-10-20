# Nexus NumPy (`import numpy`) — Architecture & Developer Manual

## 1. Overview & Architecture

The Nexus NumPy library is a native numerical computing engine written entirely in pure Nexus source code (`stdlib/numpy/*.nex`) with **0% libc, 0% BLAS/LAPACK, and 0% external runtime dependencies**. It compiles directly to bare-metal x86-64 machine code via the self-hosted Nexus compiler core and native Linux ELF64 emitter.

Like Python's NumPy, Nexus NumPy provides:
1. **Multidimensional `NdArray` Structure**: Arbitrary 1D vectors and 2D matrices backed by contiguous linear memory or strided views.
2. **Strided Views & Zero-Copy Slicing**: Slices, transposes, and reshapes operate via pointer offsets and stride modifications without copying underlying data buffers whenever possible.
3. **Automatic N-D Broadcasting**: Standard NumPy broadcasting rules for binary arithmetic operations between arrays of compatible shapes (e.g. adding a `(1, M)` row vector to an `(N, M)` matrix).
4. **Universal Functions (ufuncs)**: Vectorized mathematical functions with full IEEE-754 floating-point edge-case handling (`NaN`, `Inf`, `-Inf`, `-0.0`).
5. **Linear Algebra (`numpy.linalg`)**: Pure Nexus implementations of matrix multiplication, determinants, matrix inversion, LU decomposition with partial pivoting, QR decomposition with Householder reflections, Cholesky decomposition, Singular Value Decomposition (SVD), symmetric eigenvalue solver, and least-squares regression.
6. **Descriptive & Inferential Statistics**: Compensated summation (Neumaier / Kahan algorithm), variance, standard deviation, quantiles/percentiles, covariance, and Pearson correlation matrices.
7. **Reproducible PRNG**: Deterministic pseudo-random number generator supporting uniform, Gaussian/normal (Box-Muller), integer ranges, and shuffling.
8. **Sorting & Searching**: Merge-sort with index tracking (`argsort`), binary search (`searchsorted`), and deduplication (`unique`).
9. **Serialization & I/O**: High-speed binary format (`.npy` with magic header and checksums) and RFC-compliant CSV reader/writer.

---

## 2. Memory Model & Data Representation

Every `NdArray` in Nexus NumPy is represented by a compact header struct allocated in the runtime heap:

```text
+-------------------------------------------------------------------+
|                            NdArray Header                         |
+-------------+-------------+-------------+-------------+-----------+
| data_ptr    | offset      | ndim        | size        | dtype     |
| (64-bit)    | (bytes)     | (1 or 2)    | (elements)  | (F64/I64) |
+-------------+-------------+-------------+-------------+-----------+
| shape[0]    | shape[1]    | stride[0]   | stride[1]   | is_view   |
| (rows)      | (cols)      | (bytes)     | (bytes)     | (0 or 1)  |
+-------------+-------------+-------------+-------------+-----------+
```

### Strata & Stride Arithmetic
For a 2D matrix with shape `(R, C)` in row-major (C-contiguous) format:
- `stride[0] = C * element_size` (typically 8 bytes for 64-bit floats)
- `stride[1] = element_size` (8 bytes)
- Element address: `data_ptr + offset + (i * stride[0]) + (j * stride[1])`

### Zero-Copy Transpose & Views
Transposing a 2D matrix simply swaps `shape[0] <-> shape[1]` and `stride[0] <-> stride[1]`, marking the resulting `NdArray` as a non-contiguous view (`is_view = 1`) without allocating or copying a single byte of payload.

---

## 3. Subsystem Breakdown

| Module | File | Lines | Primary Responsibilities |
| :--- | :--- | :--- | :--- |
| **Core** | `stdlib/numpy/core.nex` | 1,780 | NdArray allocation, shape descriptors, views, strided indexing, multi-dim indexing parser, error diagnostics. |
| **Creation** | `stdlib/numpy/creation.nex` | 743 | Constructors: `zeros`, `ones`, `full`, `arange`, `linspace`, `logspace`, `eye`, `diag`, `from_buffer`. |
| **Ops** | `stdlib/numpy/ops.nex` | 797 | Vectorized arithmetic (`+`, `-`, `*`, `/`, `%`), comparison (`==`, `!=`, `<`, `<=`, `>`, `>=`), and logical broadcasting. |
| **Reduce** | `stdlib/numpy/reduce.nex` | 469 | Compensated Neumaier reductions: `sum`, `prod`, `min`, `max`, `mean`, `ptp`, `argmin`, `argmax`, `cumsum`, `cumprod`. |
| **Math** | `stdlib/numpy/math.nex` | 640 | Element-wise ufuncs: `sin`, `cos`, `tan`, `exp`, `log`, `sqrt`, `power`, `clip`, `sign`, `round`, `floor`, `ceil`. |
| **Linear Algebra** | `stdlib/numpy/linalg.nex` | 1,174 | Matrix ops: `dot`, `matmul`, `solve`, `inv`, `det`, `lu`, `qr`, `cholesky`, `svd`, `eig`, `lstsq`, norms. |
| **Statistics** | `stdlib/numpy/stats.nex` | 445 | `var`, `std`, `median`, `quantile`, `percentile`, `cov`, `corrcoef`, histogram bins. |
| **Random** | `stdlib/numpy/random.nex` | 346 | PRNG state: `random_seed`, `random_uniform`, `random_normal`, `random_integers`, `shuffle`, `permutation`. |
| **Sort** | `stdlib/numpy/sort.nex` | 391 | In-place & out-of-place `sort`, `argsort`, `searchsorted_left/right`, `unique`. |
| **I/O** | `stdlib/numpy/io.nex` | 565 | Binary export/import (`.npy`), CSV parser and serializer (`to_csv_string`, `from_csv_string`, `save_csv`, `load_csv`). |

---

## 4. Usage Patterns & Quickstart

```nexus
import numpy

# 1. Array Creation
let a = numpy.array([1.0, 2.0, 3.0, 4.0, 5.0])
let grid = numpy.zeros_2d(4, 4)
let ramp = numpy.linspace(0.0, 1.0, 100)

# 2. 2D Matrix Literals & Indexing
let M = numpy.array([[1.0, 2.0], [3.0, 4.0]])
let top_right = M[0, 1]
M[1, 1] = 99.0

# 3. Linear Algebra
let A = numpy.array([[3.0, 1.0, -1.0], [2.0, 4.0, 1.0], [-1.0, 2.0, 5.0]])
let b = numpy.array([4.0, 1.0, 1.0])
let x = numpy.solve(A, b)
let A_inv = numpy.inv(A)

# 4. Statistics & Reductions
let mu = numpy.mean(a)
let sigma = numpy.std(a)
let total = numpy.sum(a)

# 5. CSV Serialization
numpy.save_csv("data.csv", M)
let loaded = numpy.load_csv("data.csv")
```
