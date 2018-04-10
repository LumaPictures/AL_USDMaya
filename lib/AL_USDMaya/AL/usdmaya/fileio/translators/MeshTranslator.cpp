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
#include "AL/usd/utils/SIMD.h"
#include "AL/usdmaya/utils/Utils.h"
#include "AL/usdmaya/utils/MeshUtils.h"
#include "AL/usdmaya/fileio/ExportParams.h"
#include "AL/usdmaya/fileio/ImportParams.h"
#include "AL/usdmaya/fileio/AnimationTranslator.h"
#include "AL/usdmaya/fileio/translators/MeshTranslator.h"

#include "maya/MAnimUtil.h"
#include "maya/MColorArray.h"
#include "maya/MDagPath.h"
#include "maya/MDoubleArray.h"
#include "maya/MFnMesh.h"
#include "maya/MFloatArray.h"
#include "maya/MFloatPointArray.h"
#include "maya/MGlobal.h"
#include "maya/MIntArray.h"
#include "maya/MItMeshPolygon.h"
#include "maya/MItMeshVertex.h"
#include "maya/MObject.h"
#include "maya/MPlug.h"
#include "maya/MUintArray.h"
#include "maya/MVector.h"
#include "maya/MVectorArray.h"

#include "pxr/usd/usd/modelAPI.h"
#include "pxr/usd/usd/timeCode.h"
#include "pxr/usd/usdGeom/mesh.h"

#include <algorithm>
#include <cstring>

namespace AL {
namespace usdmaya {
namespace fileio {
namespace translators {


//----------------------------------------------------------------------------------------------------------------------
bool MeshTranslator::attributeHandled(const UsdAttribute& usdAttr)
{
  const std::string& str = usdAttr.GetName().GetString();
  const char* glimpse_prefix = "glimpse_";
  if(!std::strncmp(glimpse_prefix, str.c_str(), 8))
  {
    return true;
  }

  return DagNodeTranslator::attributeHandled(usdAttr);
}


//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------
// Export
//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------------------
void MeshTranslator::copyNormalData(const MFnMesh& fnMesh, const UsdAttribute& normalsAttr, UsdTimeCode time)
{
  MStatus status;
  const uint32_t numNormals = fnMesh.numNormals();
  VtArray<GfVec3f> normals(numNormals);
  const float* normalsData = fnMesh.getRawNormals(&status);
  if(status)
  {
    memcpy((GfVec3f*)normals.data(), normalsData, sizeof(float) * 3 * numNormals);
    normalsAttr.Set(normals, time);
  }
  else
  {
    MGlobal::displayError(MString("Unable to access mesh normals on mesh: ") + fnMesh.fullPathName());
  }
}

//----------------------------------------------------------------------------------------------------------------------
UsdPrim MeshTranslator::exportObject(UsdStageRefPtr stage, MDagPath path, const SdfPath& usdPath, const ExporterParams& params)
{
  if(!params.m_meshes)
    return UsdPrim();

  UsdGeomMesh mesh = UsdGeomMesh::Define(stage, usdPath);

  MStatus status;
  MFnMesh fnMesh(path, &status);
  AL_MAYA_CHECK_ERROR2(status, MString("unable to attach function set to mesh") + path.fullPathName());
  if(status)
  {
    UsdAttribute pointsAttr = mesh.GetPointsAttr();
    if (params.m_animTranslator && AnimationTranslator::isAnimatedMesh(path))
    {
      params.m_animTranslator->addMesh(path, pointsAttr);
    }
    
    AL::usdmaya::utils::copyVertexData(fnMesh, pointsAttr);
    AL::usdmaya::utils::copyFaceConnectsAndPolyCounts(mesh, fnMesh);
    AL::usdmaya::utils::copyInvisibleHoles(mesh, fnMesh);
    AL::usdmaya::utils::copyUvSetData(mesh, fnMesh, params.m_leftHandedUV);
    AL::usdmaya::utils::copyNormalData(fnMesh, mesh.GetNormalsAttr());
    AL::usdmaya::utils::copyGlimpseTesselationAttributes(mesh, fnMesh);
    AL::usdmaya::utils::copyColourSetData(mesh, fnMesh);
    AL::usdmaya::utils::copyCreaseVertices(mesh, fnMesh);
    AL::usdmaya::utils::copyCreaseEdges(mesh, fnMesh);

    // pick up any additional attributes attached to the mesh node (these will be added alongside the transform attributes)
    if(params.m_dynamicAttributes)
    {
      UsdPrim prim = mesh.GetPrim();
      DgNodeTranslator::copyDynamicAttributes(path.node(), prim);
    }
  }
  return mesh.GetPrim();
}

//----------------------------------------------------------------------------------------------------------------------
UsdPrim MeshTranslator::exportUV(UsdStageRefPtr stage, MDagPath path, const SdfPath& usdPath, const ExporterParams& params)
{
  UsdPrim overPrim = stage->OverridePrim(usdPath);
  MStatus status;
  MFnMesh fnMesh(path, &status);
  AL_MAYA_CHECK_ERROR2(status, MString("unable to attach function set to mesh") + path.fullPathName());
  if (status)
  {
    UsdGeomMesh mesh(overPrim);
    AL::usdmaya::utils::copyUvSetData(mesh, fnMesh, params.m_leftHandedUV);
  }
  return overPrim;
}

//----------------------------------------------------------------------------------------------------------------------
MStatus MeshTranslator::registerType()
{
  return MS::kSuccess;
}

//----------------------------------------------------------------------------------------------------------------------
MObject MeshTranslator::createNode(const UsdPrim& from, MObject parent, const char* nodeType, const ImporterParams& params)
{
  if(!params.m_meshes)
    return MObject::kNullObj;

  const UsdGeomMesh mesh(from);

  TfToken orientation;
  bool leftHanded = (mesh.GetOrientationAttr().Get(&orientation) and orientation == UsdGeomTokens->leftHanded);

  MFnMesh fnMesh;
  MFloatPointArray points;
  MVectorArray normals;
  MIntArray counts, connects;

  AL::usdmaya::utils::gatherFaceConnectsAndVertices(mesh, points, normals, counts, connects, leftHanded);

  MObject polyShape = fnMesh.create(points.length(), counts.length(), points, counts, connects, parent);

  MIntArray normalsFaceIds;
  normalsFaceIds.setLength(connects.length());
  int32_t* normalsFaceIdsPtr = &normalsFaceIds[0];
  if(normals.length())
  {
    MIntArray normalsFaceIds;
    normalsFaceIds.setLength(connects.length());
    int32_t* normalsFaceIdsPtr = &normalsFaceIds[0];
    if (normals.length() == fnMesh.numFaceVertices())
    {
      for (uint32_t i = 0, k = 0, n = counts.length(); i < n; i++)
      {
        for (uint32_t j = 0, m = counts[i]; j < m; j++, ++k)
        {
          normalsFaceIdsPtr[k] = i;
        }
      }
    }
    fnMesh.setFaceVertexNormals(normals, normalsFaceIds, connects);
  }

  MFnDagNode fnDag(polyShape);
  fnDag.setName(std::string(from.GetName().GetString() + std::string("Shape")).c_str());

  AL::usdmaya::utils::applyHoleFaces(mesh, fnMesh);
  AL::usdmaya::utils::applyVertexCreases(mesh, fnMesh);
  AL::usdmaya::utils::applyEdgeCreases(mesh, fnMesh);
  AL::usdmaya::utils::applyGlimpseSubdivParams(from, fnMesh);
  AL::usdmaya::utils::applyGlimpseUserDataParams(from, fnMesh);
  applyDefaultMaterialOnShape(polyShape);
  AL::usdmaya::utils::applyPrimVars(mesh, fnMesh, counts, connects);

  return polyShape;
}

//----------------------------------------------------------------------------------------------------------------------
} // translators
} // fileio
} // usdmaya
} // AL
//----------------------------------------------------------------------------------------------------------------------
