/* -------------------------------------------------------------------------- *
 *                                   OpenMM                                   *
 * -------------------------------------------------------------------------- *
 * This is part of the OpenMM molecular simulation toolkit originating from   *
 * Simbios, the NIH National Center for Physics-Based Simulation of           *
 * Biological Structures at Stanford, funded under the NIH Roadmap for        *
 * Medical Research, grant U54 GM072970. See https://simtk.org.               *
 *                                                                            *
 * Portions copyright (c) 2016 Stanford University and the Authors.           *
 * Authors: Peter Eastman                                                     *
 * Contributors:                                                              *
 *                                                                            *
 * Permission is hereby granted, free of charge, to any person obtaining a    *
 * copy of this software and associated documentation files (the "Software"), *
 * to deal in the Software without restriction, including without limitation  *
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,   *
 * and/or sell copies of the Software, and to permit persons to whom the      *
 * Software is furnished to do so, subject to the following conditions:       *
 *                                                                            *
 * The above copyright notice and this permission notice shall be included in *
 * all copies or substantial portions of the Software.                        *
 *                                                                            *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR *
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   *
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    *
 * THE AUTHORS, CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,    *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR      *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE  *
 * USE OR OTHER DEALINGS IN THE SOFTWARE.                                     *
 * -------------------------------------------------------------------------- */

#include "ReferencePlumedKernelsBXD.h"
#include "PlumedForceBXD.h"
#include "openmm/OpenMMException.h"
#include "openmm/NonbondedForce.h"
#include "openmm/internal/ContextImpl.h"
#include "openmm/reference/RealVec.h"
#include "openmm/reference/ReferencePlatform.h"
#include <cstring>

using namespace PlumedPlugin;
using namespace OpenMM;
using namespace std;

static vector<RealVec>& extractPositions(ContextImpl& context) {
    ReferencePlatform::PlatformData* data = reinterpret_cast<ReferencePlatform::PlatformData*>(context.getPlatformData());
    return *((vector<RealVec>*) data->positions);
}

static vector<RealVec>& extractForces(ContextImpl& context) {
    ReferencePlatform::PlatformData* data = reinterpret_cast<ReferencePlatform::PlatformData*>(context.getPlatformData());
    return *((vector<RealVec>*) data->forces);
}

static RealVec* extractBoxVectors(ContextImpl& context) {
    ReferencePlatform::PlatformData* data = reinterpret_cast<ReferencePlatform::PlatformData*>(context.getPlatformData());
    return (RealVec*) data->periodicBoxVectors;
}

ReferenceCalcPlumedForceKernelBXD::ReferenceCalcPlumedForceKernelBXD(std::string name, const OpenMM::Platform& platform, OpenMM::ContextImpl& contextImpl): 
CalcPlumedForceKernelBXD(name, platform), 
contextImpl(contextImpl), 
hasInitialized(false), 
lastStepIndex(0) 
{
}

ReferenceCalcPlumedForceKernelBXD::~ReferenceCalcPlumedForceKernelBXD() {
    if (hasInitialized)
        plumed_finalize(plumedmain);
}

void ReferenceCalcPlumedForceKernelBXD::initialize(const System& system, const PlumedForceBXD& force) {
    // Construct and initialize the PLUMED interface object.

    plumedmain = plumed_create();
    hasInitialized = true;
    int apiVersion;
    plumed_cmd(plumedmain, "getApiVersion", &apiVersion);
    if (apiVersion < 4)
        throw OpenMMException("Unsupported API version.  Upgrade PLUMED to a newer version.");
    int precision = 8;
    plumed_cmd(plumedmain, "setRealPrecision", &precision);
    double conversion = 1.0;
    plumed_cmd(plumedmain, "setMDEnergyUnits", &conversion);
    plumed_cmd(plumedmain, "setMDLengthUnits", &conversion);
    plumed_cmd(plumedmain, "setMDTimeUnits", &conversion);
    plumed_cmd(plumedmain, "setMDEngine", "OpenMM");
    int numParticles = system.getNumParticles();
    plumed_cmd(plumedmain, "setNatoms", &numParticles);
    double dt = contextImpl.getIntegrator().getStepSize();
    plumed_cmd(plumedmain, "setTimestep", &dt);
    plumed_cmd(plumedmain, "init", NULL);
    vector<char> scriptChars(force.getScript().size()+1);
    strcpy(&scriptChars[0], force.getScript().c_str());
    char* line = strtok(&scriptChars[0], "\r\n");
    while (line != NULL) {
        plumed_cmd(plumedmain, "readInputLine", line);
        line = strtok(NULL, "\r\n");
    }
    usesPeriodic = system.usesPeriodicBoundaryConditions();

    // Record the particle masses.

    masses.resize(numParticles);
    for (int i = 0; i < numParticles; i++)
        masses[i] = system.getParticleMass(i);

    // If there's a NonbondedForce, get charges from it.

    for (int j = 0; j < system.getNumForces(); j++) {
        const NonbondedForce* nonbonded = dynamic_cast<const NonbondedForce*>(&system.getForce(j));
        if (nonbonded != NULL) {
            charges.resize(numParticles);
            double sigma, epsilon;
            for (int i = 0; i < numParticles; i++)
                nonbonded->getParticleParameters(i, charges[i], sigma, epsilon);
        }
    }
}

// ************************* New functions *********************

void ReferenceCalcPlumedForceKernelBXD::passToPlumed_first(OpenMM::ContextImpl& context)
{
      // Pass the current state to PLUMED.

    ReferencePlatform::PlatformData* data = reinterpret_cast<ReferencePlatform::PlatformData*>(context.getPlatformData());
    int step = data->stepCount;
    plumed_cmd(plumedmain, "setStep", &step);
    plumed_cmd(plumedmain, "setMasses", &masses[0]);
    if (charges.size() > 0)
        plumed_cmd(plumedmain, "setCharges", &charges[0]);
    vector<RealVec>& pos = extractPositions(context);
    plumed_cmd(plumedmain, "setPositions", &pos[0][0]);
    vector<RealVec>& force = extractForces(context);
    plumed_cmd(plumedmain, "setForces", &force[0][0]);
    if (usesPeriodic) 
    {
        RealVec* boxVectors = extractBoxVectors(context);
        plumed_cmd(plumedmain, "setBox", &boxVectors[0][0]);
    }
    double virial[9];
    plumed_cmd(plumedmain, "setVirial", &virial);
}

void ReferenceCalcPlumedForceKernelBXD::passToPlumed()
{

}

// ***************************************************************


double ReferenceCalcPlumedForceKernelBXD::execute(ContextImpl& context, bool includeForces, bool includeEnergy) {
    // Pass the current state to PLUMED.

    ReferencePlatform::PlatformData* data = reinterpret_cast<ReferencePlatform::PlatformData*>(context.getPlatformData());
    int step = data->stepCount;
    plumed_cmd(plumedmain, "setStep", &step);
    plumed_cmd(plumedmain, "setMasses", &masses[0]);
    if (charges.size() > 0)
        plumed_cmd(plumedmain, "setCharges", &charges[0]);
    vector<RealVec>& pos = extractPositions(context);
    plumed_cmd(plumedmain, "setPositions", &pos[0][0]);
    vector<RealVec>& force = extractForces(context);
    plumed_cmd(plumedmain, "setForces", &force[0][0]);
    if (usesPeriodic) {
        RealVec* boxVectors = extractBoxVectors(context);
        plumed_cmd(plumedmain, "setBox", &boxVectors[0][0]);
    }
    double virial[9];
    plumed_cmd(plumedmain, "setVirial", &virial);

    // Calculate the forces and energy.

    plumed_cmd(plumedmain, "prepareCalc", NULL);
    plumed_cmd(plumedmain, "performCalcNoUpdate", NULL);
    if (step != lastStepIndex) {
        plumed_cmd(plumedmain, "update", NULL);
        lastStepIndex = step;
    }
    double energy = 0;
    plumed_cmd(plumedmain, "getBias", &energy);
    return energy;
}
