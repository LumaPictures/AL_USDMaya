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
#include "AL/usdmaya/Utils.h"
#include "AL/usdmaya/DebugCodes.h"

#include "maya/MMatrix.h"
#include "maya/MEulerRotation.h"
#include "maya/MVector.h"
#include "maya/MObject.h"
#include "maya/MFnDagNode.h"
#include "maya/MDagPath.h"
#include "maya/MGlobal.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/editTarget.h"
#include "pxr/usd/usd/common.h"
#include "pxr/usd/usd/stage.h"

#include <algorithm>

namespace AL {
namespace usdmaya {

//----------------------------------------------------------------------------------------------------------------------
MTransformationMatrix matrixToMTransformationMatrix(GfMatrix4d& value)
{
  MMatrix mayaMatrix;
  // maya matrices and pxr matrices share same ordering, so can copy direcly into MMatrix's storage
  value.Get(mayaMatrix.matrix);
  return MTransformationMatrix(mayaMatrix);
}

//----------------------------------------------------------------------------------------------------------------------
MString mapUsdPrimToMayaNode(const UsdPrim& usdPrim, const MObject& mayaObject, const MDagPath* const usdMayaShapeNode)
{
  if(!usdPrim.IsValid())
  {
    MGlobal::displayError("mapUsdPrimToMayaNode: Invalid prim!");
    return MString();
  }
  TfToken mayaPathAttributeName("MayaPath");

  UsdStageWeakPtr stage = usdPrim.GetStage();

  // copy the previousTarget, and restore it later
  UsdEditTarget previousTarget = stage->GetEditTarget();
  //auto previousLayer = previousTarget.GetLayer();
  auto sessionLayer = stage->GetSessionLayer();
  stage->SetEditTarget(sessionLayer);

  MFnDagNode mayaNode(mayaObject);
  MDagPath mayaDagPath;
  mayaNode.getPath(mayaDagPath);
  std::string mayaElementPath = convert(mayaDagPath.fullPathName());

  if(mayaDagPath.length() == 0 && usdMayaShapeNode)
  {
    // Prepend the mayaPathPrefix
    mayaElementPath = usdMayaShapeNode->fullPathName().asChar() + usdPrim.GetPath().GetString();
    std::replace(mayaElementPath.begin(), mayaElementPath.end(), '/','|');
  }

  VtValue mayaPathValue(mayaElementPath);
  usdPrim.SetCustomDataByKey(mayaPathAttributeName, mayaPathValue);

  TF_DEBUG(ALUSDMAYA_TRANSLATORS).Msg("Capturing the path for prim=%s mayaObject=%s\n", usdPrim.GetName().GetText(), mayaElementPath.c_str());

  //restore the edit target
  stage->SetEditTarget(previousTarget);

  return convert(mayaElementPath);
}

//----------------------------------------------------------------------------------------------------------------------
} // usdmaya
} // AL
//----------------------------------------------------------------------------------------------------------------------
