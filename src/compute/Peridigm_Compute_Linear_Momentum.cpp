/*! \file Peridigm_Compute_Linear_Momentum.cpp */

//@HEADER
// ************************************************************************
//
//                             Peridigm
//                 Copyright (2011) Sandia Corporation
//
// Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions?
// David J. Littlewood   djlittl@sandia.gov
// John A. Mitchell      jamitch@sandia.gov
// Michael L. Parks      parks@sandia.gov
// Stewart A. Silling    sasilli@sandia.gov
//
// ************************************************************************
//@HEADER

#include <vector>

#include "Peridigm_Compute_Linear_Momentum.hpp"
#include "../core/Peridigm.hpp"

//! Standard constructor.
PeridigmNS::Compute_Linear_Momentum::Compute_Linear_Momentum(PeridigmNS::Peridigm *peridigm_ ){peridigm = peridigm_;}

//! Destructor.
PeridigmNS::Compute_Linear_Momentum::~Compute_Linear_Momentum(){}


//! Returns the fieldspecs computed by this class
std::vector<Field_NS::FieldSpec> PeridigmNS::Compute_Linear_Momentum::getFieldSpecs() const 
{
  std::vector<Field_NS::FieldSpec> myFieldSpecs;

  return myFieldSpecs;
}

//! Perform computation
int PeridigmNS::Compute_Linear_Momentum::compute( Teuchos::RCP< std::vector<PeridigmNS::Block> > blocks  ) const
{
  return 0;
}
 
//! Fill the linear momentum vector
int PeridigmNS::Compute_Linear_Momentum::computeLinearMomentum( Teuchos::RCP< std::vector<PeridigmNS::Block> > blocks, bool storeLocal  ) const
{
  int retval;

  double globalLM = 0.0;
  Teuchos::RCP<Epetra_Vector> velocity, volume, linear_momentum;
  std::vector<PeridigmNS::Block>::iterator blockIt;
  for(blockIt = blocks->begin() ; blockIt != blocks->end() ; blockIt++)
  {
    Teuchos::RCP<PeridigmNS::DataManager> dataManager = blockIt->getDataManager();
    Teuchos::RCP<PeridigmNS::NeighborhoodData> neighborhoodData = blockIt->getNeighborhoodData();
    const int numOwnedPoints = neighborhoodData->NumOwnedPoints();
    const int* ownedIDs = neighborhoodData->OwnedIDs();

    velocity        = dataManager->getData(Field_NS::VELOC3D, Field_ENUM::STEP_NP1);
    volume          = dataManager->getData(Field_NS::VOLUME, Field_ENUM::STEP_NONE);
    linear_momentum = dataManager->getData(Field_NS::LINEAR_MOMENTUM3D, Field_ENUM::STEP_NP1);
	
    // Sanity check
    if ( (velocity->Map().NumMyElements() != volume->Map().NumMyElements()) )
    {
      retval = 1;
      return(retval);
    }

    *linear_momentum = *velocity;
 	
    // Collect values
    double *volume_values = volume->Values();
    double *velocity_values = velocity->Values();
    double *linear_momentum_values = linear_momentum->Values();	

    // Initialize linear momentum values
    double linear_momentum_x,  linear_momentum_y, linear_momentum_z;
    linear_momentum_x = linear_momentum_y = linear_momentum_z = 0.0;

    double density = blockIt->getMaterialModel()->Density();
    
    // volume is a scalar and force a vector, so maps are different; must do multiplication on per-element basis
    int numElements = numOwnedPoints;  	
    for (int i=0;i<numElements;i++) 
    {
      int ID = ownedIDs[i];
      double mass = density*volume_values[ID];
      double v1 = velocity_values[3*ID];
      double v2 = velocity_values[3*ID+1];
      double v3 = velocity_values[3*ID+2];
      linear_momentum_values[3*i] = mass*linear_momentum_values[3*i];
      linear_momentum_values[3*i+1] = mass*linear_momentum_values[3*i+1];
      linear_momentum_values[3*i+2] = mass*linear_momentum_values[3*i+2];
      if (storeLocal)
      {
        linear_momentum_x = linear_momentum_x + mass*v1;
        linear_momentum_y = linear_momentum_y + mass*v2;
        linear_momentum_z = linear_momentum_z + mass*v3;
      }
    }

    if (!storeLocal)
    {
      // Update info across processors
      std::vector<double> localLinearMomentum(3), globalLinearMomentum(3);
      localLinearMomentum[0] = linear_momentum_x;
      localLinearMomentum[1] = linear_momentum_y;
      localLinearMomentum[2] = linear_momentum_z;

      peridigm->getEpetraComm()->SumAll(&localLinearMomentum[0], &globalLinearMomentum[0], 3);
	
      globalLinearMomentum[0] = globalLinearMomentum[0];
      globalLinearMomentum[1] = globalLinearMomentum[1];
      globalLinearMomentum[2] = globalLinearMomentum[2];
      globalLM += sqrt(globalLinearMomentum[0]*globalLinearMomentum[0] + globalLinearMomentum[1]*globalLinearMomentum[1] + globalLinearMomentum[2]*globalLinearMomentum[2]);
    }
  }

/*
	if (peridigm->getEpetraComm()->MyPID() == 0)
	{
	std::cout << "Hello!" << std::endl;

	std::cout << "Total Linear Momentum =  " << "("  << globalLinearMomentum[0]
                                                 << ", " << globalLinearMomentum[1]
                                                 << ", " << globalLinearMomentum[2] << ")" << std::endl;
	}
*/

  // Store global energy in block (block globals are static, so only need to assign data to first block)
  if (!storeLocal)
    blocks->begin()->getScalarData(Field_NS::GLOBAL_LINEAR_MOMENTUM) = globalLM;
	
  return(0);

}
