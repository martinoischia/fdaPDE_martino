#ifndef __MIXED_FE_REGRESSION_IMP_H__
#define __MIXED_FE_REGRESSION_IMP_H__

#include <iostream>
#include <chrono>
#include <random>
#include <fstream>
#include <thread>
#include "R_ext/Print.h"


//----------------------------------------------------------------------------//
// Dirichlet BC

template<typename InputHandler>
void MixedFERegressionBase<InputHandler>::addDirichletBC() //adds boundary conditions to all
{
	UInt id1,id3, id2;

	UInt nnodes = N_*M_;

	const std::vector<UInt> * bc_indices = regressionData_.getDirichletIndices();
	const std::vector<Real> * bc_values = regressionData_.getDirichletValues();
	UInt nbc_indices = bc_indices->size();
	Real pen=10e20;

	for( auto i=0; i<nbc_indices; i++)
	 {
			id1=(*bc_indices)[i];
			id3=id1+nnodes;
			id2=id1+N_;

         if (!regressionData_.getFlagIterative())
         {
             matrixNoCov_.coeffRef(id1,id1)=pen;
             matrixNoCov_.coeffRef(id3, id3) = pen;
         }
         else if (regressionData_.getFlagIterative() && i<nbc_indices/M_)
         {
             matrixNoCov_.coeffRef(id1, id1) = pen;
             matrixNoCov_.coeffRef(id2, id2) = pen;
         }

         _rightHandSide(id1)=(*bc_values)[i]*pen;
         _rightHandSide(id3)=0;
	 }

	matrixNoCov_.makeCompressed();
}


template<typename InputHandler>
void MixedFERegressionBase<InputHandler>::addDirichletBC_matrix() //adds boundary conditions to all
{
	UInt id1,id3;

	UInt nnodes = N_*M_;

	const std::vector<UInt> * bc_indices = regressionData_.getDirichletIndices();
	const std::vector<Real> * bc_values = regressionData_.getDirichletValues();
	UInt nbc_indices = bc_indices->size();

	Real pen=10e20;

	for( auto i=0; i<nbc_indices; i++)
	 {
			id1=(*bc_indices)[i];
			id3=id1+nnodes;

			matrixNoCov_.coeffRef(id1,id1)=pen;
			matrixNoCov_.coeffRef(id3,id3)=pen;
	 }

	matrixNoCov_.makeCompressed();
}




//Add NA
template<typename InputHandler>
void MixedFERegressionBase<InputHandler>::addNA()
{
	const std::vector<UInt> * observations_na= regressionData_.getObservationsNA();
	for(UInt id:*observations_na)
	{
		for(UInt j=0; j<psi_.cols(); ++j)
		{
			if(psi_.coeff(id,j)!=0)
				psi_.coeffRef(id, j) = 0;
		}
	}
	psi_.pruned();
	psi_.makeCompressed();
}

//----------------------------------------------------------------------------//
// Setters

template<typename InputHandler>
template<UInt ORDER, UInt mydim, UInt ndim>
void MixedFERegressionBase<InputHandler>::setPsi(const MeshHandler<ORDER, mydim, ndim> & mesh_)
{
	// Psi is a nlocations x nnodes  matrix, first fetch the dimensions
	UInt nnodes = mesh_.num_nodes();	// Define the number of nodes
	// Set the number of locations depending on presence or not of temporal data
	UInt nlocations = regressionData_.getNumberofSpaceObservations();

	psi_.resize(nlocations, nnodes); // Resize the matrix

	// Optimized strategies according to the presence of locations
	if(regressionData_.isLocationsByNodes() & !regressionData_.isLocationsByBarycenter()) // .Pointwise data -- no barycenters
	{
		std::vector<coeff> tripletAll;
		if(!regressionData_.isSpaceTime() && !regressionData_.isMixed()) // Just spatial case
		{
			// THEORETICAL REMARK:
			// If isLocationsByNodes is active it entails that nodes are used as locations
			// However, maybe, NOT IN ALL nodes evaluations are performed: some might be utility nodes not
			// associated with values:
			// e.g. think about a triangle with 6 nodes, probably only the main 3 might be associated
			// with a value with the others not, but this is only one possible choice:
			// The efficent fact is that anyway Phi_j(p_i) will be either 1 if i is the Lagrange
			// node of j or 0 if it is not. Note that since Phi are as many as the nodes, but only
			// some of the nodes are locations some columns of Phi = [Phi_j(p_i)]_ij might be of
			// all zeros: i.e. that Phi_j is linked to a node which is not a location since it's value is NA.
			// In such a case Phi is NOT a sqare matrix and there are more culumns than rows
			// If all nodes == locations then Phi == square Identity, but this is just a particular case
			// and, even though isLocationsByNodes is true, this might not be veriefied
			const std::vector<UInt> * k = regressionData_.getObservationsIndices();
			UInt k_size = k->size();
			tripletAll.reserve(k_size);

			for (UInt i=0; i< k_size; ++i)
			{
				// Add a value 1 for each valid index in row i
				// and column k[i] (under psi_k[i], the associated node)
				tripletAll.push_back(coeff(i,(*k)[i],1.0));
			}
		}
		else
		{
			tripletAll.reserve(nlocations);
			for (UInt i=0; i<nlocations; ++i)
			{
				tripletAll.push_back(coeff(i,i,1.0));
			}
		}
		psi_.setFromTriplets(tripletAll.begin(),tripletAll.end());
								
	}
	else if(regressionData_.isLocationsByBarycenter() && (regressionData_.getNumberOfRegions() == 0)) // Pointwise data -- by barycenter
	{
		// Exploit isLocationsByBarycenter simplyfication
		static constexpr UInt EL_NNODES = how_many_nodes(ORDER,mydim);
					  

		for(UInt i=0; i<nlocations;i++)
		{ // Update Psi looping on all locations
			// We already know the id of the element containing the point
			Element<EL_NNODES, mydim, ndim> tri_activated = mesh_.getElement(regressionData_.getElementId(i));

			if(tri_activated.getId() == Identifier::NVAL)
			{ // Invald id --> error
							
				Rprintf("ERROR: Point %d is not in the domain, remove point and re-perform smoothing\n", i+1);
			}
			else
			{ // tri_activated.getId() found, it's action might be felt a priori by all the psi of the element, one for each node
			 
												  
	 
				for(UInt node=0; node<EL_NNODES ; ++node)
				{// Loop on all the nodes of the found element and update the related entries of Psi
					Real evaluator = regressionData_.getBarycenter(i,node); // We already know the value to add
					// Insert the value in the column given by the GLOBAL indexing of the evaluated NODE
					psi_.insert(i, tri_activated[node].getId()) = evaluator;
				}
			}
		} // End of for loop
								
	}
	else if((!regressionData_.isLocationsByBarycenter()) && (regressionData_.getNumberOfRegions() == 0))
	{
		// THEORETICAL REMARK:
		// If isLocationsByNodes && isLocationsByBarycenters are false
		// locations are unspecified points, so we have to evaluate them directly
		static constexpr UInt EL_NNODES = how_many_nodes(ORDER,mydim);

		// Resize for info storage
		this->barycenters_.resize(nlocations, EL_NNODES);
		this->element_ids_.resize(nlocations);

		for(UInt i=0; i<nlocations;i++)
		{ // Update Psi looping on all locations
			// [[GM missing a defaulted else, raising a WARNING!]]
																										 
																									  
			Element<EL_NNODES, mydim, ndim> tri_activated = mesh_.findLocation(regressionData_.template getLocations<ndim>(i));
	 

			// Search the element containing the point
			if(tri_activated.getId() == Identifier::NVAL)
			{ // If not found
							
				Rprintf("ERROR: Point %d is not in the domain, remove point and re-perform smoothing\n", i+1);
			}
			else
			{ // tri_activated.getId() found, it's action might be felt a priori by all the psi of the element, one for each node
	 
				element_ids_(i) = tri_activated.getId(); // Save the id of the ELEMENT containing the location

				for(UInt node=0; node<EL_NNODES ; ++node)
				{// Loop on all the nodes of the found element and update the related entries of Psi
					// Evaluate psi in the node
					Real evaluator = tri_activated.evaluate_point(regressionData_.template getLocations<ndim>(i), Eigen::Matrix<Real,EL_NNODES,1>::Unit(node));
					// Save barycenter information
					barycenters_(i,node)=tri_activated.getBaryCoordinates(regressionData_.template getLocations<ndim>(i))[node];
					// Insert the value in the column given by the GLOBAL indexing of the evaluated NODE
					psi_.insert(i, tri_activated[node].getId()) = evaluator;
				}
			}
		} // End of for loop
	}
	else // Areal data
	{
		static constexpr UInt EL_NNODES = how_many_nodes(ORDER,mydim);

		Real * tab; // Psi_i temporary storage
		tab = (Real*) malloc(sizeof(Real)*nnodes);

		for(UInt i=0; i<nlocations;i++) //nlocations = number of regions
		{ // Loop to update by rows Psi matrix
			for(UInt k=0; k<nnodes; k++)
				tab[k] = 0;	//Iniialize at 0
			for(UInt j=0; j<mesh_.num_elements(); j++)
			{
				if((*(regressionData_.getIncidenceMatrix()))(i,j) == 1) // Element j is in region i
				{ // Location is related to that mesh element
					Element<EL_NNODES, mydim, ndim> tri = mesh_.getElement(j); // Identify the element
					for(UInt k=0; k<EL_NNODES; k++)
					{ // Add contribution of the area to right location
						tab[tri[k].getId()] += tri.integrate(Eigen::Matrix<Real,EL_NNODES,1>::Unit(k)); // integral over tri of psi_k
					}
				}
			}
			for(UInt k=0; k<nnodes; k++)
			{ // Loop update Psi matrix i-th row
				if(tab[k] != 0) // Relevant change
				{ // Update the right position
					psi_.insert(i,k) = tab[k]/A_(i); // Divide by |D_i|
				}
			}
		}
		free(tab);	// Deallocate dynamic memory
								
	}
	psi_.makeCompressed();	// Compress for optimization
}

template<typename InputHandler>
void MixedFERegressionBase<InputHandler>::setH(void)
{
	// Debug print
	// Rprintf("Computing Projection Matrix\n");

	UInt nlocations = regressionData_.getNumberofObservations();
	const auto * Wp(this->regressionData_.getCovariates());
	bool ilbn = regressionData_.isLocationsByNodes();
	if (!ilbn) H_ = (*Wp)*WTW_.solve(typename InputHandler::des_mat_type(Wp->transpose()));
	else
	{
		const std::vector<UInt> * k = regressionData_.getObservationsIndices();

		// Some rows might be discarded [[we probably have data for every node not only the points ???]]
		UInt n_Wcols = Wp->cols();
		typename InputHandler::des_mat_type * Wp_reduced  = new typename InputHandler::des_mat_type;
		Wp_reduced->resize(regressionData_.getNumberofObservations(), n_Wcols);

		for (UInt i=0; i<nlocations; ++i)
		{
		       UInt index_i = (*k)[i];
		       for (auto j=0; j<n_Wcols; ++j)
		       {
			       Wp_reduced->coeffRef(i,j) = Wp->coeff(index_i,j);
		       }
		}
		const typename InputHandler::des_mat_type Wt = Wp_reduced->transpose(); // Compute W^t
		typename InputHandler::inv_mat_type solv;
		solv.compute(Wt * (*Wp_reduced));
		H_ = (*Wp_reduced) * solv.solve(Wt);		 // using cholesky LDLT decomposition for computing hat matrix
		delete Wp_reduced;
	}
}

template<typename InputHandler>
void MixedFERegressionBase<InputHandler>::setQ(void)
{
	// Debug text
 	// std::cout << "Computing Orthogonal Space Projection Matrix" << std::endl;

	// Remember Q = I-H
	Q_.resize(H_.rows(), H_.cols());	// Resizing dimensions as H
	Q_ = -H_;
	for (UInt i=0; i<H_.rows(); ++i)
	{
		Q_(i,i) += 1;			// Summing the identity by rows (or columns)
	}
}

template<typename InputHandler>
template<UInt ORDER, UInt mydim, UInt ndim>
void MixedFERegressionBase<InputHandler>::setA(const MeshHandler<ORDER, mydim, ndim> & mesh_)
{
	UInt nRegions = regressionData_.getNumberOfRegions();	// Number of regions for areal partition
	// If the problem is temporal, m stores the number of temporal nodes, else is defaulted as 1
	UInt m = regressionData_.isSpaceTime() ? regressionData_.getNumberofTimeObservations():1;
	if(!this->regressionData_.isArealDataAvg())
	{ //areal data for FPIRLS
		A_ = VectorXr::Ones(m*nRegions);	// vector of pure ones
	}
	else
	{
		A_ = VectorXr::Zero(m*nRegions);	// neutral vector to be filled
		for(UInt i=0; i<nRegions; i++)		// fill the vector
		{
			const MatrixXi * imp = regressionData_.getIncidenceMatrix();
			for(UInt j=0; j<imp->cols(); j++)
			{
				if((*imp)(i,j) == 1)	// Valid input for region
				{
					A_(i) += mesh_.elementMeasure(j); // Add area
				}
			}
			for(UInt k=1; k<m; k++) // if m=1 we avoid the step
			{
				A_(i+k*nRegions) = A_(i); // Replicate the vector m times
			}
		}
	}
}

template<typename InputHandler>
void MixedFERegressionBase<InputHandler>::setpsi_t_(void)
{
	//Additional storage of the transpose, which changes in case of spacetime
	psi_t_ = SpMat(psi_.transpose());
	psi_t_.makeCompressed(); // Compress sparse matrix
}

template<typename InputHandler>
void MixedFERegressionBase<InputHandler>::setDMat(void)
{
	if(regressionData_.getWeightsMatrix()->size() == 0) // no weights
		DMat_ = psi_;
	else
		DMat_ = regressionData_.getWeightsMatrix()->asDiagonal()*psi_;


	if(regressionData_.getNumberOfRegions() == 0) // pointwise data
		DMat_ = psi_t_*DMat_;
    else
    {
        if (!regressionData_.getFlagIterative())
            DMat_ = psi_t_*A_.asDiagonal()*DMat_; // areal data: need to add the diag(|D_1|,...,|D_N|)
        else
        {
            VectorXr miniA_  = A_.segment(0, regressionData_.getNumberOfRegions());
            DMat_ = psi_t_ *miniA_.asDiagonal()*DMat_; // areal data for iterative method
        }
	}


}
//----------------------------------------------------------------------------//
// Utilities [[GM NOT VERY OPTMIZED, SENSE??, we have Q and P...]]

template<typename InputHandler>
MatrixXr MixedFERegressionBase<InputHandler>::LeftMultiplybyQ(const MatrixXr& u)
{
	// Weight matrix is used for GAM problems, it is also automatically added to the utility
	const VectorXr * P = this->regressionData_.getWeightsMatrix();

	if(regressionData_.getCovariates()->rows() == 0)
	{
		// Q is the projecton on Col(W) if W == 0 => Q = Identity
		if(P->size()==0)
			return u;
		else
			return P->asDiagonal()*u;
	}
	else
	{	
		// This should be fixed because I did not take into account this case when I moved WTW to regressionData

		const auto & W(*(this->regressionData_.getCovariates()));
		// // Check factorization, if not present factorize the matrix W^t*W
		// if(isWTWfactorized_ == false)
		// {
		// 	if(P->size() == 0)
		// 		WTW_.compute(W.transpose()*W);
		// 	else
		// 		WTW_.compute(W.transpose()*P->asDiagonal()*W);
		// 	isWTWfactorized_=true; // Flag to no repeat the operation next time
		// }

		MatrixXr Hu;
		// Compute H (or I-Q) the projection on Col(W) and multiply it times u
		if(P->size() == 0)
			Hu = W*WTW_.solve(W.transpose()*u);
		else
			Hu = W*WTW_.solve(W.transpose()*P->asDiagonal()*u);

		// Return the result
		if(P->size()==0)
			return u-Hu;
		else
			return P->asDiagonal()*(u - Hu);
	}

}

//----------------------------------------------------------------------------//
// Builders
template<typename InputHandler>
void MixedFERegressionBase<InputHandler>::buildSpaceTimeMatrices()
{
    SpMat IM(M_, M_); // (Identinty M_ elements) Matrix temporal_nodes x temporal_nodes
    SpMat phi;     // Dummy for update, old Psi will be overwritten by Psi_tilde
	// Distinguish between two problem classes
	if(regressionData_.getFlagParabolic() && !regressionData_.isMixed())
	{ // Parabolic case
            MixedFDRegression <InputHandler> FiniteDifference(mesh_time_, regressionData_);
            FiniteDifference.setDerOperator();
            SpMat L = FiniteDifference.getDerOpL(); // Matrix of finite differences
            IM.setIdentity(); // Set as identity matrix
            LR0k_ = kroneckerProduct(L, R0_); // REMARK --> HAS TO BE ADDED TO R1 (that is R1 tilde) in the system
            phi = IM;
		// Right hand side correction for the initial condition:
		rhs_ic_correction_ = (1/(mesh_time_[1]-mesh_time_[0]))*(R0_*(*(regressionData_.getInitialValues())));
	}
	else if (regressionData_.isMixed()){
		IM.setIdentity();
		phi = IM;
	}
	else
	{ // Separable case
		MixedSplineRegression <InputHandler> Spline(mesh_time_,regressionData_);
		SpMat IN(N_,N_);
		Spline.setPhi();
		Spline.setTimeMass();
		Spline.smoothSecondDerivative();
		if(regressionData_.getFlagMass())
		{ // Mass penalization
			IM = Spline.getTimeMass();
			IN = R0_;
		}
		else
		{ // Identity penalization
			IM.setIdentity();
			IN.setIdentity();
		}
		phi = Spline.getPhi();
		SpMat Pt = Spline.getPt();
		Ptk_ = kroneckerProduct(Pt,IN);
	}

	// Make the Kronecker product to tensorize the system, overwriting old matrices
	// Update Psi_

        SpMat psi_temp = psi_;
        psi_.resize(N_ * M_, N_ * M_);
        psi_ = kroneckerProduct(phi, psi_temp);
        addNA();

        // Update R1_
        SpMat R1_temp = R1_;
        R1_.resize(N_ * M_, N_ * M_);
        R1_ = kroneckerProduct(IM, R1_temp);
        R1_.makeCompressed();

        // Update R0
        SpMat R0_temp = R0_;
        R0_.resize(N_ * M_, N_ * M_);
        R0_ = kroneckerProduct(IM, R0_temp);
        R0_.makeCompressed();

	// right hand side correction for the forcing term:
	if(this->isSpaceVarying)
	{ // otherwise no forcing term needed
		VectorXr forcingTerm = rhs_ft_correction_; // Store old data
		rhs_ft_correction_.resize(M_*N_); // New size
		for(UInt i=0; i<N_; i++) // Update forcing term (i.e. repeat identically M_ times)
		{
			for(UInt j=0; j<M_; j++)
			{
				rhs_ft_correction_(i+j*N_) = forcingTerm(i);
			}
		}
	}
}


template<typename InputHandler>
void MixedFERegressionBase<InputHandler>::buildSpaceTimeMatrices_iterative(){
    UInt nnodes = N_ * M_; // Define number of space-times nodes

    // set Psi_tilde, used to build the rhs of the system
    SpMat  psi_temp = psi_;
    SpMat IM(M_, M_); // Matrix temporal_nodes x temporal_nodes
    IM.setIdentity();
    psi_.resize(nnodes, nnodes);
    psi_ = kroneckerProduct(IM, psi_temp);
    addNA();

	if(regressionData_.isSpaceTime())
	{
		Real delta = mesh_time_[1] - mesh_time_[0]; // Time interval
		// Right hand side correction for the initial condition:
		rhs_ic_correction_ = (1 / delta)*(R0_*(*(regressionData_.getInitialValues())));
	}

    // right hand side correction for the forcing term:
    if(this->isSpaceVarying)
    { // otherwise no forcing term needed
        VectorXr forcingTerm = rhs_ft_correction_; // Store old data
        rhs_ft_correction_.resize(M_*N_); // New size
        for(UInt i=0; i<N_; i++) // Update forcing term (i.e. repeat identically M_ times)
        {
            for(UInt j=0; j<M_; j++)
            {
                rhs_ft_correction_(i+j*N_) = forcingTerm(i);
            }
        }
    }
}

template<typename InputHandler>
void MixedFERegressionBase<InputHandler>::getRightHandData(VectorXr& rightHandData)
{
	UInt nnodes = N_*M_;	// Total number of spatio-temporal nodes
	const VectorXr * obsp = regressionData_.getObservations();	// Get the value of the observations

	rightHandData = VectorXr::Zero(nnodes);	// Initialize the rhd at 0
	if(regressionData_.getCovariates()->rows() == 0)
	{ // No covariates case: if mixed I should not enter here because there are covariates
		if(regressionData_.isLocationsByNodes() && !regressionData_.isSpaceTime())
		{ // Regresionbyodes + just space --> Psi^t*z [simplified Psi] (or Psi^t*Q*z in GAM)
			// does nothing since Q==I unless in GAM [where multiplication by Q also involves P (weight matrix)]
			VectorXr tmp = LeftMultiplybyQ(*obsp);
			UInt nlocations = regressionData_.getNumberofObservations();	// Count number of locations

			for(UInt i=0; i<nlocations; ++i)
			{ // Simplified multiplication by Psi^t
				auto index_i = (*(regressionData_.getObservationsIndices()))[i];
				rightHandData(index_i) = tmp(i);
			}
		}
		else if(regressionData_.isLocationsByNodes() && regressionData_.isSpaceTime() && regressionData_.getFlagParabolic())
		{ // Regressionbynodes + parabolic --> Psi^t*z [simplified Psi]
			for(UInt i=0; i<regressionData_.getObservationsIndices()->size(); ++i)
			{ // Simplified multiplication by Psi^t
				auto index_i = (*(regressionData_.getObservationsIndices()))[i];
				rightHandData(index_i) = (*obsp)[index_i];
			}
		}
		else if(regressionData_.getNumberOfRegions() == 0)
		{ // Generic pointwise pata, no optimization allowed --> Psi^t*z [or Psi^t*P*z in GAM]
			// LeftMultiplybyQ does nothing since Q==I unless in GAM [where multipliction by Q also involves P (weight matrix)]
			rightHandData = psi_.transpose()*LeftMultiplybyQ(*obsp);
		}
		else
		{ // Areal data, no optimization allowed --> Psi^t*A*z [or Psi^t*A*P*z in GAM]
			// LeftMultiplybyQ does nothing since Q==I unless in GAM [where multipliction by Q also involves P (weight matrix)]
			rightHandData = psi_.transpose()*A_.asDiagonal()*LeftMultiplybyQ(*obsp);
		}
	}
	else if(regressionData_.getNumberOfRegions() == 0)
	{ // With covariates, pointwise data, no optimization --> Psi^t*Q*z [in GAM Q=Q(P)]
		rightHandData = psi_.transpose()*LeftMultiplybyQ(*obsp);
	}
	else
	{ // With covariates, areal data, no optimization --> Psi^t*A*Q*z [in GAM Q=Q(P)]
		rightHandData = psi_.transpose()*A_.asDiagonal()*LeftMultiplybyQ(*obsp);
	}

}

template<typename InputHandler>
void MixedFERegressionBase<InputHandler>::buildMatrixNoCov(const SpMat & NWblock, const SpMat & SWblock,  const SpMat & SEblock)
{
    UInt nnodes = N_; // only space and Iterative method
	if(!regressionData_.getFlagIterative())
    	if (regressionData_.isSpaceTime() || regressionData_.isMixed())
        	nnodes *= M_;

	// Vector to be filled with the triplets used to build _coeffmatrix (reserved with the right dimension)
	std::vector<coeff> tripletAll;
	tripletAll.reserve(NWblock.nonZeros() + 2*SWblock.nonZeros() + SEblock.nonZeros());

	// Parsing all matrices, reading the values to be put inside _coeffmatrix, coordinates according to the rules
	for(UInt k=0; k<NWblock.outerSize(); ++k)
		for(SpMat::InnerIterator it(NWblock,k); it; ++it)
		{
			tripletAll.push_back(coeff(it.row(), it.col(),it.value()));
		}
	for(UInt k=0; k<SEblock.outerSize(); ++k)
		for(SpMat::InnerIterator it(SEblock,k); it; ++it)
		{
			tripletAll.push_back(coeff(it.row()+nnodes, it.col()+nnodes,it.value()));
		}
	for(UInt k=0; k<SWblock.outerSize(); ++k)
		for(SpMat::InnerIterator it(SWblock,k); it; ++it)
		{
			tripletAll.push_back(coeff(it.col(), it.row()+nnodes,it.value()));
		}
	for (UInt k=0; k<SWblock.outerSize(); ++k)
		for (SpMat::InnerIterator it(SWblock,k); it; ++it)
		{
			tripletAll.push_back(coeff(it.row()+nnodes, it.col(), it.value()));
		}

	// Define, resize, fill and compress
	matrixNoCov_.setZero();
	matrixNoCov_.resize(2*nnodes,2*nnodes);
	matrixNoCov_.setFromTriplets(tripletAll.begin(),tripletAll.end());
	matrixNoCov_.makeCompressed();
}

//----------------------------------------------------------------------------//
// Factorizer & Solver
template<typename InputHandler>
void MixedFERegressionBase<InputHandler>::system_factorize()
{

    UInt nnodes = N_*M_;	// Note that is only space M_=1
	const VectorXr * P = regressionData_.getWeightsMatrix(); // Matrix of weights for GAM

	// First phase: Factorization of matrixNoCov
	matrixNoCovdec_.compute(matrixNoCov_);

	const auto & W(*(this->regressionData_.getCovariates()));

	if(regressionData_.getCovariates()->rows() != 0 && !isUVComputed)
	{ // Needed only if there are covariates, else we can stop before
		// Second phase: factorization of matrix  G =  C + [V * matrixNoCov^-1 * U]= C + D
		// Definition of matrix U = [ psi^T * A * W | 0 ]^T and V= [ W^T*psi| 0]
        isUVComputed=true;
		U_ = MatrixXr::Zero(2*nnodes,W.cols());
		V_ = MatrixXr::Zero(W.cols(),2*nnodes);

		if(P->size()==0)
		{
			V_.leftCols(nnodes) = W.transpose()*psi_;
		}
		else
		{
			V_.leftCols(nnodes) = W.transpose()*P->asDiagonal()*psi_;
		}


		if(regressionData_.getNumberOfRegions()==0)
		{ // pointwise data
			if(P->size() == 0)
				U_.topRows(nnodes) = psi_.transpose()*W;
			else
				U_.topRows(nnodes) = psi_.transpose()*P->asDiagonal()*W;
		}
		else
		{ //areal data
			if(P->size() == 0)
				U_.topRows(nnodes) = psi_.transpose()*A_.asDiagonal()*W;
			else
				U_.topRows(nnodes) = psi_.transpose()*A_.asDiagonal()*P->asDiagonal()*W;
    	}
	}

	if (regressionData_.getCovariates()->rows() != 0 &&!regressionData_.getFlagIterative())
	{
		MatrixXr D = V_ * matrixNoCovdec_.solve(U_);

		// G = C + D
		MatrixXr G;
		if (P->size() == 0)
		{
			G = -W.transpose() * W + D;
		}
		else
		{
			G = -W.transpose() * P->asDiagonal() * W + D;
		}
		Gdec_.compute(G);
	}
}

template<typename InputHandler>
template<typename Derived>
MatrixXr MixedFERegressionBase<InputHandler>::solve_covariates_iter(const Eigen::MatrixBase<Derived> & b, UInt time_index)
{
    //Iterative method, called only if there are covariates
    //splits the matrices U,V (built in system_factorize) and find the solution

    const auto & W(*(this->regressionData_.getCovariates()));
	MatrixXr V_k = MatrixXr::Zero(V_.rows(), 2 * N_);
	V_k.leftCols(N_) = V_.block(0, time_index * N_, V_.rows(), N_);

	MatrixXr U_k = MatrixXr::Zero(2 * N_, U_.cols());
	U_k.topRows(N_) = U_.block(time_index * N_, 0, N_, U_.cols());

	MatrixXr D = V_k * matrixNoCovdec_.solve(U_k);

	// G = C + D
	MatrixXr G;
	G = -W.transpose() * W + D;
	Gdec_.compute(G);

	MatrixXr x1 = matrixNoCovdec_.solve(b);

	// Resolution of G * x2 = V * x1
	MatrixXr x2 = Gdec_.solve(V_k * x1);

	// Resolution of the system matrixNoCov * x3 = U * x2
	x1 -= matrixNoCovdec_.solve(U_k * x2);

	return x1;
}

template<typename InputHandler>
template<typename Derived>
MatrixXr MixedFERegressionBase<InputHandler>::system_solve(const Eigen::MatrixBase<Derived> & b)
{
	// Resolution of the system matrixNoCov * x1 = b
	MatrixXr x1 = matrixNoCovdec_.solve(b);
	if(regressionData_.getCovariates()->rows() != 0 && !regressionData_.getFlagIterative())
	{
		// Resolution of G * x2 = V * x1
		MatrixXr x2 = Gdec_.solve(V_*x1);
		// Resolution of the system matrixNoCov * x3 = U * x2
		x1 -= matrixNoCovdec_.solve(U_*x2);
	}
	return x1;
}


//----------------------------------------------------------------------------//
// GCV

template<typename InputHandler>
void MixedFERegressionBase<InputHandler>::computeDegreesOfFreedom(UInt output_indexS, UInt output_indexT, Real lambdaS, Real lambdaT)
{
	std::string GCVmethod = optimizationData_.get_DOF_evaluation();
	switch (GCVmethod == "exact") {
		case 1:
		  if(regressionData_.getFlagIterative() & !isGAMData)
              computeDOFExact_iterative(output_indexS, output_indexT, lambdaS, lambdaT);
			else
              computeDegreesOfFreedomExact(output_indexS, output_indexT, lambdaS, lambdaT);
			break;
		case 0:
			if(regressionData_.getFlagIterative() & !isGAMData)
                computeDOFStochastic_iterative(output_indexS, output_indexT, lambdaS, lambdaT);
			else
                computeDegreesOfFreedomStochastic(output_indexS, output_indexT, lambdaS, lambdaT);
			break;
	}
}

template<typename InputHandler>
void MixedFERegressionBase<InputHandler>::computeGeneralizedCrossValidation(UInt output_indexS, UInt output_indexT, Real lambdaS, Real lambdaT)
{
	VectorXr dataHat;

	const VectorXr* z = regressionData_.getObservations();
	if(regressionData_.getCovariates()->rows()==0) //Data estimated from the model
		dataHat = psi_*_solution(output_indexS,output_indexT).topRows(psi_.cols());
	else
		dataHat = *z - LeftMultiplybyQ(*z) + LeftMultiplybyQ(psi_*_solution(output_indexS,output_indexT).topRows(psi_.cols()));

	UInt n = dataHat.rows();
	if(regressionData_.isSpaceTime() || regressionData_.isMixed())
		{
			const std::vector<UInt> * observations_na= regressionData_.getObservationsNA();
			for(UInt id:*observations_na)
			{
				dataHat[id]=0;
			}
			n-=observations_na->size();
		}

//! GCV computation
	_GCV(output_indexS,output_indexT) = (n / ((n - optimizationData_.get_tuning()*((this->getDOF())(output_indexS, output_indexT))) * (n - optimizationData_.get_tuning()*((this->getDOF())(output_indexS, output_indexT))))) * (*z-dataHat).dot(*z-dataHat);
	if (_GCV(output_indexS,output_indexT) < optimizationData_.get_best_value())
	{
		optimizationData_.set_best_lambda_S(output_indexS);
		optimizationData_.set_best_lambda_T(output_indexT);
		optimizationData_.set_best_value(_GCV(output_indexS,output_indexT));
	}
}

template<typename InputHandler>
void MixedFERegressionBase<InputHandler>::computeDegreesOfFreedomExact(UInt output_indexS, UInt output_indexT, Real lambdaS, Real lambdaT)
{
    UInt nnodes = N_*M_;
    UInt nlocations = regressionData_.getNumberofObservations();
    Real degrees=0;


    MatrixXr X1;
    if (regressionData_.getNumberOfRegions() == 0){ //pointwise data
        X1 = psi_.transpose() * LeftMultiplybyQ(psi_);
    }else{ //areal data
        X1 = psi_.transpose() * A_.asDiagonal() * LeftMultiplybyQ(psi_);
    }


    if (isRcomputed_ == false)
    {
        isRcomputed_ = true;
        //take R0 from the final matrix since it has already applied the dirichlet boundary conditions
        SpMat R0 = matrixNoCov_.bottomRightCorner(nnodes,nnodes)/lambdaS;

         R0dec_.compute(R0);
        if(!regressionData_.isSpaceTime() || !regressionData_.getFlagParabolic())
        {
            MatrixXr X2 = R0dec_.solve(R1_);
            R_ = R1_.transpose() * X2;
        }
    }

    MatrixXr P;
    MatrixXr X3=X1;

    //define the penalization matrix: note that for separable smoothin should be P=lambdaS*Psk+lambdaT*Ptk
    // but the second term has been added to X1 for dirichlet boundary conditions
    if (regressionData_.isSpaceTime() && regressionData_.getFlagParabolic())
    {
        SpMat X2 = R1_+lambdaT*LR0k_;
        P = lambdaS*X2.transpose()*R0dec_.solve(X2);
    }
    else
    {
        P = lambdaS*R_;
    }


    if(regressionData_.isSpaceTime() && !regressionData_.getFlagParabolic())
        X3 += lambdaT*Ptk_;

    //impose dirichlet boundary conditions if needed
    if(regressionData_.getDirichletIndices()->size()!=0)
    {
        const std::vector<UInt> * bc_indices = regressionData_.getDirichletIndices();
        UInt nbc_indices = bc_indices->size();

         Real pen=10e20;
        for(UInt i=0; i<nbc_indices; i++)
        {
            UInt id = (*bc_indices)[i];
            X3(id,id)=pen;
        }
    }

    X3 -= P; 
    Eigen::PartialPivLU<MatrixXr> Dsolver(X3);

    const auto k = regressionData_.getObservationsIndices();

    if(!regressionData_.isSpaceTime() && regressionData_.isLocationsByNodes()) {
    if(regressionData_.getCovariates()->rows() != 0)
        degrees += regressionData_.getCovariates()->cols();

    // Setup rhs B
    MatrixXr B;
    B = MatrixXr::Zero(nnodes,nlocations);
    // B = I(:,k) * Q
    for (auto i=0; i<nlocations;++i) {
        VectorXr ei = VectorXr::Zero(nlocations);
        ei(i) = 1;
        VectorXr Qi = LeftMultiplybyQ(ei);
        for (int j=0; j<nlocations; ++j) {
            B((*k)[i], j) = Qi(j);
    }
    }
    // Solve the system TX = B
    MatrixXr X;
    X = Dsolver.solve(B);
    // Compute trace(X(k,:))
    for (int i = 0; i < k->size(); ++i) {
        degrees += X((*k)[i], i);
    }
    }

    if (regressionData_.isSpaceTime() || !regressionData_.isLocationsByNodes() || regressionData_.isMixed())
    {
        MatrixXr X;
    X = Dsolver.solve(X1);

    if (regressionData_.getCovariates()->rows() != 0) {
        degrees += regressionData_.getCovariates()->cols();
    }
    for (int i = 0; i<nnodes; ++i) {
        degrees += X(i,i);
    }
    }

    _dof(output_indexS,output_indexT) = degrees;
}

template<typename InputHandler>
void MixedFERegressionBase<InputHandler>::computeDOFExact_iterative(UInt output_indexS, UInt output_indexT, Real lambdaS, Real lambdaT) {
    UInt stopthecount = 0;
    Real degrees = 0;
    UInt nlocations = regressionData_.getNumberofSpaceObservations();

    for(UInt k=0; k<M_; k++)
    {
      MatrixXr X1;
      MatrixXr Q_k = MatrixXr::Zero(nlocations,nlocations);
      if (regressionData_.getCovariates()->rows() != 0)
          Q_k = Q_.block(k*nlocations,k*nlocations,nlocations,nlocations);
      else {
          for (UInt i=0; i<nlocations; i++){
              Q_k(i,i)=1;
          }

      }
      if (regressionData_.getNumberOfRegions() == 0) { //pointwise data
          X1 = Q_k;
      } else { //areal data
          VectorXr miniA_  = A_.segment(0, regressionData_.getNumberOfRegions());
          X1 =  miniA_.asDiagonal() * Q_k;
          }

      psi_mini = psi_.block(k * nlocations, k* N_, nlocations, N_);
      X1=psi_mini.transpose()*X1*psi_mini;

      if (isRcomputed_ == false)
      {
          isRcomputed_ = true;
          SpMat R0;
            //take R0 from the final matrix since it has already applied the dirichlet boundary conditions
            R0 = matrixNoCov_.bottomRightCorner(N_, N_) / lambdaS;
            Eigen::SparseLU<SpMat> R0dec_;
            R0dec_.compute(R0);
            MatrixXr X2 = R0dec_.solve(R1_);
            R_ = R1_.transpose() * X2;
      }
    
          MatrixXr P;
          MatrixXr X3 = X1;
          //define the penalization matrix:
          //  P = lambdaS * (psi_mini*R_*psi_mini.transpose());
			P = lambdaS * R_;

        //impose dirichlet boundary conditions if needed

         if (regressionData_.getDirichletIndices()->size() != 0)
         {
            const std::vector<UInt> * bc_indices = regressionData_.getDirichletIndices();

             UInt nbc_indices = bc_indices->size();
            Real pen=10e20;
            for (UInt i = 0; i < (nbc_indices/M_); i++) {
                UInt id1=(*bc_indices)[i];

                X3.coeffRef(id1, id1) = pen;
            }
         }

        X3 -= P;

        Eigen::PartialPivLU <MatrixXr> Dsolver(X3);


         const auto ki = regressionData_.getObservationsIndices();

        // Setup rhs B

		MatrixXr psiQ_k;
		if (regressionData_.getNumberOfRegions() == 0) { //pointwise data
			 psiQ_k= MatrixXr::Zero(nlocations,nlocations);
			for(UInt i=0; i< nlocations;i++){
				psiQ_k(i,i)=1;
			}
            if (regressionData_.getCovariates()->rows() == 0)
                psiQ_k=psi_mini.transpose()*psi_mini;
			else
	        {
	            if(stopthecount==0){
	                 degrees += regressionData_.getCovariates()->cols();
	                stopthecount=1;
	          }
	          psiQ_k=Q_.block(k*nlocations,k*nlocations,nlocations,nlocations);
							psiQ_k=psi_mini.transpose()*psiQ_k*psi_mini;
	        }
		}

		if (regressionData_.getNumberOfRegions() != 0)
		{ //areal data
			UInt nreg=regressionData_.getNumberOfRegions();

			if (regressionData_.getCovariates()->rows() != 0)
			{
			    if(stopthecount==0)
			    {
			        degrees += regressionData_.getCovariates()->cols();
			        stopthecount=1;
			    }
			    psiQ_k=Q_.block(k*nreg,k*nreg,nreg,nreg);
					psiQ_k=psi_mini.transpose()*psiQ_k*psi_mini;

			}
			if (regressionData_.getCovariates()->rows() == 0)
			{
			    psiQ_k=psi_mini.transpose()*psi_mini;
			}

		}

		// Solve the system TX = B
        MatrixXr X;
        X = Dsolver.solve(psiQ_k);

        // Compute trace(X(k,:))
        for (UInt i = 0; i < N_; ++i) {
            degrees += X(i, i);
        }
    }
      _dof(output_indexS, output_indexT) = degrees;
}


template<typename InputHandler>
void MixedFERegressionBase<InputHandler>::computeDegreesOfFreedomStochastic(UInt output_indexS, UInt output_indexT, Real lambdaS, Real lambdaT)
{
	UInt nnodes = N_*M_;
	UInt nlocations = regressionData_.getNumberofObservations();

	// std::random_device rd;
    auto seed = std::chrono::system_clock::now().time_since_epoch().count();
    std::default_random_engine generator(seed);
	// Creation of the random matrix
	std::bernoulli_distribution distribution(0.5);
	UInt nrealizations = optimizationData_.get_nrealizations();
	MatrixXr u(nlocations, nrealizations);
	for (int j=0; j<nrealizations; ++j) {
		for (int i=0; i<nlocations; ++i) {
			if (distribution(generator)) {
				u(i,j) = 1.0;
			}
			else {
				u(i,j) = -1.0;
			}
		}
	}

	// Define the first right hand side : | I  0 |^T * psi^T * A * Q * u
	MatrixXr b = MatrixXr::Zero(2*nnodes,u.cols());
	if (regressionData_.getNumberOfRegions() == 0){
		b.topRows(nnodes) = psi_.transpose() * LeftMultiplybyQ(u);
	}else{
		b.topRows(nnodes) = psi_.transpose() * A_.asDiagonal() * LeftMultiplybyQ(u);
	}

	// Resolution of the system
	MatrixXr x = system_solve(b);

	MatrixXr uTpsi = u.transpose()*psi_;
	VectorXr edf_vect(nrealizations);
	Real q = 0;

	// Degrees of freedom = q + E[ u^T * psi * | I  0 |* x ]
	if (regressionData_.getCovariates()->rows() != 0) {
		q = regressionData_.getCovariates()->cols();
	}
	// For any realization we compute the degrees of freedom
	for (int i=0; i<nrealizations; ++i) {

		edf_vect(i) = uTpsi.row(i).dot(x.col(i).head(nnodes)) + q;
	}

    // Estimates: sample mean, sample variance
	Real mean = edf_vect.sum()/nrealizations;
	_dof(output_indexS,output_indexT) = mean;
}



template<typename InputHandler>
void MixedFERegressionBase<InputHandler>::computeDOFStochastic_iterative(UInt output_indexS, UInt output_indexT, Real lambdaS, Real lambdaT)
{
	UInt nlocations = regressionData_.getNumberofSpaceObservations();
    Real mean=0;
    Real q=0;

    // std::random_device rd;
    auto seed = std::chrono::system_clock::now().time_since_epoch().count();
    std::default_random_engine generator(seed);
    // Creation of the random matrix
    std::bernoulli_distribution distribution(0.5);
    UInt nrealizations = optimizationData_.get_nrealizations();
    MatrixXr u(M_*nlocations, nrealizations);
    for (int j=0; j<nrealizations; ++j) {
        for (int i=0; i<M_*nlocations; ++i) {
            if (distribution(generator)) {
                u(i,j) = 1.0;
            }
            else {
                u(i,j) = -1.0;
            }
        }
    }
    for(UInt k=0; k<M_; ++k){
        // Define the first right hand side : | I  0 |^T * psi^T * A * Q * u
        psi_mini = psi_.block(k * nlocations, k* N_, nlocations, N_);
        MatrixXr b = MatrixXr::Zero(2*N_,u.cols());
					MatrixXr Qu=LeftMultiplybyQ(u);
        if (regressionData_.getNumberOfRegions() == 0){
            b.topRows(N_) = psi_mini.transpose()* Qu.block(k*nlocations,0,nlocations,u.cols());
        }else{

            VectorXr miniA_  = A_.segment(0, regressionData_.getNumberOfRegions());
            b.topRows(N_) =  psi_mini.transpose()*(miniA_.asDiagonal() * Qu.block(k*nlocations,0,nlocations,u.cols()));
        }
        // Resolution of the system
        MatrixXr x;
        if (regressionData_.getCovariates()->rows() == 0)
            x = this->template system_solve(b);
        else
            x = this->template solve_covariates_iter(b,k);

        MatrixXr uTpsi;
        if (regressionData_.getNumberOfRegions() == 0){
					MatrixXr ut=u.transpose();
            uTpsi = (ut.block(0,k*nlocations,nrealizations,nlocations))*psi_mini;
        }else{

						MatrixXr ut=u.transpose();
            uTpsi = (ut.block(0,k*regressionData_.getNumberOfRegions(),nrealizations,regressionData_.getNumberOfRegions()))*psi_mini;
        }

        VectorXr edf_vect(nrealizations);

        // Degrees of freedom = q + E[ u^T * psi * | I  0 |* x ]
        if (regressionData_.getCovariates()->rows() != 0) {
            q = regressionData_.getCovariates()->cols();
        }

        // Degrees of freedom = q + E[ u^T * psi * | I  0 |* x ]
        // For any realization we compute the degrees of freedom
        for (UInt i=0; i<nrealizations; ++i) {

            edf_vect(i) = uTpsi.row(i).dot(x.col(i).head(N_));
        }

        // Estimates: sample mean, sample variance
        mean += edf_vect.sum()/nrealizations;
      }
	_dof(output_indexS,output_indexT) = mean + q;
}


template<typename InputHandler>
template<UInt ORDER, UInt mydim, UInt ndim, typename A>
void MixedFERegressionBase<InputHandler>::preapply(EOExpr<A> oper, const ForcingTerm & u, const MeshHandler<ORDER, mydim, ndim> & mesh_)
{
	const auto * Wp = regressionData_.getCovariates();

	UInt nnodes = N_*M_;	// total number of spatio-temporal nodes
	FiniteElement<ORDER, mydim, ndim> fe;
	// Set Areal data if present and no already done
	if(regressionData_.getNumberOfRegions()>0 && !isAComputed)
	{
		this->template setA<ORDER, mydim, ndim>(mesh_);
		isAComputed = true;
	}
	// Set psi matrix if not already done
	if(!isPsiComputed){
		this->template setPsi<ORDER, mydim, ndim>(mesh_);
		isPsiComputed = true;
	}
    psi_mini = psi_;
	// If there are covariates in the model set H and Q
	if(Wp->rows() != 0)
	{
		setH();
		setQ();
	}

	typedef EOExpr<Mass> ETMass; Mass EMass; ETMass mass(EMass);
	if(!isR1Computed)
	{
		Assembler::operKernel(oper, mesh_, fe, R1_);
		isR1Computed = true;
	}
	if(!isR0Computed)
	{
		Assembler::operKernel(mass, mesh_, fe, R0_);
		isR0Computed = true;
	}
	if(this->isSpaceVarying)
	{
		Assembler::forcingTerm(mesh_, fe, u, rhs_ft_correction_);
	}

	if(!regressionData_.getFlagIterative())
		if(regressionData_.isSpaceTime() || regressionData_.isMixed() )
			this->buildSpaceTimeMatrices();

	// Set final transpose of Psi matrix
	setpsi_t_();
	// Set matrix DMat for all cases
	setDMat();
	// Set the matrix needed for the iterative method
	if(regressionData_.getFlagIterative())
        buildSpaceTimeMatrices_iterative();

	// Define right hand data [rhs]
	VectorXr rightHandData;
	getRightHandData(rightHandData); //updated
	this->_rightHandSide = VectorXr::Zero(2*nnodes);
	this->_rightHandSide.topRows(nnodes)=rightHandData;
}

//----------------------------------------------------------------------------//
// Composed operations

// To be general, it takes in input the value of lambda
template<typename InputHandler>
void MixedFERegressionBase<InputHandler>::buildSystemMatrixNoCov(Real lambda_S)
{
        this->R1_lambda = (-lambda_S)*(R1_);
        this->R0_lambda = (-lambda_S)*(R0_);

        this->buildMatrixNoCov(this->DMat_, this->R1_lambda, this->R0_lambda);
}


template<typename InputHandler>
void MixedFERegressionBase<InputHandler>::buildSystemMatrixNoCov(Real lambdaS, Real lambdaT)
{

    this->R0_lambda = (-lambdaS) * R0_; // build the SouthEast block of the matrix
    this->R1_lambda = (-lambdaS) * R1_;
    // Update the SouthWest block of the matrix (also the NorthEast block transposed) if parabolic
    // distinguishing between iterative and monolithic method
    if (regressionData_.isSpaceTime() && regressionData_.getFlagParabolic() && !regressionData_.getFlagIterative())
    {
        this->R1_lambda -= lambdaS * (lambdaT * LR0k_);
    }
    if (regressionData_.isSpaceTime() && regressionData_.getFlagParabolic() && regressionData_.getFlagIterative())
    {
        //Recall: with the iterative method the  matrix of the systems have dimension 2N_*2N_ i
        Real delta = mesh_time_[1] - mesh_time_[0];
        this->R1_lambda = (lambdaS) * R1_ - (lambdaT / delta) * R0_lambda;
    }

    // Update NorthWest block of matrix if separable problem
    if (regressionData_.isSpaceTime() && !regressionData_.getFlagParabolic())
    {
        this->buildMatrixNoCov(this->DMat_ + lambdaT * Ptk_, R1_lambda, R0_lambda);
    }
    else
    {
        this->buildMatrixNoCov(this->DMat_, R1_lambda, R0_lambda);
    }
}

//----------------------------------------------------------------------------//
// Public solvers
template<typename InputHandler>
MatrixXr MixedFERegressionBase<InputHandler>::apply_to_b(const MatrixXr & b)
{
	const Real last_lambda = optimizationData_.get_last_lS_used();
	const Real lambda_ = optimizationData_.get_current_lambdaS();
        if(lambda_ != last_lambda)
        {
                this->buildSystemMatrixNoCov(lambda_);

		if(regressionData_.getDirichletIndices()->size() > 0)  // if areal data NO BOUNDARY CONDITIONS
		{
			this->addDirichletBC_matrix();
		}

                this->system_factorize();
        }

	optimizationData_.set_last_lS_used(lambda_);

        return this->template system_solve(b);
}


template<typename InputHandler>
MatrixXv  MixedFERegressionBase<InputHandler>::apply(void)
{
	UInt nnodes = N_*M_; // Define nuber of nodes
	const VectorXr * obsp = regressionData_.getObservations(); // Get observations

	UInt sizeLambdaS;
	if (!regressionData_.isSpaceTime() && !isGAMData &&!regressionData_.isMixed())
	   sizeLambdaS=1;
	else
	   sizeLambdaS = optimizationData_.get_size_S();
	UInt sizeLambdaT = optimizationData_.get_size_T();

	this->_solution.resize(sizeLambdaS,sizeLambdaT);
	this->_dof.resize(sizeLambdaS,sizeLambdaT);
	this->_GCV.resize(sizeLambdaS,sizeLambdaT);
	if(regressionData_.getCovariates()->rows() != 0)
	{
		this->_beta.resize(sizeLambdaS,sizeLambdaT);
	}

	VectorXr rhs = _rightHandSide; // Save rhs for modification

	for(UInt s=0; s<sizeLambdaS; ++s)
	{
		for(UInt t=0; t<sizeLambdaT; ++t)
		{
                        Real lambdaS;
			if(!regressionData_.isSpaceTime() && !isGAMData && !regressionData_.isMixed()) //at the moment only space is implemented
				{
					lambdaS = optimizationData_.get_current_lambdaS();
				}
			else
			 	lambdaS = (optimizationData_.get_lambda_S())[s];

			Real lambdaT = (optimizationData_.get_lambda_T())[t];
			_rightHandSide = rhs;

			if(isGAMData || regressionData_.isSpaceTime() || optimizationData_.get_current_lambdaS()!=optimizationData_.get_last_lS_used() || regressionData_.isMixed())
			{
				if(!regressionData_.isSpaceTime())
				{
					buildSystemMatrixNoCov(lambdaS);
				}
                        	else
				{
					buildSystemMatrixNoCov(lambdaS, lambdaT);
				}
			}

			// Right-hand side correction for space varying PDEs
			if(this->isSpaceVarying)
			{
			    _rightHandSide.bottomRows(nnodes)= (lambdaS)*rhs_ft_correction_;
			}

			// Right-hand side correction for initial condition in parabolic case
			if(regressionData_.isSpaceTime() && regressionData_.getFlagParabolic())
			{
				for(UInt i=0; i<regressionData_.getInitialValues()->rows(); i++)
				{
					_rightHandSide(nnodes+i) += lambdaS*lambdaT* rhs_ic_correction_(i);
				}
			}

			// Applying boundary conditions if necessary
			if(regressionData_.getDirichletIndices()->size() != 0)  // if areal data NO BOUNDARY CONDITIONS
				addDirichletBC();


			//f Factorization of the system for woodbury decomposition
			if(isGAMData || regressionData_.isSpaceTime() || optimizationData_.get_current_lambdaS()!=optimizationData_.get_last_lS_used() || regressionData_.isMixed())
			{
				system_factorize();
			}

			// system solution
			_solution(s,t) = this->template system_solve(this->_rightHandSide);


			if(optimizationData_.get_loss_function()=="GCV" && (!isGAMData && (regressionData_.isSpaceTime() || regressionData_.isMixed())))
			{
				if (optimizationData_.get_DOF_evaluation()!="not_required")
				{
					computeDegreesOfFreedom(s,t,lambdaS,lambdaT);
				}
				computeGeneralizedCrossValidation(s,t,lambdaS,lambdaT);
			}
			else
			{
				_dof(s,t) = -1;
				_GCV(s,t) = -1;
			}

			// regression coefficients computation
			if(regressionData_.getCovariates()->rows()!=0)
			{
				const auto & W(*(this->regressionData_.getCovariates()));
				VectorXr P(*(this->regressionData_.getWeightsMatrix()));
				VectorXr beta_rhs;
				if( P.size() != 0)
				{
					beta_rhs = W.transpose()*P.asDiagonal()*(*obsp - psi_*_solution(s,t).topRows(psi_.cols()));
				}
				else
				{
					beta_rhs = W.transpose()*(*obsp - psi_*_solution(s,t).topRows(psi_.cols()));
				}
				_beta(s,t) = WTW_.solve(beta_rhs);
			}
		}
	}
	if(!(isGAMData||regressionData_.isSpaceTime()||regressionData_.isMixed()) && optimizationData_.get_current_lambdaS()!=optimizationData_.get_last_lS_used())
	{
		optimizationData_.set_last_lS_used(optimizationData_.get_current_lambdaS());
	}
	_rightHandSide = rhs; // Return rhs to original status for next apply call

	//std::cout<<_solution(0,0).size()<<std::endl; per il caso GCV semplice la solution è il valore in posizione 0,0
	return this->_solution;
}

//Iterative method for Space-Time problems
template<typename InputHandler>
MatrixXv  MixedFERegressionBase<InputHandler>::apply_iterative(void) 
{
    UInt nnodes = N_ * M_; // Define number of space-times nodes
	if (regressionData_.isSpaceTime()){
    	Real delta = mesh_time_[1] - mesh_time_[0]; // Time interval
	}
    const VectorXr *obsp = regressionData_.getObservations(); // Get observations
    UInt nlocations = regressionData_.getNumberofSpaceObservations();

    UInt sizeLambdaS = optimizationData_.get_size_S();
    if (!regressionData_.isSpaceTime() && !isGAMData &&!regressionData_.isMixed())
        sizeLambdaS=1;
    else
        sizeLambdaS = optimizationData_.get_size_S();
    UInt sizeLambdaT = optimizationData_.get_size_T();

    this->_solution.resize(sizeLambdaS, sizeLambdaT);
    this->_dof.resize(sizeLambdaS,sizeLambdaT);
    this->_GCV.resize(sizeLambdaS,sizeLambdaT);
    if(regressionData_.getCovariates()->rows() != 0)
    {
        this->_beta.resize(sizeLambdaS,sizeLambdaT);
    }

    _solution_k_.resize(2 * N_, 1);  // inside the loop over i, it saves the solution for each time instant k
    _rightHandSide_k_.resize(2 * N_); // inside the loop over i, it saves the right hand side term of the system at each time istant k
    _solution_f_old_.resize(nnodes);  // stores f needed to update the rhs term

    const VectorXr rhs = _rightHandSide; // Save rhs for modification

    for (UInt s = 0; s < sizeLambdaS; ++s) {
        for (UInt t = 0; t < sizeLambdaT; ++t) {

            Real  J = 0, J_old = 10 ^(18);
			VectorXr residual(2 * N_ * M_), residual_k(2 * N_), preconditioned_residual(2 * N_ * M_);

            _solution(s, t) = VectorXr::Zero(2 * nnodes);
            Real lambdaS = (optimizationData_.get_lambda_S())[s];
            Real lambdaT = (optimizationData_.get_lambda_T())[t];

            _rightHandSide = rhs;
			if (regressionData_.isSpaceTime())
			{
				for (UInt i = 0; i < regressionData_.getInitialValues()->rows(); i++)  // p
				{
					_rightHandSide(nnodes + i) = lambdaS * lambdaT * rhs_ic_correction_(i);
				}
			}
            // Right-hand side correction for space varying PDEs
            if(this->isSpaceVarying)
            {
                _rightHandSide.bottomRows(nnodes)+= (lambdaS)*rhs_ft_correction_;
            }

            const SpMat psi_temp_mini=psi_mini;
            // (i=0) Solution Initialization: f^{k,0} (Solving a only space problem)
            // Inside this function it also calls buildsystemmatrixnocov that initialize R0_lambda, R1_lambda (might be modified by initialize_g too), and system factorize
            initialize_f(lambdaS, lambdaT, s,t);            

            _solution_f_old_ = _solution(s, t).topRows(nnodes);

            if (regressionData_.getObservationsNA()->size() == 0 && !regressionData_.isMixed()) {
                buildSystemMatrixNoCov(lambdaS, lambdaT);
                // Applying boundary conditions if necessary
                if (regressionData_.getDirichletIndices()->size() != 0)
                    addDirichletBC();
                system_factorize();
            }


            if (regressionData_.getCovariates()->rows() != 0) {
                const auto & W(*(this->regressionData_.getCovariates()));
                VectorXr tmp;
                tmp = W.transpose() * (*obsp - psi_ * _solution(s, t).topRows(psi_.cols()));
                _beta(s, t) = WTW_.solve(tmp);
            }
            psi_mini = psi_temp_mini;
			residual = compute_residual (_solution(s, t), lambdaS, lambdaT);
			Real normalizing_factor = _rightHandSide.norm();
            J = compute_J(s,t);
            UInt i = 1;
            while (stopping_criterion(i, J, J_old, residual, normalizing_factor))
            {
                for (UInt k = 0; k < M_; ++k) {

                    if (regressionData_.getObservationsNA()->size()!= 0 && !regressionData_.isMixed())
                    //modifying psi_mini to take care of missing values
                    {
                        psi_mini = psi_.block(k * nlocations, k* N_, nlocations, N_);
                        DMat_ = psi_mini.transpose()* psi_mini;
                        buildSystemMatrixNoCov(lambdaS, lambdaT);
                        if (regressionData_.getDirichletIndices()->size() != 0)
                            addDirichletBC();
                        system_factorize();
                    }

					residual_k.head(N_) = residual.segment( k * N_ , N_);
					residual_k.tail(N_) = residual.segment( N_ * M_ + k * N_, N_);

					// Solve residual
                    if (regressionData_.getCovariates()->rows() == 0){
                    _solution_k_ = this->template system_solve(residual_k);}
                    else{
                    _solution_k_ = this->template solve_covariates_iter(residual_k ,k);}

					
                    //Store the solution fˆ{k,i}, gˆ{k,i} in _solution(s,t)
                    _solution(s, t).segment(k * N_, N_) -= _solution_k_.topRows(N_);
                    _solution(s, t).segment(nnodes + k * N_, N_) -= _solution_k_.bottomRows(N_);
					//Store solution_k for next residual
					preconditioned_residual.segment(k * N_, N_) = _solution_k_.topRows(N_);
					preconditioned_residual.segment(nnodes + k * N_, N_) = _solution_k_.bottomRows(N_);
                }

				// Update the residual
				residual -= LeftMultiplyByMonolithic_iterative ( preconditioned_residual , lambdaS, lambdaT);

                _solution_f_old_ = _solution(s, t).topRows(nnodes);  // store f to update the rhs during the iterations

                // covariates computation
                if (regressionData_.getCovariates()->rows() != 0) {
                    const auto & W(*(this->regressionData_.getCovariates()));
                    VectorXr beta_rhs;
                    beta_rhs = W.transpose() * (*obsp - psi_ * _solution(s, t).topRows(psi_.cols()));
                    _beta(s, t) = WTW_.solve(beta_rhs);
                }

                J_old = J;
                psi_mini = psi_temp_mini;
                J = compute_J(s,t) ;
                i++;
            }

            Rprintf("Solution found after %d iterations (max number of iterations: %d)\n", i, (regressionData_.get_maxiter()+1));

            if(optimizationData_.get_loss_function()=="GCV" && (!isGAMData&&(regressionData_.isSpaceTime() || regressionData_.isMixed())))
            {
                if (optimizationData_.get_DOF_evaluation()!="not_required")
                {
                    computeDegreesOfFreedom(s,t,lambdaS,lambdaT);
                }
                computeGeneralizedCrossValidation(s,t,lambdaS,lambdaT);
            }
            else
            {
                _dof(s,t) = -1;
                _GCV(s,t) = -1;
            }
        }
    }
    _rightHandSide = rhs;

    return this->_solution;
}

//---- ITERATIVE METHOD PART------
template<typename InputHandler>
void MixedFERegressionBase<InputHandler>::initialize_f(Real lambdaS, Real lambdaT, UInt& lambdaS_index, UInt& lambdaT_index)
{
    UInt nnodes = N_ * M_; // Define number of space-times nodes
    UInt nlocations = regressionData_.getNumberofSpaceObservations();
    if (regressionData_.getObservationsNA()->size() == 0) {
        buildSystemMatrixNoCov(lambdaS);
        // Applying boundary conditions if necessary
        if (regressionData_.getDirichletIndices()->size() != 0)
            addDirichletBC();
        system_factorize();
    }

    for (UInt k = 0; k < M_; ++k) 
	{ //loop over time istants

        if (regressionData_.getObservationsNA()->size()!= 0)
            //modifying psi_mini to take care of missing values
        {
            psi_mini = psi_.block(k * nlocations, k* N_, nlocations, N_);
            DMat_ = psi_mini.transpose()* psi_mini;
            buildSystemMatrixNoCov(lambdaS);
            if (regressionData_.getDirichletIndices()->size() != 0)
                addDirichletBC();
            system_factorize();
        }

        _rightHandSide_k_.topRows(N_) = _rightHandSide.segment(k * N_, N_);  //setting the right hand side of the system
        _rightHandSide_k_.bottomRows(N_) = _rightHandSide.segment(nnodes + (k * N_), N_);
        if (regressionData_.getCovariates()->rows() == 0)
            _solution_k_ = this->template system_solve(_rightHandSide_k_);
        else
            _solution_k_ = this->template solve_covariates_iter(_rightHandSide_k_,k);
        
		_solution(lambdaS_index, lambdaT_index).segment(k * N_, N_) = _solution_k_.topRows(N_); // saving f^{k,0}
		
		if (regressionData_.isMixed())
			_solution(lambdaS_index, lambdaT_index).segment(nnodes + k * N_, N_) = _solution_k_.bottomRows(N_);
    }
	if (!regressionData_.isMixed())
		initialize_g(lambdaS,lambdaT, lambdaS_index, lambdaT_index);
}

template<typename InputHandler>
void MixedFERegressionBase<InputHandler>::initialize_g(Real lambdaS, Real lambdaT, UInt& lambdaS_index, UInt& lambdaT_index) {
    // (backward in time)
    UInt nnodes = N_ * M_; // Define number of space-times nodes
    Real delta = mesh_time_[1] - mesh_time_[0]; // Time interval
    Eigen::SparseLU<SpMat> Matdec_;
    UInt nlocations = regressionData_.getNumberofSpaceObservations();

    VectorXr rhs_k = VectorXr::Zero(N_);
    this->R1_lambda = (lambdaS) * R1_ + (lambdaT * lambdaS / delta) * R0_;
    Matdec_.compute(R1_lambda);
    for (UInt k = M_; k > 0; --k) {
        if (regressionData_.getObservationsNA()->size()!= 0)
            //modifying psi_mini to take care of missing values
        {
            psi_mini = psi_.block((k - 1) * nlocations, (k - 1) * N_, nlocations, N_);
            DMat_ = psi_mini.transpose()* psi_mini;
        }

        if (regressionData_.getCovariates()->rows() != 0) {
            MatrixXr H_k_;
            H_k_ = H_.block((k-1) * nlocations, (k-1) * nlocations, nlocations, nlocations);
            MatrixXr Cov_block = psi_mini.transpose() * (H_k_) * psi_mini;
            SpMat spCov_block;
            spCov_block = Cov_block.sparseView();
            if (k == M_)
                rhs_k = _rightHandSide.segment((k - 1) * N_, N_) -
                        (DMat_ - spCov_block)  * _solution(lambdaS_index, lambdaT_index).segment((k - 1) * N_, N_);
            else
                rhs_k = _rightHandSide.segment((k - 1) * N_, N_) -
                        (DMat_- spCov_block) * _solution(lambdaS_index, lambdaT_index).segment((k - 1) * N_, N_) +
                        (lambdaT / delta) * R0_lambda * _solution(lambdaS_index, lambdaT_index).segment(nnodes + (k * N_), N_);
        }

        else{
            if (k == M_)
                rhs_k = _rightHandSide.segment((k - 1) * N_, N_) -
                        DMat_ * _solution(lambdaS_index, lambdaT_index).segment((k - 1) * N_, N_);
            else
                rhs_k = _rightHandSide.segment((k - 1) * N_, N_) -
                        DMat_ * _solution(lambdaS_index, lambdaT_index).segment((k - 1) * N_, N_) +
                        (lambdaT / delta) * R0_lambda * _solution(lambdaS_index, lambdaT_index).segment(nnodes + (k * N_), N_);
        }
        _solution(lambdaS_index, lambdaT_index).segment(nnodes + (k - 1) * N_, N_) = Matdec_.solve(rhs_k);
    }

}


template<typename InputHandler>
void MixedFERegressionBase<InputHandler>::update_rhs(UInt& time_index, Real lambdaS, Real lambdaT, UInt& lambdaS_index, UInt& lambdaT_index){

    UInt nnodes = N_ * M_; // Define number of space-times nodes
	if (regressionData_.isSpaceTime())
	{
		Real delta = mesh_time_[1] - mesh_time_[0]; // Time interval

		if (time_index == (M_ - 1))
			_rightHandSide_k_.topRows(N_) = _rightHandSide.segment(time_index * N_, N_);
		else
			_rightHandSide_k_.topRows(N_) = _rightHandSide.segment(time_index * N_, N_) +
											(lambdaT / delta) * R0_lambda *
												_solution(lambdaS_index, lambdaT_index).segment(nnodes + (time_index + 1) * N_, N_);

		if (time_index == 0)
			_rightHandSide_k_.bottomRows(N_) = _rightHandSide.segment(nnodes + (time_index * N_), N_);
		else
			_rightHandSide_k_.bottomRows(N_) = _rightHandSide.segment(nnodes + (time_index * N_), N_) +
											   ((lambdaT / delta) * R0_lambda *
												_solution_f_old_.segment((time_index - 1) * N_, N_));
	}
	else if (regressionData_.isMixed())
	{
		//changed approach in the end
	}
}

template<typename InputHandler>
bool MixedFERegressionBase<InputHandler>::stopping_criterion(UInt& index, Real J, Real J_old, const VectorXr & r, Real normalizing_factor ) const {
    // return true if the iterative method has to perform another iteration, false if it has to be stopped

    bool do_stop_by_iteration = false;  // Do I need to stop becouse n_it > n_max?
    bool do_stop_by_treshold = false; // Do I need to stop becouse |{J(i) - J(i-1)}/J(i)|< treshold?
	bool do_stop_by_residual = false;

    if(index > regressionData_.get_maxiter()){
        do_stop_by_iteration = true;
    }

	if( (r.norm() / normalizing_factor) < regressionData_.get_treshold_residual()){
        do_stop_by_residual = true;
    }


    if(abs((J - J_old) / J) < regressionData_.get_treshold()){
			do_stop_by_treshold = true;
        }

    return !(do_stop_by_iteration || (do_stop_by_treshold && do_stop_by_residual));
}

template<typename InputHandler>
VectorXr MixedFERegressionBase<InputHandler>::LeftMultiplyByMonolithic_iterative(const VectorXr & v, Real lambdaS, Real lambdaT)
{
	VectorXr res = VectorXr::Zero (2*N_*M_);
	Real delta;
	MatrixXr U_k;
	if (regressionData_.isSpaceTime()) delta = mesh_time_[1] - mesh_time_[0];
	
	// upper part
	for (UInt k = 0; k < M_; ++k)
	{
		res.segment(k * N_, N_) = matrixNoCov_.topLeftCorner(N_, N_) * v.segment(k * N_, N_);

		res.segment(k * N_, N_) += matrixNoCov_.topRightCorner(N_, N_) * v.segment(N_ * M_ + k * N_, N_); 

		if (regressionData_.isSpaceTime() && regressionData_.getFlagParabolic() && k != M_ - 1)
		{
			res.segment(k * N_, N_) += (lambdaT / delta) * R0_lambda * v.segment(N_ * M_ + (k + 1) * N_, N_);
		}

		if (regressionData_.getCovariates()->rows() != 0)
		{
			U_k = U_.block(k * N_, 0, N_, U_.cols());
			res.segment(k * N_, N_) += -U_k * WTW_.solve( V_ * v.topRows(N_ * M_) );
		}
	}

	// lower part
	for (UInt k = 0; k < M_; ++k)
	{
		res.segment(N_ * M_ + k * N_, N_) = matrixNoCov_.bottomLeftCorner(N_ , N_) * v.segment(k * N_, N_) + matrixNoCov_.bottomRightCorner(N_ , N_) * v.segment(N_ * M_ + k * N_, N_);

		if (regressionData_.isSpaceTime() && regressionData_.getFlagParabolic() && k != 0)
		{
			res.segment(N_ * M_ + k * N_, N_) += (lambdaT / delta) * R0_lambda * v.segment(( k - 1 ) * N_, N_) ;
		}
	}

	return res;
}

template <typename InputHandler>
Real MixedFERegressionBase<InputHandler>::compute_J(UInt &lambdaS_index, UInt &lambdaT_index)
{
	Real J_MSE = 0;
	Real J_reg = 0;
	Real lambdaS = (optimizationData_.get_lambda_S())[lambdaS_index];
	UInt nlocations = regressionData_.getNumberofSpaceObservations();
	const VectorXr *obsp = regressionData_.getObservations();
	const auto & W(*(this->regressionData_.getCovariates()));
	MatrixXr W_k_;
	for (UInt k = 0; k < M_; ++k)
	{
		psi_mini = psi_.block(k * nlocations, k * N_, nlocations, N_);
		VectorXr temp  = (*obsp).segment(k * nlocations,nlocations) - psi_mini * _solution(lambdaS_index, lambdaT_index).segment(k * N_, N_);
		if (regressionData_.getCovariates()->rows() != 0)
		{
			W_k_ = W.block(k * nlocations, 0, nlocations, W.cols());
			temp -= W_k_ * _beta(lambdaS_index, lambdaT_index);
		}
		J_MSE += temp.squaredNorm();
        J_reg += lambdaS * (_solution(lambdaS_index, lambdaT_index).segment(N_*M_ + k  * N_, N_)).transpose() * R0_ * _solution(lambdaS_index, lambdaT_index).segment(N_*M_ + k  * N_, N_);
	}
	return (J_MSE + J_reg);
}

//----------------------------------------------------------------------------//

template<typename MatrixType>
class MixedFERegression<RegressionData<MatrixType>>: public MixedFERegressionBase<RegressionData<MatrixType>>
{
	public:
		MixedFERegression(const RegressionData<MatrixType> & regressionData,  OptimizationData & optimizationData, UInt nnodes_):
			MixedFERegressionBase<RegressionData<MatrixType>>(regressionData, optimizationData, nnodes_) {};
		MixedFERegression(const std::vector<Real> & mesh_time, const RegressionData<> & regressionData, OptimizationData & optimizationData, UInt nnodes_):
			MixedFERegressionBase<RegressionData<MatrixType>>(mesh_time, regressionData, optimizationData, nnodes_) {};

		template<UInt ORDER, UInt mydim, UInt ndim>
		void preapply(const MeshHandler<ORDER,mydim,ndim> & mesh)
		{
			typedef EOExpr<Stiff> ETStiff; Stiff EStiff; ETStiff stiff(EStiff);
		    MixedFERegressionBase<RegressionData<MatrixType>>::preapply(stiff, ForcingTerm(), mesh);
		}
};

template<>
class MixedFERegression<RegressionDataElliptic>: public MixedFERegressionBase<RegressionDataElliptic>
{
	public:
		MixedFERegression(const RegressionDataElliptic & regressionData,  OptimizationData & optimizationData, UInt nnodes_):
			MixedFERegressionBase<RegressionDataElliptic>(regressionData, optimizationData, nnodes_) {};
		MixedFERegression(const std::vector<Real> & mesh_time, const RegressionDataElliptic & regressionData,  OptimizationData & optimizationData, UInt nnodes_):
			MixedFERegressionBase<RegressionDataElliptic>(mesh_time, regressionData, optimizationData, nnodes_) {};

		template<UInt ORDER, UInt mydim, UInt ndim>
		void preapply(const MeshHandler<ORDER,mydim,ndim> & mesh)
		{
			typedef EOExpr<Mass>  ETMass;  Mass EMass;   ETMass mass(EMass);
			typedef EOExpr<Stiff> ETStiff; Stiff EStiff; ETStiff stiff(EStiff);
			typedef EOExpr<Grad>  ETGrad;  Grad EGrad;   ETGrad grad(EGrad);

	  		const Real& c = this->regressionData_.getC();
	  		const Diffusion<PDEParameterOptions::Constant>& K = this->regressionData_.getK();
	  		const Advection<PDEParameterOptions::Constant>& b = this->regressionData_.getBeta();

			MixedFERegressionBase<RegressionDataElliptic>::preapply(c*mass+stiff[K]+b.dot(grad), ForcingTerm(), mesh);

		}
};

template<>
class MixedFERegression<RegressionDataEllipticSpaceVarying> : public MixedFERegressionBase<RegressionDataEllipticSpaceVarying>
{
	public:
		MixedFERegression(const RegressionDataEllipticSpaceVarying & regressionData,  OptimizationData & optimizationData, UInt nnodes_):
			MixedFERegressionBase<RegressionDataEllipticSpaceVarying>(regressionData, optimizationData, nnodes_) {};
		MixedFERegression(const std::vector<Real> & mesh_time, const RegressionDataEllipticSpaceVarying & regressionData,  OptimizationData & optimizationData, UInt nnodes_):
			MixedFERegressionBase<RegressionDataEllipticSpaceVarying>(mesh_time, regressionData, optimizationData, nnodes_) {};


		template< UInt ORDER, UInt mydim, UInt ndim>
		void preapply(const MeshHandler<ORDER,mydim,ndim> & mesh)
		{
			typedef EOExpr<Mass>  ETMass;  Mass EMass;   ETMass mass(EMass);
			typedef EOExpr<Stiff> ETStiff; Stiff EStiff; ETStiff stiff(EStiff);
			typedef EOExpr<Grad>  ETGrad;  Grad EGrad;   ETGrad grad(EGrad);

			const Reaction& c = this->regressionData_.getC();
			const Diffusion<PDEParameterOptions::SpaceVarying>& K = this->regressionData_.getK();
			const Advection<PDEParameterOptions::SpaceVarying>& b = this->regressionData_.getBeta();
			const ForcingTerm& u= this->regressionData_.getU();

			this->isSpaceVarying = TRUE;

			MixedFERegressionBase<RegressionDataEllipticSpaceVarying>::preapply(c*mass+stiff[K]+b.dot(grad), u, mesh);

		}
};

// -- TEMPORAL PART --
template<typename InputHandler>
void MixedSplineRegression<InputHandler>::setPhi(void)
{

	Spline<SPLINE_DEGREE, ORDER_DERIVATIVE> spline(mesh_time_);
	UInt M = spline.num_knots()-SPLINE_DEGREE-1;
	UInt m = regressionData_.getNumberofTimeObservations();

	phi_.resize(m, M);
	Real value;

	for(UInt i=0; i<m; ++i)
	{
		for(UInt j=0; j<M; ++j)
		{
			value = spline.BasisFunction(j, this->regressionData_.getTimeLocations()[i]);
			if(value!=0)
			{
				phi_.coeffRef(i,j) = value;
			}
		}
	}
	phi_.makeCompressed();
}

template<typename InputHandler>
void MixedSplineRegression<InputHandler>::setTimeMass(void)
{
    Spline<SPLINE_DEGREE, 0> spline(mesh_time_);
    Assembler::operKernel(spline, timeMass_);
}

template<typename InputHandler>
void MixedSplineRegression<InputHandler>::smoothSecondDerivative(void)
{
    Spline<SPLINE_DEGREE, ORDER_DERIVATIVE> spline(mesh_time_);
    Assembler::operKernel(spline, Pt_);
}

// Parabolic
template<typename InputHandler>
void MixedFDRegression<InputHandler>::setDerOperator(void)
{
	// Build the matrix of finite differences [1/dt1 0 .......... 0]																	 [0 ....0 -1/dtM 1/dtM]
	UInt M = mesh_time_.size()-1;
	derOpL_.resize(M, M);

	// Set the first and the last rows
	Real delta = mesh_time_[1] - mesh_time_[0];
	derOpL_.coeffRef(0,0) = 1/delta;

	delta = mesh_time_[M-1] - mesh_time_[M-2];
	derOpL_.coeffRef(M-1,M-1) = 1/delta;
	derOpL_.coeffRef(M-1,M-2) = -1/delta;

	for(UInt i=1; i<M-1; ++i)
	{
		delta = mesh_time_[i] - mesh_time_[i-1];
		derOpL_.coeffRef(i,i-1) = -1/delta;
		derOpL_.coeffRef(i,i) 	= 1/delta;
	}

	derOpL_.makeCompressed();
}

// -- GAM PART --
template<>
class MixedFERegression<GAMDataLaplace>: public MixedFERegressionBase<RegressionData<>>
{
	public:
		MixedFERegression(const RegressionData<> & regressionData, OptimizationData & optimizationData, UInt nnodes_):
			MixedFERegressionBase<RegressionData<>>(regressionData, optimizationData, nnodes_) {};

		template<UInt ORDER, UInt mydim, UInt ndim>
		void preapply(const MeshHandler<ORDER,mydim,ndim> & mesh)
		{
			typedef EOExpr<Stiff> ETStiff; Stiff EStiff; ETStiff stiff(EStiff);
			MixedFERegressionBase<RegressionData<>>::preapply(stiff, ForcingTerm(), mesh);
		}
};

template<>
class MixedFERegression<GAMDataElliptic>: public MixedFERegressionBase<RegressionDataElliptic>
{
	public:
		MixedFERegression(const RegressionDataElliptic & regressionData, OptimizationData & optimizationData, UInt nnodes_):
			MixedFERegressionBase<RegressionDataElliptic>(regressionData, optimizationData, nnodes_) {};

		template< UInt ORDER, UInt mydim, UInt ndim>
		void preapply(const MeshHandler<ORDER,mydim,ndim> & mesh)
		{
			typedef EOExpr<Mass>  ETMass;  Mass EMass;   ETMass mass(EMass);
			typedef EOExpr<Stiff> ETStiff; Stiff EStiff; ETStiff stiff(EStiff);
			typedef EOExpr<Grad>  ETGrad;  Grad EGrad;   ETGrad grad(EGrad);

	  		const Real& c = this->regressionData_.getC();
	  		const Diffusion<PDEParameterOptions::Constant>& K = this->regressionData_.getK();
	  		const Advection<PDEParameterOptions::Constant>& b = this->regressionData_.getBeta();

			MixedFERegressionBase<RegressionDataElliptic>::preapply(c*mass+stiff[K]+b.dot(grad), ForcingTerm(), mesh);

		}
};

template<>
class MixedFERegression<GAMDataEllipticSpaceVarying>: public MixedFERegressionBase<RegressionDataEllipticSpaceVarying>
{
	public:
		MixedFERegression(const RegressionDataEllipticSpaceVarying & regressionData, OptimizationData & optimizationData, UInt nnodes_):
			MixedFERegressionBase<RegressionDataEllipticSpaceVarying>( regressionData, optimizationData, nnodes_) {};

		template<UInt ORDER, UInt mydim, UInt ndim>
		void preapply(const MeshHandler<ORDER,mydim,ndim> & mesh)
		{
			typedef EOExpr<Mass>  ETMass;  Mass EMass;   ETMass mass(EMass);
			typedef EOExpr<Stiff> ETStiff; Stiff EStiff; ETStiff stiff(EStiff);
			typedef EOExpr<Grad>  ETGrad;  Grad EGrad;   ETGrad grad(EGrad);

			const Reaction& c = this->regressionData_.getC();
			const Diffusion<PDEParameterOptions::SpaceVarying>& K = this->regressionData_.getK();
			const Advection<PDEParameterOptions::SpaceVarying>& b = this->regressionData_.getBeta();
			const ForcingTerm& u= this->regressionData_.getU();

			this->isSpaceVarying = TRUE;

			MixedFERegressionBase<RegressionDataEllipticSpaceVarying>::preapply(c*mass+stiff[K]+b.dot(grad), u, mesh);

		}
};

#endif
