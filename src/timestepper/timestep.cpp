#include "timestepper.hpp"

void timeStepper::timeStep(int &numReads, int &numWrites)
{
  /* First take a half step */
  PetscPrintf(PETSC_COMM_WORLD, "  ---Half step--- \n");

  currentStep = timeStepperSwitches::HALF_STEP;
  primOld->communicate();
  /* Apply boundary conditions on primOld */
  setProblemSpecificBCs(XCoords->vars, primOld->vars);

  int numReadsElemSet, numWritesElemSet;
  int numReadsComputeFluxes, numWritesComputeFluxes;
  elemOld->set(primOld->vars, *geomCenter,
               numReadsElemSet, numWritesElemSet
              );
  elemOld->computeFluxes(*geomCenter, 0, consOld->vars, 
                         numReadsComputeFluxes, numWritesComputeFluxes
                        );
  numReads  = numReadsElemSet  + numReadsComputeFluxes;
  numWrites = numWritesElemSet + numWritesComputeFluxes; 

  if (params::viscosity || params::conduction)
  {
    int numReadsEMHDGradients, numWritesEMHDGradients;
    double dX[3];
    dX[0] = prim->dX1; dX[1] = prim->dX2; dX[2] = prim->dX3;
    elemOld->computeEMHDGradients(*geomCenter, dX,
                                  numReadsEMHDGradients,
                                  numWritesEMHDGradients
                                 );
    numReads  += numReadsEMHDGradients;
    numWrites += numWritesEMHDGradients;
  }
  
  int numReadsDivFluxes, numWritesDivFluxes;
  computeDivOfFluxes(*primOld, numReadsDivFluxes, numWritesDivFluxes);
  numReads  += numReadsDivFluxes;
  numWrites += numWritesDivFluxes;

  /* Set a guess for prim */
  for (int var=0; var < prim->numVars; var++)
  {
    prim->vars[var] = primOld->vars[var];
    numReads  += 1;
    numWrites += 1;
  }

  /* Solve dU/dt + div.F - S = 0 to get prim at n+1/2 */
  solve(*prim);

  /* Copy solution to primHalfStepGhosted. WARNING: Right now
   * primHalfStep->vars[var] points to prim->vars[var]. Might need to do a deep
   * copy. */
  for (int var=0; var < prim->numVars; var++)
  {
    primHalfStep->vars[var] = prim->vars[var];
    numReads  += 1;
    numWrites += 1;
  }
  halfStepDiagnostics(XCoords->vars, primHalfStep->vars);
  /* Half step complete */

  /* Now take the full step */
  PetscPrintf(PETSC_COMM_WORLD, "  ---Full step--- \n");

  currentStep = timeStepperSwitches::FULL_STEP;
  primHalfStep->communicate();
  /* apply boundary conditions on primHalfStep */
  setProblemSpecificBCs(XCoords->vars, primHalfStep->vars);

  elemHalfStep->set(primHalfStep->vars, *geomCenter,
                    numReadsElemSet, numWritesElemSet
                   );
  numReads  += numReadsElemSet;
  numWrites += numWritesElemSet; 

  if (params::viscosity || params::conduction)
  {
    int numReadsEMHDGradients, numWritesEMHDGradients;
    double dX[3];
    dX[0] = prim->dX1; dX[1] = prim->dX2; dX[2] = prim->dX3;
    elemHalfStep->computeEMHDGradients(*geomCenter, dX,
                                       numReadsEMHDGradients,
                                       numWritesEMHDGradients
                                      );
    numReads  += numReadsEMHDGradients;
    numWrites += numWritesEMHDGradients;
  }
  computeDivOfFluxes(*primHalfStep, numReadsDivFluxes, numWritesDivFluxes);
  numReads  += numReadsDivFluxes;
  numWrites += numWritesDivFluxes;

  /* Solve dU/dt + div.F - S = 0 to get prim at n+1/2. NOTE: prim already has
   * primHalfStep as a guess */
  solve(*prim);

  /* Copy solution to primOldGhosted */
  for (int var=0; var < prim->numVars; var++)
  {
    primOld->vars[var] = prim->vars[var];
    numReads  += 1;
    numWrites += 1;
  }
  /* Compute diagnostics */
  fullStepDiagnostics(XCoords->vars, primOld->vars);

  time += dt;
  /* done */
}
