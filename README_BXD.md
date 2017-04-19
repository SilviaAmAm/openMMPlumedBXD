### New files added for BXD 

**openmm-plumed/openmmapi/src/PlumedForceBXD.cpp**
**openmm-plumed/openmmapi/include/PlumedForceBXD.h**

These have been added because there is need for a new force object that is not quite the same as that in PlumedForce.h.
The PlumedForceBXD object is created in the HelloArgon.cpp file. The class contains a function called "createImpl" which creates an object "PlumedForceImplBXD". 

```
ForceImpl* PlumedForceBXD::createImpl() const 
{
    return new PlumedForceImplBXD(*this);
}
```

**openmm-plumed/openmmapi/include/internal/PlumedForceImplBXD.h**
**openmm-plumed/openmmapi/src/PlumedForceImplBXD.cpp**

The constructor is modified here so that it receives the PlumedForceBXD object rather than the PlumedForce object.

```
PlumedForceImplBXD::PlumedForceImplBXD(const PlumedForceBXD& owner) : owner(owner) {
}
```

Then, the initialise function is modified so that it takes the right kernel. Then, the initialize function is modified so that it creates the BXD kernel.


**openmm-plumed/openmmapi/include/PlumedKernelsBXD.h**

This implements the new Kernel that is used in PlumedForceImplBXD.cpp in the initialize function, where the kernel is created.

**/Users/walfits/OpenMM-from-src/Source/openmm-plumed/platforms/reference/src/ReferencePlumedKernelsBXD.h**

This implements a class that inherits from CalcPlumedForceKernelBXD. It is the class that has the functions to update the state in plumed.




