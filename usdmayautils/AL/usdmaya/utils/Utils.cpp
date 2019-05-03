//
// Copyright 2017 Animal Logic
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.//
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
#include "AL/usdmaya/utils/Utils.h"
#include "AL/usd/utils/DebugCodes.h"
#include "AL/maya/utils/Utils.h"

#include "maya/MDagPath.h"
#include "maya/MEulerRotation.h"
#include "maya/MFnDagNode.h"
#include "maya/MGlobal.h"
#include "maya/MMatrix.h"
#include "maya/MVector.h"

namespace AL {
namespace usdmaya {
namespace utils {

//----------------------------------------------------------------------------------------------------------------------
MTransformationMatrix matrixToMTransformationMatrix(const GfMatrix4d& value)
{
  MMatrix mayaMatrix;
  // maya matrices and pxr matrices share same ordering, so can copy direcly into MMatrix's storage
  value.Get(mayaMatrix.matrix);
  return MTransformationMatrix(mayaMatrix);
}

//----------------------------------------------------------------------------------------------------------------------
MString mapUsdPrimToMayaNode(const UsdPrim& usdPrim,
                             const MObject& mayaObject,
                             const MDagPath* const proxyShapeNode)
{
  if(!usdPrim.IsValid())
  {
    MGlobal::displayError("mapUsdPrimToMayaNode: Invalid prim!");
    return MString();
  }
  TfToken mayaPathAttributeName("MayaPath");

  UsdStageWeakPtr stage = usdPrim.GetStage();

  MFnDagNode mayaNode(mayaObject);
  MDagPath mayaDagPath;
  mayaNode.getPath(mayaDagPath);
  std::string mayaElementPath = AL::maya::utils::convert(mayaDagPath.fullPathName());

  if(mayaDagPath.length() == 0 && proxyShapeNode)
  {
    // Prepend the mayaPathPrefix
    mayaElementPath = proxyShapeNode->fullPathName().asChar() + usdPrim.GetPath().GetString();
    std::replace(mayaElementPath.begin(), mayaElementPath.end(), '/','|');
  }

  TF_DEBUG(ALUTILS_INFO).Msg("Mapped the path for prim=%s to mayaObject=%s\n", usdPrim.GetName().GetText(), mayaElementPath.c_str());

  return AL::maya::utils::convert(mayaElementPath);
}

//----------------------------------------------------------------------------------------------------------------------
void convertDoubleVec4ArrayToFloatVec3Array(const double* const input, float* const output, size_t count)
{
  for(size_t i = 0; i < count; ++i)
  {
    output[i * 3] = float(input[i * 4]);
    output[i * 3 + 1] = float(input[i * 4 + 1]);
    output[i * 3 + 2] = float(input[i * 4 + 2]);
  }
}

//----------------------------------------------------------------------------------------------------------------------
} // utils
} // usdmaya
} // AL
//----------------------------------------------------------------------------------------------------------------------
