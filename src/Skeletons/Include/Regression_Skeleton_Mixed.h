#ifndef __REGRESSION_SKELETON_MIXED_H__
#define __REGRESSION_SKELETON_MIXED_H__

#include "../../FdaPDE.h"
#include "../../Lambda_Optimization/Include/Grid_Evaluator.h"
#include "../../Lambda_Optimization/Include/Lambda_Optimizer.h"
#include "../../Lambda_Optimization/Include/Newton.h"
#include "../../Lambda_Optimization/Include/Optimization_Data.h"
#include "../../Lambda_Optimization/Include/Optimization_Methods_Factory.h"
#include "../../Lambda_Optimization/Include/Solution_Builders.h"
#include "../../Mesh/Include/Mesh.h"
#include "../../Regression/Include/Mixed_FE_Regression.h"

template<typename InputHandler, UInt ORDER, UInt mydim, UInt ndim>
SEXP regression_skeleton_mixed(InputHandler &regressionData, OptimizationData optimizationData, SEXP Rmesh)
{
	MeshHandler<ORDER, mydim, ndim> mesh(Rmesh, regressionData.getSearch());
	MixedFERegression<InputHandler> regression(regressionData, optimizationData, mesh.num_nodes());
	Rprintf("in preapply");
	regression.preapply(mesh); // preliminary apply (preapply) to store all problem matrices
	if (regression.isIter())
	{
		if (regressionData.verbose_) Rprintf("in apply iter");
	    regression.apply_iterative();
	}
	else
	{
		if (regressionData.verbose_) Rprintf("in apply");
		regression.apply();
	}
	const MatrixXv& solution = regression.getSolution();
	const MatrixXr& dof = regression.getDOF();
	const MatrixXr & GCV = regression.getGCV();
	UInt bestLambda = optimizationData.get_best_lambda_S();
	MatrixXv beta;
	beta = regression.getBeta();
	const MatrixXr & barycenters = regression.getBarycenters();
	const VectorXi & elementIds = regression.getElementIds();
Rprintf("\nllllasaasssssssss");
	//Copy result in R memory
	SEXP result = NILSXP;
	result = PROTECT(Rf_allocVector(VECSXP, 5+5+2));
	
	SET_VECTOR_ELT(result, 0, Rf_allocMatrix(REALSXP, solution(0).size(), solution.size()));
	SET_VECTOR_ELT(result, 1, Rf_allocVector(REALSXP, solution.size()));
	SET_VECTOR_ELT(result, 2, Rf_allocVector(REALSXP, solution.size()));
	SET_VECTOR_ELT(result, 3, Rf_allocVector(INTSXP, 1));
	SET_VECTOR_ELT(result, 4, Rf_allocMatrix(REALSXP, beta(0).size(), beta.size()));

	Real *rans = REAL(VECTOR_ELT(result, 0));
	for(UInt j = 0; j < solution.size(); j++)
	{
		for(UInt i = 0; i < solution(0).size(); i++)
			rans[i + solution(0).size()*j] = solution(j)(i);
	}

	Real *rans1 = REAL(VECTOR_ELT(result, 1));
	for(UInt i = 0; i < solution.size(); i++)
	{
		rans1[i] = dof(i);
	}

	//! Copy GCV vector
	Real *rans2 = REAL(VECTOR_ELT(result, 2));
	for(UInt i = 0; i < solution.size(); i++)
	{
		rans2[i] = GCV(i);
	}

	//! Copy best lambda
	UInt *rans3 = INTEGER(VECTOR_ELT(result, 3));
	rans3[0] = bestLambda;

	//! Copy betas
	Real *rans4 = REAL(VECTOR_ELT(result, 4));
	for(UInt j = 0; j < beta.size(); j++)
	{
		for(UInt i = 0; i < beta(0).size(); i++)
			rans4[i + beta(0).size()*j] = beta(j)(i);
	}
				Rprintf("\ntyjuyjjus");

	//SEND TREE INFORMATION TO R
	SET_VECTOR_ELT(result, 5, Rf_allocVector(INTSXP, 1)); //tree_header information
	int *rans5 = INTEGER(VECTOR_ELT(result, 5));
	rans5[0] = mesh.getTree().gettreeheader().gettreelev();
				Rprintf("\ns");

	SET_VECTOR_ELT(result, 6, Rf_allocVector(REALSXP, ndim*2)); //tree_header domain origin
	Real *rans6 = REAL(VECTOR_ELT(result, 6));
	for(UInt i = 0; i < ndim*2; i++)
		rans6[i] = mesh.getTree().gettreeheader().domainorig(i);
				Rprintf("\nll");

	SET_VECTOR_ELT(result, 7, Rf_allocVector(REALSXP, ndim*2)); //tree_header domain scale
	Real *rans7 = REAL(VECTOR_ELT(result, 7));
	for(UInt i = 0; i < ndim*2; i++)
		rans7[i] = mesh.getTree().gettreeheader().domainscal(i);

				Rprintf("\nllllllmnyimuiynu");

	UInt num_tree_nodes = mesh.num_elements()+1; //Be careful! This is not equal to number of elements
	SET_VECTOR_ELT(result, 8, Rf_allocMatrix(INTSXP, num_tree_nodes, 3)); //treenode information
	int *rans8 = INTEGER(VECTOR_ELT(result, 8));
	for(UInt i = 0; i < num_tree_nodes; i++)
			rans8[i] = mesh.getTree().gettreenode(i).getid();
				Rprintf("\nladas");

	for(UInt i = 0; i < num_tree_nodes; i++)
			rans8[i + num_tree_nodes*1] = mesh.getTree().gettreenode(i).getchild(0);
				Rprintf("\nas");

	for(UInt i = 0; i < num_tree_nodes; i++)
			rans8[i + num_tree_nodes*2] = mesh.getTree().gettreenode(i).getchild(1);
				Rprintf("\nl");

	SET_VECTOR_ELT(result, 9, Rf_allocMatrix(REALSXP, num_tree_nodes, ndim*2)); //treenode box coordinate
	Real *rans9 = REAL(VECTOR_ELT(result, 9));
	for(UInt j = 0; j < ndim*2; j++)
	{
		for(UInt i = 0; i < num_tree_nodes; i++)
			rans9[i + num_tree_nodes*j] = mesh.getTree().gettreenode(i).getbox().get()[j];
	}
				Rprintf("\nlllldas");

	//SEND BARYCENTER INFORMATION TO R
	SET_VECTOR_ELT(result, 10, Rf_allocVector(INTSXP, elementIds.rows())); //element id of the locations point (vector)
	int *rans10 = INTEGER(VECTOR_ELT(result, 10));
	for(UInt i = 0; i < elementIds.rows(); i++)
		rans10[i] = elementIds(i);
				Rprintf("\nllllll");

	SET_VECTOR_ELT(result, 11, Rf_allocMatrix(REALSXP, barycenters.rows(), barycenters.cols())); //barycenter information (matrix)
	Real *rans11 = REAL(VECTOR_ELT(result, 11));
	for(UInt j = 0; j < barycenters.cols(); j++)
	{
		for(UInt i = 0; i < barycenters.rows(); i++)
			rans11[i + barycenters.rows()*j] = barycenters(i,j);
	}
	UNPROTECT(1);
				Rprintf("\ndas");

	return(result);
}


#endif