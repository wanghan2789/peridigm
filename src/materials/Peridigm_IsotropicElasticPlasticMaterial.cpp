/*
 * Peridigm_IsotropicElasticPlasticMaterial.cxx
 *
 */
#include "Peridigm_IsotropicElasticPlasticMaterial.hpp"
#include "Peridigm_CriticalStretchDamageModel.hpp"
#include <Teuchos_TestForException.hpp>
#include "Epetra_Vector.h"
#include "Epetra_MultiVector.h"
#include "PdMaterialUtilities.h"
#include <limits>


PeridigmNS::IsotropicElasticPlasticMaterial::IsotropicElasticPlasticMaterial(const Teuchos::ParameterList & params)
:
Material(params),
m_decompStates(),
m_damageModel()
{

	m_decompStates.addScalarStateBondVariable("scalarPlasticExtensionState_N");
	m_decompStates.addScalarStateBondVariable("scalarPlasticExtensionState_NP1");
	m_decompStates.addScalarStateVariable("Lambda");

	//! \todo Add meaningful asserts on material properties.
	m_bulkModulus = params.get<double>("Bulk Modulus");
	m_shearModulus = params.get<double>("Shear Modulus");
	m_horizon = params.get<double>("Material Horizon");
	m_density = params.get<double>("Density");
	m_yieldStress = params.get<double>("Yield Stress");

	/*
	 * Set the yield stress to a very large value: this in effect makes the model run elastic -- useful for testing
	 */
	if(params.isType<string>("Test"))
		m_yieldStress = std::numeric_limits<double>::max( );

	if(params.isSublist("Damage Model")){
		Teuchos::ParameterList damageParams = params.sublist("Damage Model");
		if(!damageParams.isParameter("Type")){
			TEST_FOR_EXCEPTION(true, Teuchos::Exceptions::InvalidParameter,
					"Damage model \"Type\" not specified in Damage Model parameter list.");
		}
		string& damageModelType = damageParams.get<string>("Type");
		if(damageModelType == "Critical Stretch"){
			m_damageModel = Teuchos::rcp(new PeridigmNS::CriticalStretchDamageModel(damageParams));
		}
		else{
			TEST_FOR_EXCEPTION(true, Teuchos::Exceptions::InvalidParameter,
					"Invalid damage model, \"None\" or \"Critical Stretch\" required.");
		}
	}

    // set up vector of variable specs
    m_variableSpecs = Teuchos::rcp(new std::vector<Field_NS::FieldSpec>);
    m_variableSpecs->push_back(Field_NS::VOLUME);
    m_variableSpecs->push_back(Field_NS::DAMAGE);
    m_variableSpecs->push_back(Field_NS::WEIGHTED_VOLUME);
    m_variableSpecs->push_back(Field_NS::DILATATION);
    m_variableSpecs->push_back(Field_NS::COORD3D);
    m_variableSpecs->push_back(Field_NS::DISPL3D);
    m_variableSpecs->push_back(Field_NS::VELOC3D);
    m_variableSpecs->push_back(Field_NS::ACCEL3D);
    m_variableSpecs->push_back(Field_NS::FORCE3D);
    m_variableSpecs->push_back(Field_NS::DEVIATORIC_PLASTIC_EXTENSION);
    m_variableSpecs->push_back(Field_NS::LAMBDA);

}


PeridigmNS::IsotropicElasticPlasticMaterial::~IsotropicElasticPlasticMaterial()
{
}


void PeridigmNS::IsotropicElasticPlasticMaterial::initialize(const Epetra_Vector& x,
                                                             const Epetra_Vector& u,
                                                             const Epetra_Vector& v,
                                                             const double dt,
                                                             const Epetra_Vector& cellVolume,
                                                             const int numOwnedPoints,
                                                             const int* ownedIDs,
                                                             const int* neighborhoodList,
                                                             double* bondState,
                                                             PeridigmNS::DataManager& dataManager,
                                                             Epetra_MultiVector& vectorConstitutiveData,
                                                             Epetra_Vector& force) const
{

	 // Sanity checks on vector sizes
	  TEST_FOR_EXCEPT_MSG(x.MyLength() != u.MyLength(),
						  "x and u vector lengths do not match\n");
	  TEST_FOR_EXCEPT_MSG(x.MyLength() != v.MyLength(),
						  "x and v vector lengths do not match\n");
	  TEST_FOR_EXCEPT_MSG(x.MyLength() != vectorConstitutiveData.MyLength(),
						  "x and vector constitutive data vector lengths do not match\n");
	  TEST_FOR_EXCEPT_MSG(x.MyLength() != force.MyLength(),
						  "x and force vector lengths do not match\n");

	  //! \todo Create structure for storing influence function values.
//	  double omega = 1.0;

	  // Initialize data fields
	  vectorConstitutiveData.PutScalar(0.0);
	  force.PutScalar(0.0);
	  int neighborhoodListIndex = 0;
	  int bondStateIndex = 0;
	  for(int iID=0 ; iID<numOwnedPoints ; ++iID){
		int numNeighbors = neighborhoodList[neighborhoodListIndex++];
		for(int iNID=0 ; iNID<numNeighbors ; ++iNID){
		  bondState[bondStateIndex++] = 0.0;
	      neighborhoodListIndex++;
	    }
	  }

	  // Extract pointers to the underlying data
      double* weightedVolume;
      dataManager.getData(Field_NS::WEIGHTED_VOLUME, Field_NS::FieldSpec::STEP_NONE)->ExtractView(&weightedVolume);

	  PdMaterialUtilities::computeWeightedVolume(x.Values(),cellVolume.Values(),weightedVolume,numOwnedPoints,neighborhoodList);

}

void
PeridigmNS::IsotropicElasticPlasticMaterial::updateConstitutiveData(const Epetra_Vector& x,
                                                                    const Epetra_Vector& u,
                                                                    const Epetra_Vector& v,
                                                                    const double dt,
                                                                    const Epetra_Vector& cellVolume,
                                                                    const int numOwnedPoints,
                                                                    const int* ownedIDs,
                                                                    const int* neighborhoodList,
                                                                    double* bondState,
                                                                    PeridigmNS::DataManager& dataManager,
                                                                    Epetra_MultiVector& vectorConstitutiveData,
                                                                    Epetra_Vector& force) const
{

	// Extract pointers to the underlying data in the constitutiveData array
	double *dilatation, *damage, *weightedVolume;
        dataManager.getData(Field_NS::DILATATION, Field_NS::FieldSpec::STEP_NP1)->ExtractView(&dilatation);
        dataManager.getData(Field_NS::DAMAGE, Field_NS::FieldSpec::STEP_NP1)->ExtractView(&damage);
        dataManager.getData(Field_NS::WEIGHTED_VOLUME, Field_NS::FieldSpec::STEP_NONE)->ExtractView(&weightedVolume);

	std::pair<int,double*> vectorView = m_decompStates.extractStrideView(vectorConstitutiveData);
	double *y = m_decompStates.extractCurrentPositionView(vectorView);

	// Update the geometry
	PdMaterialUtilities::updateGeometry(x.Values(),u.Values(),v.Values(),y,x.MyLength(),dt);

	// Update the bondState
	if(!m_damageModel.is_null()){
		m_damageModel->computeDamage(x,
				u,
				v,
				dt,
				cellVolume,
				numOwnedPoints,
				ownedIDs,
				neighborhoodList,
				bondState,
				vectorConstitutiveData,
				force);
	}

	//  Update the element damage (percent of bonds broken)
	int neighborhoodListIndex = 0;
	int bondStateIndex = 0;
	for(int iID=0 ; iID<numOwnedPoints ; ++iID){
		int nodeID = ownedIDs[iID];
		int numNeighbors = neighborhoodList[neighborhoodListIndex++];
		neighborhoodListIndex += numNeighbors;
		double totalDamage = 0.0;
		for(int iNID=0 ; iNID<numNeighbors ; ++iNID){
			totalDamage += bondState[bondStateIndex++];
		}
		if(numNeighbors > 0)
			totalDamage /= numNeighbors;
		else
			totalDamage = 0.0;
		damage[nodeID] = totalDamage;
	}


	PdMaterialUtilities::computeDilatation(x.Values(),y,weightedVolume,cellVolume.Values(),bondState,dilatation,neighborhoodList,numOwnedPoints);
}

void
PeridigmNS::IsotropicElasticPlasticMaterial::computeForce(const Epetra_Vector& x,
                                                          const Epetra_Vector& u,
                                                          const Epetra_Vector& v,
                                                          const double dt,
                                                          const Epetra_Vector& cellVolume,
                                                          const int numOwnedPoints,
                                                          const int* ownedIDs,
                                                          const int* neighborhoodList,
                                                          double* bondState,
                                                          PeridigmNS::DataManager& dataManager,
                                                          Epetra_MultiVector& vectorConstitutiveData,
                                                          Epetra_Vector& force) const
{

	  // Extract pointers to the underlying data in the constitutiveData array
	  double* dilatation;
          dataManager.getData(Field_NS::DILATATION, Field_NS::FieldSpec::STEP_NP1)->ExtractView(&dilatation);
	  std::pair<int,double*> vectorView = m_decompStates.extractStrideView(vectorConstitutiveData);
	  double *y = m_decompStates.extractCurrentPositionView(vectorView);

	  double* edpN;
	  double* edpNP1;
	  dataManager.getData(Field_NS::DEVIATORIC_PLASTIC_EXTENSION, Field_NS::FieldSpec::STEP_N)->ExtractView(&edpN);
	  dataManager.getData(Field_NS::DEVIATORIC_PLASTIC_EXTENSION, Field_NS::FieldSpec::STEP_NP1)->ExtractView(&edpNP1);

	  double* weightedVolume;
	  dataManager.getData(Field_NS::WEIGHTED_VOLUME, Field_NS::FieldSpec::STEP_NONE)->ExtractView(&weightedVolume);

	  double* lambdaN;
	  double* lambdaNP1;
	  dataManager.getData(Field_NS::LAMBDA, Field_NS::FieldSpec::STEP_N)->ExtractView(&lambdaN);
	  dataManager.getData(Field_NS::LAMBDA, Field_NS::FieldSpec::STEP_NP1)->ExtractView(&lambdaNP1);


	  // Compute the force on each particle that results from interactions
	  // with locally-owned nodes
	  force.PutScalar(0.0);

	  PdMaterialUtilities::computeInternalForceIsotropicElasticPlastic
	  (
			  x.Values(),
			  y,
			  weightedVolume,
			  cellVolume.Values(),
			  dilatation,
			  bondState,
			  edpN,
			  edpNP1,
			  lambdaN,
			  lambdaNP1,
			  force.Values(),
			  neighborhoodList,
			  numOwnedPoints,
			  m_bulkModulus,
			  m_shearModulus,
			  m_horizon,
			  m_yieldStress
	  );

}

