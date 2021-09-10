#ifndef INRIA_LDLT_UTIL_HPP_ZX9HNY5GS
#define INRIA_LDLT_UTIL_HPP_ZX9HNY5GS

#include <Eigen/Core>
#include <Eigen/SparseCore>
#include <Eigen/Cholesky>
#include <Eigen/QR>
#include <utility>
#include <ldlt/views.hpp>
#include <qp/views.hpp>
#include <ldlt/detail/meta.hpp>

using c_int = long long;
using c_float = double;

template <typename T, ldlt::Layout L>
using Mat = Eigen::Matrix<
		T,
		Eigen::Dynamic,
		Eigen::Dynamic,
		(L == ldlt::colmajor) ? Eigen::ColMajor : Eigen::RowMajor>;
template <typename T>
using Vec = Eigen::Matrix<T, Eigen::Dynamic, 1>;

template <typename Scalar>
using SparseMat = Eigen::SparseMatrix<Scalar, Eigen::ColMajor, c_int>;

namespace ldlt_test {
using namespace ldlt;
namespace rand {

using std::uint64_t;

using uint128_t = __uint128_t;
uint128_t g_lehmer64_state =
		uint128_t(0xda942042e4dd58b5) * uint128_t(0xda942042e4dd58b5);

auto lehmer64() -> uint64_t { // [0, 2^64)
	g_lehmer64_state *= 0xda942042e4dd58b5;
	return uint64_t(g_lehmer64_state >> uint128_t(64U));
}

void set_seed(uint64_t seed) {
	g_lehmer64_state = seed + 1;
	lehmer64();
	lehmer64();
}

auto uniform_rand() -> double { // [0, 2^53]
	uint64_t a = lehmer64() / (1U << 11U);
	return double(a) / double(uint64_t(1) << 53U);
}
auto normal_rand() -> double {
	static const double pi2 = std::atan(static_cast<double>(1)) * 8;

	double u1 = uniform_rand();
	double u2 = uniform_rand();

	double ln = std::log(u1);
	double sqrt = std::sqrt(-2 * ln);

	return sqrt * std::cos(pi2 * u2);
}

template <typename Scalar>
auto vector_rand(isize nrows) -> Vec<Scalar> {
	auto v = Vec<Scalar>(nrows);

	for (isize i = 0; i < nrows; ++i) {
		v(i) = Scalar(rand::normal_rand());
	}

	return v;
}
template <typename Scalar>
auto matrix_rand(isize nrows, isize ncols) -> Mat<Scalar, colmajor> {
	auto v = Mat<Scalar, colmajor>(nrows, ncols);

	for (isize i = 0; i < nrows; ++i) {
		for (isize j = 0; j < ncols; ++j) {
			v(i, j) = Scalar(rand::normal_rand());
		}
	}

	return v;
}

template <typename Scalar>
auto positive_definite_rand(isize n, Scalar cond) -> Mat<Scalar, colmajor> {
	auto out = rand::matrix_rand<Scalar>(n, n);
	auto qr = out.householderQr();
	Mat<Scalar, colmajor> q = qr.householderQ();
	auto d = Vec<Scalar>(n);

	{
		using std::exp;
		using std::log;
		Scalar diff = log(cond);
		for (isize i = 0; i < n; ++i) {
			d(i) = exp(Scalar(i) / Scalar(n) * diff);
		}
	}

	return q * d.asDiagonal() * q.transpose();
}

template <typename Scalar>
auto sparse_positive_definite_rand(isize n, Scalar cond, double p)
		-> SparseMat<Scalar> {
	auto H = SparseMat<Scalar>(n, n);

	for (isize i = 0; i < n; ++i) {
		auto urandom = rand::uniform_rand();
		if (urandom < p) {
			auto random = Scalar(rand::normal_rand());
			H.insert(i, i) = random;
		}
	}

	for (isize i = 0; i < n; ++i) {
		for (isize j = i + 1; j < n; ++j) {
			auto urandom = rand::uniform_rand();
			if (urandom < p / 2) {
				auto random = Scalar(rand::normal_rand());
				H.insert(i, j) = random;
			}
		}
	}

	Mat<Scalar, colmajor> H_dense = H.toDense();
	Vec<Scalar> eigh =
			H_dense.template selfadjointView<Eigen::Upper>().eigenvalues();

	Scalar min = eigh.minCoeff();
	Scalar max = eigh.maxCoeff();

	// new_min = min + rho
	// new_max = max + rho
	//
	// (max + rho)/(min + rho) = cond
	// 1 + (max - min) / (min + rho) = cond
	// (max - min) / (min + rho) = cond - 1
	// min + rho = (max - min) / (cond - 1)
	// rho = (max - min)/(cond - 1) - min
	Scalar rho = (max - min) / (cond - 1) - min;

	for (isize i = 0; i < n; ++i) {
		H.coeffRef(i, i) += rho;
	}
	H.makeCompressed();
	return H;
}

template <typename Scalar>
auto sparse_matrix_rand(isize nrows, isize ncols, double p)
		-> SparseMat<Scalar> {
	auto A = SparseMat<Scalar>(nrows, ncols);

	for (isize i = 0; i < nrows; ++i) {
		for (isize j = 0; j < ncols; ++j) {
			if (rand::uniform_rand() < p) {
				A.insert(i, j) = Scalar(rand::normal_rand());
			}
		}
	}
	A.makeCompressed();
	return A;
}
} // namespace rand
using ldlt::detail::Duration;
using ldlt::detail::Clock;
using ldlt::usize;

template <typename T>
struct BenchResult {
	Duration duration;
	T result;
};

template <typename Fn>
auto bench_for_n(usize n, Fn fn) -> BenchResult<decltype(fn())> {
	auto begin = Clock::now();

	auto result = fn();

	for (usize i = 1; i < n; ++i) {
		result = fn();
	}

	auto end = Clock::now();
	return {
			(end - begin) / n,
			LDLT_FWD(result),
	};
}

template <typename Fn>
auto bench_for(Duration d, Fn fn) -> BenchResult<decltype(fn())> {
	namespace time = std::chrono;
	using DurationF64 = time::duration<double, std::ratio<1>>;

	auto elapsed = Duration(0);
	usize n_runs = 1;

	while (true) {
		auto res = ldlt_test::bench_for_n(n_runs, fn);
		elapsed = res.duration;
		if (elapsed > d) {
			return res;
		}

		if ((elapsed * n_runs) > time::microseconds{100}) {
			break;
		}
		n_runs *= 2;
	}

	auto d_f64 = time::duration_cast<DurationF64>(d);
	auto elapsed_f64 = time::duration_cast<DurationF64>(elapsed);
	double ratio = (d_f64 / elapsed_f64);
	return ldlt_test::bench_for_n(usize(std::ceil(ratio)), fn);
}

namespace osqp {
inline auto to_sparse(Mat<c_float, colmajor>& mat) -> SparseMat<c_float> {
	SparseMat<c_float> out(mat.rows(), mat.cols());

	using Eigen::Index;
	for (Index j = 0; j < mat.cols(); ++j) {
		for (Index i = 0; i < mat.rows(); ++i) {
			if (mat(i, j) != 0) {
				out.insert(i, j) = mat(i, j);
			}
		}
	}
	out.makeCompressed();
	return out;
}

inline auto to_sparse_sym(Mat<c_float, colmajor>& mat) -> SparseMat<c_float> {
	SparseMat<c_float> out(mat.rows(), mat.cols());
	using Eigen::Index;
	for (Index j = 0; j < mat.cols(); ++j) {
		for (Index i = 0; i < j + 1; ++i) {
			if (mat(i, j) != 0) {
				out.insert(i, j) = mat(i, j);
			}
		}
	}
	out.makeCompressed();
	return out;
}
} // namespace osqp
} // namespace ldlt_test

template <
		typename MatLhs,
		typename MatRhs,
		typename T = typename MatLhs::Scalar>
auto matmul(MatLhs const& a, MatRhs const& b) -> Mat<T, ldlt::colmajor> {
	using Upscaled = typename std::
			conditional<std::is_floating_point<T>::value, long double, T>::type;

	return (Mat<T, ldlt::colmajor>(a).template cast<Upscaled>().operator*(
							Mat<T, ldlt::colmajor>(b).template cast<Upscaled>()))
	    .template cast<T>();
}

template <
		typename MatLhs,
		typename MatMid,
		typename MatRhs,
		typename T = typename MatLhs::Scalar>
auto matmul3(MatLhs const& a, MatMid const& b, MatRhs const& c)
		-> Mat<T, ldlt::colmajor> {
	return ::matmul(::matmul(a, b), c);
}

namespace ldlt {
namespace detail {
template <typename... Ts>
using type_sequence = meta_::type_sequence<Ts...>*;

template <usize I, typename T>
struct HollowLeaf {};
template <typename ISeq, typename... Ts>
struct HollowIndexedTuple;
template <usize... Is, typename... Ts>
struct HollowIndexedTuple<index_sequence<Is...>, Ts...>
		: HollowLeaf<Is, Ts>... {};

template <usize I, typename T>
auto get_type(HollowLeaf<I, T> const*) noexcept -> T;

template <usize I>
struct pack_ith_elem {
	template <typename... Ts>
	using Type = decltype(detail::get_type<I>(
			static_cast<
					HollowIndexedTuple<make_index_sequence<sizeof...(Ts)>, Ts...>*>(
					nullptr)));
};

#if LDLT_HAS_BUILTIN(__type_pack_element)
template <usize I, typename... Ts>
using ith = __type_pack_element<I, Ts...>;
#else
template <usize I, typename... Ts>
using ith = typename pack_ith_elem<I>::template Type<Ts...>;
#endif

template <typename T>
struct type_list_ith_elem_impl;

template <usize I, typename List>
using typeseq_ith =
		typename type_list_ith_elem_impl<List>::template ith_elem<I>;

template <typename... Ts>
struct type_list_ith_elem_impl<type_sequence<Ts...>> {
	template <usize I>
	using ith_elem = ith<I, Ts...>;
};
template <typename T, T Val>
struct constant {
	static constexpr T value = Val;
};
} // namespace detail
} // namespace ldlt

LDLT_DEFINE_TAG(random_with_dim_and_n_eq, RandomWithDimAndNeq);
LDLT_DEFINE_TAG(from_data, FromData);
template <typename Scalar>
struct Qp {
	Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic, Eigen::ColMajor> H;
	Eigen::Matrix<Scalar, Eigen::Dynamic, 1, Eigen::ColMajor> g;
	Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic, Eigen::ColMajor> A;
	Eigen::Matrix<Scalar, Eigen::Dynamic, 1, Eigen::ColMajor> b;

	Eigen::Matrix<Scalar, Eigen::Dynamic, 1> solution;

	Qp(FromData /*tag*/,
	   Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic, Eigen::ColMajor> H_,
	   Eigen::Matrix<Scalar, Eigen::Dynamic, 1, Eigen::ColMajor> g_,
	   Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic, Eigen::ColMajor> A_,
	   Eigen::Matrix<Scalar, Eigen::Dynamic, 1, Eigen::ColMajor> b_) noexcept
			: H(LDLT_FWD(H_)),
				g(LDLT_FWD(g_)),
				A(LDLT_FWD(A_)),
				b(LDLT_FWD(b_)),
				solution(H.rows() + A.rows()) {

		ldlt::isize dim = ldlt::isize(H.rows());
		ldlt::isize n_eq = ldlt::isize(A.rows());

		auto kkt_mat =
				Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic, Eigen::ColMajor>(
						dim + n_eq, dim + n_eq);

		kkt_mat.topLeftCorner(dim, dim) = H;
		kkt_mat.topRightCorner(dim, n_eq) = A.transpose();
		kkt_mat.bottomLeftCorner(n_eq, dim) = A;
		kkt_mat.bottomRightCorner(n_eq, n_eq).setZero();

		solution.topRows(dim) = -g;
		solution.bottomRows(n_eq) = b;
		kkt_mat.ldlt().solveInPlace(solution);
	}

	Qp(RandomWithDimAndNeq /*tag*/, ldlt::isize dim, ldlt::isize n_eq)
			: H(ldlt_test::rand::positive_definite_rand<Scalar>(dim, Scalar(1e2))),
				g(dim),
				A(ldlt_test::rand::matrix_rand<Scalar>(n_eq, dim)),
				b(n_eq),
				solution(ldlt_test::rand::vector_rand<Scalar>(dim + n_eq)) {

		// 1/2 (x-sol)T H (x-sol)
		// 1/2 xT H x - (H sol).T x
		auto primal_solution = solution.topRows(dim);
		auto dual_solution = solution.bottomRows(n_eq);

		g = -H * primal_solution - A.transpose() * dual_solution;
		b = A * primal_solution;
	}

	auto as_view() -> qp::QpView<Scalar> {
		return {
				{ldlt::from_eigen, H},
				{ldlt::from_eigen, g},
				{ldlt::from_eigen, A},
				{ldlt::from_eigen, b},
				{ldlt::from_ptr_rows_cols_stride, nullptr, 0, ldlt::isize(H.rows()), 0},
				{ldlt::from_ptr_size, nullptr, 0},
		};
	}
	auto as_mut() -> qp::QpViewMut<Scalar> {
		return {
				{ldlt::from_eigen, H},
				{ldlt::from_eigen, g},
				{ldlt::from_eigen, A},
				{ldlt::from_eigen, b},
				{ldlt::from_ptr_rows_cols_stride, nullptr, 0, ldlt::isize(H.rows()), 0},
				{ldlt::from_ptr_size, nullptr, 0},
		};
	}
};

struct EigenNoAlloc {
	EigenNoAlloc(EigenNoAlloc&&) = delete;
	EigenNoAlloc(EigenNoAlloc const&) = delete;
	auto operator=(EigenNoAlloc&&) -> EigenNoAlloc& = delete;
	auto operator=(EigenNoAlloc const&) -> EigenNoAlloc& = delete;

#if defined(EIGEN_RUNTIME_NO_MALLOC)
	EigenNoAlloc() noexcept { Eigen::internal::set_is_malloc_allowed(false); }
	~EigenNoAlloc() noexcept { Eigen::internal::set_is_malloc_allowed(true); }
#else
	EigenNoAlloc() = default;
#endif
};

#endif /* end of include guard INRIA_LDLT_UTIL_HPP_ZX9HNY5GS */
