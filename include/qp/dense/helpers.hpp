//
// Copyright (c) 2022, INRIA
//
/**
 * @file helpers.hpp 
*/

#ifndef PROXSUITE_QP_DENSE_HELPERS_HPP
#define PROXSUITE_QP_DENSE_HELPERS_HPP
#include <tl/optional.hpp>
#include <qp/results.hpp>
#include <qp/settings.hpp>
#include <qp/status.hpp>
#include <qp/dense/fwd.hpp>
#include <qp/dense/preconditioner/ruiz.hpp>
#include <chrono>

namespace proxsuite {
namespace qp {
namespace dense {

/////// SETUP ////////
/*!
 * Computes the equality constrained initial guess of a QP problem.
 *
 * @param qpwork workspace of the solver.
 * @param qpsettings settings of the solver.
 * @param qpmodel QP problem as defined by the user (without any scaling performed).
 * @param qpresults solution results.
 */
template <typename T>
void compute_equality_constrained_initial_guess(
        Workspace<T>& qpwork,
		const Settings<T>& qpsettings,
		const Model<T>& qpmodel,
		Results<T>& qpresults){
    
    qpwork.rhs.setZero();
    qpwork.rhs.head(qpmodel.dim) = -qpwork.g_scaled;
    qpwork.rhs.segment(qpmodel.dim, qpmodel.n_eq) = qpwork.b_scaled;
    iterative_solve_with_permut_fact( //
            qpsettings,
            qpmodel,
            qpresults,
            qpwork,
            T(1),
            qpmodel.dim + qpmodel.n_eq);

    qpresults.x = qpwork.dw_aug.head(qpmodel.dim);
    qpresults.y = qpwork.dw_aug.segment(qpmodel.dim, qpmodel.n_eq);
    qpwork.dw_aug.setZero();
    qpwork.rhs.setZero();
}

/*!
 * Setups and performs the first factorization of the regularized KKT matrix of the problem.  
 *
 * @param qpwork workspace of the solver.
 * @param qpmodel QP problem model as defined by the user (without any scaling performed).
 * @param qpresults solution results.
 */
template <typename T>
void setup_factorization(Workspace<T>& qpwork,
		const Model<T>& qpmodel,
		Results<T>& qpresults){

        veg::dynstack::DynStackMut stack{
			veg::from_slice_mut,
			qpwork.ldl_stack.as_mut(),
	    };

        qpwork.kkt.topLeftCorner(qpmodel.dim, qpmodel.dim) = qpwork.H_scaled;
        qpwork.kkt.topLeftCorner(qpmodel.dim, qpmodel.dim).diagonal().array() +=
                qpresults.info.rho;
        qpwork.kkt.block(0, qpmodel.dim, qpmodel.dim, qpmodel.n_eq) =
                qpwork.A_scaled.transpose();
        qpwork.kkt.block(qpmodel.dim, 0, qpmodel.n_eq, qpmodel.dim) = qpwork.A_scaled;
        qpwork.kkt.bottomRightCorner(qpmodel.n_eq, qpmodel.n_eq).setZero();
        qpwork.kkt.diagonal()
                .segment(qpmodel.dim, qpmodel.n_eq)
                .setConstant(-qpresults.info.mu_eq);

        qpwork.ldl.factorize(qpwork.kkt, stack);
}
/*!
 * Performs the equilibration of the QP problem for reducing its ill-conditionness.
 *
 * @param qpwork workspace of the solver.
 * @param qpsettings settings of the solver.
 * @param ruiz ruiz preconditioner.
 * @param execute_preconditioner boolean variable for executing or not the ruiz preconditioner. If set to False, it uses the previous preconditioning variables (initialized to the identity preconditioner if it is the first scaling performed).
 */
template <typename T>
void setup_equilibration(Workspace<T>& qpwork, 
						Settings<T>& qpsettings, 
						preconditioner::RuizEquilibration<T>& ruiz, 
						bool execute_preconditioner){

    QpViewBoxMut<T> qp_scaled{
			{from_eigen, qpwork.H_scaled},
			{from_eigen, qpwork.g_scaled},
			{from_eigen, qpwork.A_scaled},
			{from_eigen, qpwork.b_scaled},
			{from_eigen, qpwork.C_scaled},
			{from_eigen, qpwork.u_scaled},
			{from_eigen, qpwork.l_scaled}};

	veg::dynstack::DynStackMut stack{
			veg::from_slice_mut,
			qpwork.ldl_stack.as_mut(),
	};
	ruiz.scale_qp_in_place(qp_scaled, execute_preconditioner, qpsettings, stack);
	qpwork.correction_guess_rhs_g = infty_norm(qpwork.g_scaled);      
}

/*!
* Setups the solver initial guess.
*
* @param qpwork solver workspace.
* @param qpsettings solver settings.
* @param qpmodel QP problem model as defined by the user (without any scaling performed).
* @param qpresults solver results.
*/
template <typename T>
void initial_guess(
		Workspace<T>& qpwork,
		Settings<T>& qpsettings,
		Model<T>& qpmodel,
		Results<T>& qpresults) {

    switch (qpsettings.initial_guess) {
                case InitialGuessStatus::EQUALITY_CONSTRAINED_INITIAL_GUESS:{
                    compute_equality_constrained_initial_guess(qpwork,qpsettings,qpmodel,qpresults);
                    break;
                }
    }  

}
/*!
* Updates the QP solver model.
*
* @param H quadratic cost input defining the QP model.
* @param g linear cost input defining the QP model.
* @param A equality constraint matrix input defining the QP model.
* @param b equality constraint vector input defining the QP model.
* @param C inequality constraint matrix input defining the QP model.
* @param u lower inequality constraint vector input defining the QP model.
* @param l lower inequality constraint vector input defining the QP model.
* @param qpwork solver workspace.
* @param qpsettings solver settings.
* @param qpmodel solver model.
* @param qpresults solver result. 
*/

template <typename Mat,typename T>
void update(
			tl::optional<Mat> H_,
			tl::optional<VecRef<T>> g_,
			tl::optional<Mat> A_,
			tl::optional<VecRef<T>> b_,
			tl::optional<Mat> C_,
			tl::optional<VecRef<T>> u_,
			tl::optional<VecRef<T>> l_,
			Model<T>& model) {

		// update the model
		if (g_ != tl::nullopt) {
			model.g = g_.value().eval();
		} 
		if (b_ != tl::nullopt) {
			model.b = b_.value().eval();
		}
		if (u_ != tl::nullopt) {
			model.u = u_.value().eval();
		}
		if (l_ != tl::nullopt) {
			model.l = l_.value().eval();
		} 
		if (H_ != tl::nullopt) {
			if (A_ != tl::nullopt) {
				if (C_ != tl::nullopt) {
					model.H  = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic, to_eigen_layout(rowmajor)>(H_.value());
					model.A  = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic, to_eigen_layout(rowmajor)>(A_.value());
					model.C  = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic, to_eigen_layout(rowmajor)>(C_.value());
				} else {
					model.H  = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic, to_eigen_layout(rowmajor)>(H_.value());
					model.A  = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic, to_eigen_layout(rowmajor)>(A_.value());
				}
			} else if (C_ != tl::nullopt) {
				model.H  = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic, to_eigen_layout(rowmajor)>(H_.value());
				model.C  = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic, to_eigen_layout(rowmajor)>(C_.value());
			} else {
				model.H = H_.value().eval();
			}
		} else if (A_ != tl::nullopt) {
			if (C_ != tl::nullopt) {
				model.A  = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic, to_eigen_layout(rowmajor)>(A_.value());
				model.C  = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic, to_eigen_layout(rowmajor)>(C_.value());
			} else {
				model.A = A_.value().eval();
			}
		} else if (C_ != tl::nullopt) {
			model.C  = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic, to_eigen_layout(rowmajor)>(C_.value());
		}
}
/*!
* Setups the QP solver model.
*
* @param H quadratic cost input defining the QP model.
* @param g linear cost input defining the QP model.
* @param A equality constraint matrix input defining the QP model.
* @param b equality constraint vector input defining the QP model.
* @param C inequality constraint matrix input defining the QP model.
* @param u lower inequality constraint vector input defining the QP model.
* @param l lower inequality constraint vector input defining the QP model.
* @param qpwork solver workspace.
* @param qpsettings solver settings.
* @param qpmodel solver model.
* @param qpresults solver result.
* @param ruiz ruiz preconditioner.
* @param preconditioner_status bool variable for deciding whether executing the preconditioning algorithm, or keeping previous preconditioning variables, or using the identity preconditioner (i.e., no preconditioner).
*/
template <typename Mat, typename T>
void setup( //
		Mat const& H,
		VecRef<T> g,
		Mat const& A,
		VecRef<T> b,
		Mat const& C,
		VecRef<T> u,
		VecRef<T> l,
		Settings<T>& qpsettings,
		Model<T>& qpmodel,
		Workspace<T>& qpwork,
		Results<T>& qpresults,
		preconditioner::RuizEquilibration<T>& ruiz,
		PreconditionerStatus preconditioner_status) {

	switch (qpsettings.initial_guess) {
                case InitialGuessStatus::EQUALITY_CONSTRAINED_INITIAL_GUESS:{
					qpwork.cleanup();
					qpresults.cleanup(); 
                    break;
                }
                case InitialGuessStatus::COLD_START_WITH_PREVIOUS_RESULT:{
					// keep solutions but restart workspace and results
					qpwork.cleanup();
					qpresults.cold_start();
                    break;
                }
                case InitialGuessStatus::NO_INITIAL_GUESS:{
					qpwork.cleanup();
					qpresults.cleanup(); 
                    break;
                }
				case InitialGuessStatus::WARM_START:{
					qpwork.cleanup();
					qpresults.cleanup(); 
                    break;
                }
                case InitialGuessStatus::WARM_START_WITH_PREVIOUS_RESULT:{
					qpresults.cleanup_statistics();
                    break;
                }
	}
	qpmodel.H = Eigen::
			Matrix<T, Eigen::Dynamic, Eigen::Dynamic, to_eigen_layout(rowmajor)>(H);
	qpmodel.g = g;
	qpmodel.A = Eigen::
			Matrix<T, Eigen::Dynamic, Eigen::Dynamic, to_eigen_layout(rowmajor)>(A);
	qpmodel.b = b;
	qpmodel.C = Eigen::
			Matrix<T, Eigen::Dynamic, Eigen::Dynamic, to_eigen_layout(rowmajor)>(C);
	qpmodel.u = u;
	qpmodel.l = l;

	qpwork.H_scaled = qpmodel.H;
	qpwork.g_scaled = qpmodel.g;
	qpwork.A_scaled = qpmodel.A;
	qpwork.b_scaled = qpmodel.b;
	qpwork.C_scaled = qpmodel.C;
	qpwork.u_scaled = qpmodel.u;
	qpwork.l_scaled = qpmodel.l;

	qpwork.primal_feasibility_rhs_1_eq = infty_norm(qpmodel.b);
	qpwork.primal_feasibility_rhs_1_in_u = infty_norm(qpmodel.u);
	qpwork.primal_feasibility_rhs_1_in_l = infty_norm(qpmodel.l);
	qpwork.dual_feasibility_rhs_2 = infty_norm(qpmodel.g);

	switch (preconditioner_status)
	{
	case PreconditionerStatus::EXECUTE:
		setup_equilibration(qpwork, qpsettings, ruiz, true);
		break;
	case PreconditionerStatus::IDENTITY:
		setup_equilibration(qpwork, qpsettings, ruiz, false);
		break;
	case PreconditionerStatus::KEEP:
		// keep previous one
		setup_equilibration(qpwork, qpsettings, ruiz, false);
		break;
	}
	switch (qpsettings.initial_guess) {
                case InitialGuessStatus::EQUALITY_CONSTRAINED_INITIAL_GUESS:{
                    break;
                }
                case InitialGuessStatus::COLD_START_WITH_PREVIOUS_RESULT:{
					// keep solutions but restart workspace and results
					// unscale warm start from previous problem
					ruiz.scale_primal_in_place({proxsuite::qp::from_eigen,qpresults.x});
					ruiz.scale_dual_in_place_eq({proxsuite::qp::from_eigen,qpresults.y});
					ruiz.scale_dual_in_place_in({proxsuite::qp::from_eigen,qpresults.z});
                    break;
                }
                case InitialGuessStatus::NO_INITIAL_GUESS:{
                    break;
                }
				case InitialGuessStatus::WARM_START:{
                    break;
                }
                case InitialGuessStatus::WARM_START_WITH_PREVIOUS_RESULT:{
                    // keep workspace and results solutions except statistics
					ruiz.scale_primal_in_place({proxsuite::qp::from_eigen,qpresults.x});
					ruiz.scale_dual_in_place_eq({proxsuite::qp::from_eigen,qpresults.y});
					ruiz.scale_dual_in_place_in({proxsuite::qp::from_eigen,qpresults.z});
                    break;
                }
	}
}

////// UPDATES ///////

/*!
* Update the proximal parameters of the results object.
*
* @param rho_new primal proximal parameter.
* @param mu_eq_new dual equality proximal parameter.
* @param mu_in_new dual inequality proximal parameter.
* @param results solver results.
*/
template <typename T>
void update_proximal_parameters(
		Results<T>& results,
		tl::optional<T> rho_new,
		tl::optional<T> mu_eq_new,
		tl::optional<T> mu_in_new) {

	if (rho_new != tl::nullopt) {
		results.info.rho = rho_new.value();
	}
	if (mu_eq_new != tl::nullopt) {
		results.info.mu_eq = mu_eq_new.value();
		results.info.mu_eq_inv = T(1) / results.info.mu_eq;
	}
	if (mu_in_new != tl::nullopt) {
		results.info.mu_in = mu_in_new.value();
		results.info.mu_in_inv = T(1) / results.info.mu_in;
	}
}
/*!
* Warm start the primal and dual variables.
*
* @param x_wm primal warm start.
* @param y_wm dual equality warm start.
* @param z_wm dual inequality warm start.
* @param results solver result.
* @param settings solver settings. 
*/
template <typename T>
void warm_start(
		tl::optional<VecRef<T>> x_wm,
		tl::optional<VecRef<T>> y_wm,
		tl::optional<VecRef<T>> z_wm,
		Results<T>& results,
		Settings<T>& settings) {

	isize n_eq = results.y.rows();
	isize n_in = results.z.rows();
	if (n_eq!=0){
		if (n_in!=0){
			if(x_wm != tl::nullopt && y_wm != tl::nullopt && z_wm != tl::nullopt){
					results.x = x_wm.value().eval();
					results.y = y_wm.value().eval();
					results.z = z_wm.value().eval();
			}
		}else{
			// n_in= 0
			if(x_wm != tl::nullopt && y_wm != tl::nullopt){
					results.x = x_wm.value().eval();
					results.y = y_wm.value().eval();
			}
		}
	}else if (n_in !=0){
		// n_eq = 0
		if(x_wm != tl::nullopt && z_wm != tl::nullopt){
					results.x = x_wm.value().eval();
					results.z = z_wm.value().eval();
		}
	} else {
		// n_eq = 0 and n_in = 0
		if(x_wm != tl::nullopt ){
					results.x = x_wm.value().eval();
		}
	}	

	settings.initial_guess = InitialGuessStatus::WARM_START;

}
} // namespace dense
} // namespace qp
} // namespace proxsuite

#endif /* end of include guard PROXSUITE_QP_DENSE_HELPERS_HPP */
