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

#pragma once

#include "./Api.h"

#include "maya/MObject.h"
#include "maya/MString.h"
#include "maya/MFnDependencyNode.h"

#include <string>

#include "pxr/pxr.h"
#include "pxr/base/tf/token.h"
#include "AL/maya/utils/ForwardDeclares.h"
#include "AL/usd/utils/ForwardDeclares.h"
#include "AL/maya/utils/Utils.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace AL {
namespace usdmaya {
namespace utils {

//----------------------------------------------------------------------------------------------------------------------
/// \brief  Captures the mapping of UsdPrim -> Maya Object and stores it into the session layer.
///         usdMayaShapeNode is an optional argument, if it is passed and the passed in mayaObject's path couldnt be determined,
///         then the corresponding maya path is determined using this AL::usdmaya::nodes::ProxyShape and the usdPrim path.
///         It is to get around the delayed creation of nodes using a Modifier.
/// \param  usdPrim the prim to map to the mayaObject
/// \param  mayaObject the maya node to map
/// \param  proxyShapeNode pointer to the daga path for the proxy shape
/// \return returns the path name
/// \ingroup usdmaya
//----------------------------------------------------------------------------------------------------------------------
AL_USDMAYA_UTILS_PUBLIC
MString mapUsdPrimToMayaNode(const UsdPrim& usdPrim,
                             const MObject& mayaObject,
                             const MDagPath* const proxyShapeNode = nullptr);

//----------------------------------------------------------------------------------------------------------------------
/// \brief  convert a 4x4 matrix to a Maya MTransformationMatrix, for decomposition
/// \param  value the input pixar 4x4 matrix
/// \return the MTransformationMatrix, which can be queried for individual xform components
/// \ingroup usdmaya
//----------------------------------------------------------------------------------------------------------------------
AL_USDMAYA_UTILS_PUBLIC
MTransformationMatrix matrixToMTransformationMatrix(GfMatrix4d& value);

//----------------------------------------------------------------------------------------------------------------------
/// \brief  a simple method to convert double vec4 array to float vec3 array
/// \param  the input double vec4 array
/// \param  the output float vec3 array
/// \param  count number of elements
//----------------------------------------------------------------------------------------------------------------------
void convertDoubleVec4ArrayToFloatVec3Array(const double* const input, float* const output, size_t count);

//----------------------------------------------------------------------------------------------------------------------
/// \brief  convert string types
/// \param  token the USD TfToken to convert to an MString
/// \return the MString
/// \ingroup usdmaya
//----------------------------------------------------------------------------------------------------------------------
inline MString convert(const TfToken& token)
{
  return MString(token.GetText(), token.size());
}

//----------------------------------------------------------------------------------------------------------------------
}// utils
}// usdmaya
}// AL
//----------------------------------------------------------------------------------------------------------------------
