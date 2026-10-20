# Nexus NumPy (`import numpy`) — Complete API Reference

This document provides a comprehensive catalog of all public functions available in the Nexus NumPy standard library.

---

## 1. Core Module (`stdlib/numpy/core.nex`)

### Lifecycle & Error Handling
- `np_ensure_init()`: Initializes global buffer pools and runtime heap for NumPy operations.
- `np_set_error(code, msg: string)`: Records error code and diagnostic error message string.
- `np_get_error()`: Returns current numeric error code (0 = success).
- `np_get_error_message() -> string`: Returns formatted error description.
- `np_clear_error()`: Resets error status to 0.

### Telemetry & Memory
- `np_total_allocated_bytes()`: Returns total heap memory currently allocated by NdArray buffers.
- `np_total_arrays_created()`: Returns monotonically increasing count of created arrays.

### Constants
- `np_pi() -> float`: Returns `3.141592653589793`.
- `np_e() -> float`: Returns `2.718281828459045`.
- `np_inf() -> float`: Returns positive infinity representation.
- `np_nan() -> float`: Returns NaN floating point representation.

### Strides & Views
- `np_shape(arr, dim)`: Returns extent along dimension `dim` (0 = rows, 1 = cols).
- `np_ndim(arr)`: Returns rank of array (1 or 2).
- `np_size(arr)`: Returns total element count (`shape[0] * shape[1]`).
- `np_is_view(arr)`: Returns 1 if array is a strided view, 0 if owner of contiguous buffer.
- `np_reshape(arr, new_rows, new_cols)`: Returns reshaped view or copy conforming to new dimensions.
- `np_transpose(arr)`: Returns transposed view swapping row and column strides.
- `np_flatten(arr)`: Returns 1D contiguous vector copy of array elements.
- `np_slice(arr, start, stop, step)`: Returns strided sub-array slice.

---

## 2. Creation Module (`stdlib/numpy/creation.nex`)

- `numpy.array(buffer_or_literal)`: Converts 1D or 2D array literal into NdArray.
- `numpy.zeros(n)`: Allocates 1D vector of length `n` initialized to `0.0`.
- `numpy.zeros_2d(rows, cols)`: Allocates `(rows, cols)` matrix initialized to `0.0`.
- `numpy.ones(n)`: Allocates 1D vector of length `n` initialized to `1.0`.
- `numpy.ones_2d(rows, cols)`: Allocates `(rows, cols)` matrix initialized to `1.0`.
- `numpy.full(n, val: float)`: Allocates 1D vector filled with constant `val`.
- `numpy.full_2d(rows, cols, val: float)`: Allocates `(rows, cols)` matrix filled with constant `val`.
- `numpy.arange(start: float, stop: float, step: float)`: Generates half-open range `[start, stop)` with stride `step`.
- `numpy.linspace(start: float, stop: float, num)`: Generates `num` linearly spaced samples over `[start, stop]`.
- `numpy.logspace(start: float, stop: float, num)`: Generates `num` logarithmically spaced samples (`10^start` to `10^stop`).
- `numpy.eye(n)`: Generates `(n, n)` identity matrix with ones on main diagonal.
- `numpy.diag(v)`: Constructs 2D diagonal matrix from 1D vector, or extracts main diagonal from 2D matrix.

---

## 3. Element-Wise Operations (`stdlib/numpy/ops.nex`)

All operations support scalar-array and array-array broadcasting.
- `numpy.add(a, b)`: Element-wise addition (`a + b`).
- `numpy.sub(a, b)` / `numpy.subtract(a, b)`: Element-wise subtraction (`a - b`).
- `numpy.mul(a, b)` / `numpy.multiply(a, b)`: Element-wise multiplication (`a * b`).
- `numpy.div(a, b)` / `numpy.divide(a, b)`: Element-wise division (`a / b`).
- `numpy.mod(a, b)`: Element-wise floating-point modulo (`a % b`).
- `numpy.equal(a, b)`: Element-wise boolean equality (`1.0` if equal, else `0.0`).
- `numpy.not_equal(a, b)`: Element-wise boolean inequality.
- `numpy.less(a, b)`: Element-wise strict less-than.
- `numpy.less_equal(a, b)`: Element-wise less-than or equal.
- `numpy.greater(a, b)`: Element-wise strict greater-than.
- `numpy.greater_equal(a, b)`: Element-wise greater-than or equal.
- `numpy.where(condition, x, y)`: Selects elements from `x` or `y` based on `condition`.

---

## 4. Reductions (`stdlib/numpy/reduce.nex`)

All sum and mean calculations use Neumaier compensated summation for numerical precision.
- `numpy.sum(arr) -> float`: Computes total sum across all elements.
- `numpy.sum_axis(arr, axis)`: Computes directional sum (0 = along columns, 1 = along rows).
- `numpy.prod(arr) -> float`: Computes product of all elements.
- `numpy.min(arr) -> float`: Finds minimum element value.
- `numpy.max(arr) -> float`: Finds maximum element value.
- `numpy.argmin(arr)`: Finds flat index of minimum element.
- `numpy.argmax(arr)`: Finds flat index of maximum element.
- `numpy.ptp(arr) -> float`: Computes peak-to-peak range (`max - min`).
- `numpy.cumsum(arr)`: Computes cumulative summation vector.
- `numpy.cumprod(arr)`: Computes cumulative product vector.
- `numpy.any(arr)`: Returns 1 if any element is non-zero, else 0.
- `numpy.all(arr)`: Returns 1 if all elements are non-zero, else 0.

---

## 5. Mathematical Universal Functions (`stdlib/numpy/math.nex`)

- `numpy.sin(arr)` / `numpy.cos(arr)` / `numpy.tan(arr)`: Trigonometric functions.
- `numpy.asin(arr)` / `numpy.acos(arr)` / `numpy.atan(arr)`: Inverse trigonometric functions.
- `numpy.atan2(y, x)`: Four-quadrant arc tangent.
- `numpy.exp(arr)`: Natural exponential (`e^x`).
- `numpy.log(arr)`: Natural logarithm (`ln x`).
- `numpy.log10(arr)`: Base-10 logarithm.
- `numpy.log2(arr)`: Base-2 logarithm.
- `numpy.sqrt(arr)`: Square root (`√x`).
- `numpy.square(arr)`: Square of elements (`x^2`).
- `numpy.power(arr, p: float)`: Arbitrary exponentiation (`x^p`).
- `numpy.abs(arr)`: Absolute value (`|x|`).
- `numpy.sign(arr)`: Element-wise signum (`-1.0`, `0.0`, `1.0`).
- `numpy.clip(arr, a_min: float, a_max: float)`: Restricts values to `[a_min, a_max]`.
- `numpy.floor(arr)` / `numpy.ceil(arr)` / `numpy.round(arr)`: Rounding functions.

---

## 6. Linear Algebra (`stdlib/numpy/linalg.nex`)

- `numpy.dot(a, b) -> float`: Inner product of two 1D vectors.
- `numpy.outer(a, b)`: Outer product of two vectors yielding a 2D matrix.
- `numpy.matmul(a, b)`: 2D Matrix multiplication (`A @ B`).
- `numpy.trace(mat) -> float`: Sum of diagonal elements.
- `numpy.norm(arr) -> float`: L2 Euclidean norm of vector or Frobenius norm of matrix.
- `numpy.det(mat) -> float`: Determinant of square matrix using LU factorization.
- `numpy.inv(mat)`: Multiplicative inverse of non-singular square matrix.
- `numpy.solve(A, b)`: Solves linear equation system `A * x = b` via LU decomposition with partial pivoting.
- `numpy.lu(A)`: Computes LU decomposition with row pivoting (`P * A = L * U`).
- `numpy.qr(A)`: Computes QR factorization using Householder reflection matrices.
- `numpy.cholesky(A)`: Computes Cholesky factor `L` for symmetric positive-definite matrix `A = L * L^T`.
- `numpy.svd(A)`: Computes Singular Value Decomposition (`U`, `S`, `Vt`).
- `numpy.eigvals(A)`: Computes eigenvalues of symmetric square matrix via Jacobi rotations.
- `numpy.lstsq(X, y)`: Computes least-squares solution to overdetermined linear system `X * beta ≈ y`.

---

## 7. Statistics (`stdlib/numpy/stats.nex`)

- `numpy.mean(arr) -> float`: Arithmetic mean of elements.
- `numpy.var(arr) -> float`: Population variance (`ddof = 0`).
- `numpy.sample_var(arr) -> float`: Sample variance (`ddof = 1`).
- `numpy.std(arr) -> float`: Population standard deviation.
- `numpy.sample_std(arr) -> float`: Sample standard deviation.
- `numpy.median(arr) -> float`: Median (50th percentile) of sorted elements.
- `numpy.quantile(arr, q: float) -> float`: Quantile for `q ∈ [0.0, 1.0]`.
- `numpy.percentile(arr, p: float) -> float`: Percentile for `p ∈ [0.0, 100.0]`.
- `numpy.cov(X)`: Covariance matrix across data dimensions.
- `numpy.corrcoef(X)`: Pearson product-moment correlation coefficient matrix.

---

## 8. Random Sampling (`stdlib/numpy/random.nex`)

- `numpy.random_seed(seed)`: Seeds global 64-bit pseudo-random generator.
- `numpy.random_uniform(low: float, high: float, n)`: Vector of `n` uniform floats in `[low, high)`.
- `numpy.random_normal(mean: float, std: float, n)`: Vector of `n` Gaussian distributed floats via Box-Muller transform.
- `numpy.random_integers(low, high, n)`: Vector of `n` discrete random integers in `[low, high]`.
- `numpy.shuffle(arr)`: In-place Fisher-Yates array permutation.
- `numpy.permutation(arr)`: Returns permuted copy of input array.

---

## 9. Sorting & Searching (`stdlib/numpy/sort.nex`)

- `numpy.sort(arr)`: Returns sorted copy of array (ascending order).
- `numpy.sort_inplace(arr)`: In-place merge-sort.
- `numpy.argsort(arr)`: Returns index array of sorting permutation.
- `numpy.searchsorted(arr, val: float)`: Finds insertion index via binary search.
- `numpy.unique(arr)`: Returns sorted array with duplicate values removed.

---

## 10. Serialization & I/O (`stdlib/numpy/io.nex`)

- `numpy.save_binary(path: string, arr)`: Serializes array into binary file with magic header and checksum.
- `numpy.load_binary(path: string)`: Deserializes array from binary file.
- `numpy.save_csv(path: string, arr)`: Exports matrix to standard CSV text format.
- `numpy.load_csv(path: string)`: Parses matrix from standard CSV text file.
- `numpy.to_csv_string(arr) -> string`: Converts matrix to in-memory CSV string.
- `numpy.from_csv_string(csv_str: string)`: Reconstructs matrix from in-memory CSV string.
- `numpy.format(arr) -> string`: Formats array into human-readable string representation.
