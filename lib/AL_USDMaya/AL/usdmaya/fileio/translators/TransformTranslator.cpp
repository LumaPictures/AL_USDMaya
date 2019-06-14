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
#include "AL/usdmaya/fileio/translators/TransformTranslator.h"
#include "AL/usdmaya/fileio/ExportParams.h"
#include "AL/usdmaya/fileio/ImportParams.h"
#include "AL/usdmaya/fileio/AnimationTranslator.h"
#include "AL/usdmaya/nodes/Transform.h"

#include "maya/MObject.h"
#include "maya/MStatus.h"
#include "maya/MAngle.h"
#include "maya/MDagPath.h"
#include "maya/MDoubleArray.h"
#include "maya/MPlug.h"
#include "maya/MFnTransform.h"
#include "maya/MDGModifier.h"
#include "maya/MNodeClass.h"
#include "maya/MGlobal.h"
#include "maya/MMatrix.h"
#include "maya/MEulerRotation.h"
#include "maya/MVector.h"
#include "maya/MFileIO.h"
#include "maya/MItDag.h"

#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usdGeom/xform.h"
#include "pxr/usd/usdGeom/xformCommonAPI.h"
#include "usdMaya/xformStack.h"
#include "AL/usdmaya/utils/AttributeType.h"
#include "AL/usdmaya/utils/DgNodeHelper.h"
#include "AL/usdmaya/utils/Utils.h"

namespace AL {
namespace usdmaya {
namespace fileio {
namespace translators {


//----------------------------------------------------------------------------------------------------------------------
MObject TransformTranslator::m_inheritsTransform = MObject::kNullObj;
MObject TransformTranslator::m_scale = MObject::kNullObj;
MObject TransformTranslator::m_shear = MObject::kNullObj;
MObject TransformTranslator::m_rotation = MObject::kNullObj;
MObject TransformTranslator::m_rotationX = MObject::kNullObj;
MObject TransformTranslator::m_rotationY = MObject::kNullObj;
MObject TransformTranslator::m_rotationZ = MObject::kNullObj;
MObject TransformTranslator::m_rotateOrder = MObject::kNullObj;
MObject TransformTranslator::m_rotateAxis = MObject::kNullObj;
MObject TransformTranslator::m_rotateAxisX = MObject::kNullObj;
MObject TransformTranslator::m_rotateAxisY = MObject::kNullObj;
MObject TransformTranslator::m_rotateAxisZ = MObject::kNullObj;
MObject TransformTranslator::m_translation = MObject::kNullObj;
MObject TransformTranslator::m_scalePivot = MObject::kNullObj;
MObject TransformTranslator::m_rotatePivot = MObject::kNullObj;
MObject TransformTranslator::m_scalePivotTranslate = MObject::kNullObj;
MObject TransformTranslator::m_rotatePivotTranslate = MObject::kNullObj;
MObject TransformTranslator::m_selectHandle = MObject::kNullObj;
MObject TransformTranslator::m_transMinusRotatePivot = MObject::kNullObj;
MObject TransformTranslator::m_visibility = MObject::kNullObj;

//----------------------------------------------------------------------------------------------------------------------
MStatus TransformTranslator::registerType()
{
  const char* const errorString = "Unable to extract attribute for TransformTranslator";
  MNodeClass nc("transform");
  MStatus status;
  m_rotation = nc.attribute("r", &status);
  AL_MAYA_CHECK_ERROR(status, errorString);

  m_rotationX = nc.attribute("rx", &status);
  AL_MAYA_CHECK_ERROR(status, errorString);

  m_rotationY = nc.attribute("ry", &status);
  AL_MAYA_CHECK_ERROR(status, errorString);

  m_rotationZ = nc.attribute("rz", &status);
  AL_MAYA_CHECK_ERROR(status, errorString);

  m_rotateOrder = nc.attribute("ro", &status);
  AL_MAYA_CHECK_ERROR(status, errorString);

  m_rotateAxis = nc.attribute("ra", &status);
  AL_MAYA_CHECK_ERROR(status, errorString);

  m_rotateAxisX = nc.attribute("rax", &status);
  AL_MAYA_CHECK_ERROR(status, errorString);

  m_rotateAxisY = nc.attribute("ray", &status);
  AL_MAYA_CHECK_ERROR(status, errorString);

  m_rotateAxisZ = nc.attribute("raz", &status);
  AL_MAYA_CHECK_ERROR(status, errorString);

  m_rotatePivot = nc.attribute("rp", &status);
  AL_MAYA_CHECK_ERROR(status, errorString);

  m_rotatePivotTranslate = nc.attribute("rpt", &status);
  AL_MAYA_CHECK_ERROR(status, errorString);

  m_scale = nc.attribute("s", &status);
  AL_MAYA_CHECK_ERROR(status, errorString);

  m_scalePivot = nc.attribute("sp", &status);
  AL_MAYA_CHECK_ERROR(status, errorString);

  m_scalePivotTranslate = nc.attribute("spt", &status);
  AL_MAYA_CHECK_ERROR(status, errorString);

  m_selectHandle = nc.attribute("hdl", &status);
  AL_MAYA_CHECK_ERROR(status, errorString);

  m_shear = nc.attribute("sh", &status);
  AL_MAYA_CHECK_ERROR(status, errorString);

  m_transMinusRotatePivot = nc.attribute("tmrp", &status);
  AL_MAYA_CHECK_ERROR(status, errorString);

  m_translation = nc.attribute("t", &status);
  AL_MAYA_CHECK_ERROR(status, errorString);

  m_inheritsTransform = nc.attribute("it", &status);
  AL_MAYA_CHECK_ERROR(status, errorString);

  MNodeClass dagNodeClass("dagNode");
  m_visibility = dagNodeClass.attribute("visibility", &status);
  AL_MAYA_CHECK_ERROR(status, errorString);

  return MS::kSuccess;
}

//----------------------------------------------------------------------------------------------------------------------
MObject TransformTranslator::createNode(const UsdPrim& from, MObject parent, const char* nodeType, const ImporterParams& params)
{
  const char* const xformError = "ALUSDImport: error creating transform node";
  MStatus status;
  MFnTransform fnx;
  MObject obj = fnx.create(parent, &status);
  AL_MAYA_CHECK_ERROR2(status, xformError);

  status = copyAttributes(from, obj, params);
  AL_MAYA_CHECK_ERROR_RETURN_NULL_MOBJECT(status, "ALUSDImport: error getting transform attributes");
  return obj;
}

//----------------------------------------------------------------------------------------------------------------------
MEulerRotation::RotationOrder convertRotationOrder(UsdGeomXformOp::Type type)
{
  switch (type)
  {
  case UsdGeomXformOp::TypeRotateX:
  case UsdGeomXformOp::TypeRotateY:
  case UsdGeomXformOp::TypeRotateZ:
  case UsdGeomXformOp::TypeRotateXYZ:
    return MEulerRotation::kXYZ;
  case UsdGeomXformOp::TypeRotateXZY:
    return MEulerRotation::kXZY;
  case UsdGeomXformOp::TypeRotateYXZ:
    return MEulerRotation::kYXZ;
  case UsdGeomXformOp::TypeRotateYZX:
    return MEulerRotation::kYZX;
  case UsdGeomXformOp::TypeRotateZXY:
    return MEulerRotation::kZXY;
  case UsdGeomXformOp::TypeRotateZYX:
    return MEulerRotation::kZYX;
  default:
    break;
  }
  return MEulerRotation::kXYZ;
}

//----------------------------------------------------------------------------------------------------------------------
bool TransformTranslator::getAnimationVariables(const UsdMayaXformOpClassification& opIt, MObjectArray& attributes, double &conversionFactor)
{
  attributes.clear();
  const TfToken& opName = opIt.GetName();
  if (opName == UsdMayaXformStackTokens->translate)
  {
    attributes.append(m_translation);
  }
  else if (opName == UsdMayaXformStackTokens->pivotTranslate)
  {
    attributes.append(m_rotatePivotTranslate);
    attributes.append(m_scalePivotTranslate);
  }
  else if (opName == UsdMayaXformStackTokens->pivot)
  {
    attributes.append(m_rotatePivot);
    attributes.append(m_scalePivot);
  }
  else if (opName == UsdMayaXformStackTokens->rotatePivotTranslate)
  {
    attributes.append(m_rotatePivotTranslate);
  }
  else if (opName == UsdMayaXformStackTokens->rotatePivot)
  {
    attributes.append(m_rotatePivot);
  }
  else if (opName == UsdMayaXformStackTokens->rotate)
  {
    attributes.append(m_rotation);
    MAngle one(1.0, MAngle::kDegrees);
    conversionFactor = one.as(MAngle::kRadians);
  }
  else if (opName == UsdMayaXformStackTokens->rotateAxis)
  {
    attributes.append(m_rotateAxis);
    MAngle one(1.0, MAngle::kDegrees);
    conversionFactor = one.as(MAngle::kRadians);
  }
  else if (opName == UsdMayaXformStackTokens->scalePivotTranslate)
  {
    attributes.append(m_scalePivotTranslate);
  }
  else if (opName == UsdMayaXformStackTokens->scalePivot)
  {
    attributes.append(m_scalePivot);
  }
  else if (opName == UsdMayaXformStackTokens->shear)
  {
    attributes.append(m_shear);
  }
  else if (opName == UsdMayaXformStackTokens->scale)
  {
    attributes.append(m_scale);
  }
  else
  {
    std::cerr << "TransformTranslator::copyAnimated - Unknown transform operation: " << opName.GetText() << std::endl;
    return 1;
  }

  return 0;
}

//----------------------------------------------------------------------------------------------------------------------
MStatus TransformTranslator::copyAttributes(const UsdPrim& from, MObject to, const ImporterParams& params)
{
  static const UsdTimeCode usdTime = params.m_forceDefaultRead ?
                                     UsdTimeCode::Default() : UsdTimeCode::EarliestTime();
  const char* const xformError = "ALUSDImport: error creating transform node";
  AL_MAYA_CHECK_ERROR2(DagNodeTranslator::copyAttributes(from, to, params), xformError);

  const UsdGeomXform xformSchema(from);
  bool resetsXformStack = false;
  std::vector<UsdGeomXformOp> xformops = xformSchema.GetOrderedXformOps(&resetsXformStack);

  UsdMayaXformStack::OpClassList orderedOps = \
      UsdMayaXformStack::FirstMatchingSubstack(
          {
              &UsdMayaXformStack::MayaStack(),
              &UsdMayaXformStack::CommonStack()
          },
          xformops);

  if(!orderedOps.empty())
  {
    auto opIt = orderedOps.begin();
    for(std::vector<UsdGeomXformOp>::const_iterator it = xformops.begin(), e = xformops.end(); it != e; ++it, ++opIt)
    {
      const UsdGeomXformOp& op = *it;
      const UsdMayaXformOpClassification& opClass = *opIt;
      const SdfValueTypeName vtn = op.GetTypeName();

      utils::UsdDataType attr_type = AL::usdmaya::utils::getAttributeType(vtn);

      // Import animation (if we have time samples)
      if (op.GetNumTimeSamples())
      {
        if(attr_type == utils::UsdDataType::kVec3f || attr_type == utils::UsdDataType::kVec3d)
        {
          MObjectArray attrObjs;
          double conversionFactor = 1.0;
          getAnimationVariables(opClass, attrObjs, conversionFactor);

          for(size_t attrI = 0; attrI < attrObjs.length(); ++attrI)
          {
            MObject& obj = attrObjs[attrI];
            if (obj.isNull())
            {
              continue;
            }

            const TfToken& opName = opClass.GetName();
            if (opName == UsdMayaXformStackTokens->rotate)
            {
              // Set the rotate order
              MFnTransform trans(to);
              AL_MAYA_CHECK_ERROR2(setInt32(to, m_rotateOrder, uint32_t(convertRotationOrder(op.GetOpType()))), xformError);
            }

            if (attr_type == utils::UsdDataType::kVec3f)
            {
                AL_MAYA_CHECK_ERROR2(setVec3Anim<GfVec3f>(to, obj, op, conversionFactor), xformError);
            }
            else
            {
              AL_MAYA_CHECK_ERROR2(setVec3Anim<GfVec3d>(to, obj, op, conversionFactor), xformError);
            }
          }
        }
        else if(attr_type == utils::UsdDataType::kFloat)
        {
          MObject attr;
          const TfToken& opName = opClass.GetName();
          if (opName == UsdMayaXformStackTokens->rotate)
          {
            switch(op.GetOpType())
            {
              case UsdGeomXformOp::TypeRotateX: attr = m_rotationX; break;
              case UsdGeomXformOp::TypeRotateY: attr = m_rotationY; break;
              case UsdGeomXformOp::TypeRotateZ: attr = m_rotationZ; break;
              default: break;
            }
          }
          else if (opName == UsdMayaXformStackTokens->rotateAxis)
          {
            switch(op.GetOpType())
            {
              case UsdGeomXformOp::TypeRotateX: attr = m_rotateAxisX; break;
              case UsdGeomXformOp::TypeRotateY: attr = m_rotateAxisY; break;
              case UsdGeomXformOp::TypeRotateZ: attr = m_rotateAxisZ; break;
              default: break;
            }
            break;
          }

          if (!attr.isNull())
          {
            setAngleAnim(to, attr, op);
          }
        }
        else if(attr_type == utils::UsdDataType::kMatrix4d)
        {
          if(opClass.GetName() == UsdMayaXformStackTokens->shear)
          {
            std::cerr << "[TransformTranslator::copyAttributes] Error: Animated shear not currently supported" << std::endl;
          }
        }

        continue;

      }

      // Else if static
      const float degToRad = 3.141592654f / 180.0f;

      if(attr_type == utils::UsdDataType::kVec3f)
      {
        GfVec3f value(0);

        const bool retValue = op.GetAs<GfVec3f>(&value, usdTime);
        if (!retValue)
        {
          continue;
        }

        const TfToken& opName = opClass.GetName();
        if (opName == UsdMayaXformStackTokens->translate)
        {
          AL_MAYA_CHECK_ERROR2(setVec3(to, m_translation, value[0], value[1], value[2]), xformError);
        }
        else if (opName == UsdMayaXformStackTokens->rotatePivotTranslate)
        {
          AL_MAYA_CHECK_ERROR2(setVec3(to, m_rotatePivotTranslate, value[0], value[1], value[2]), xformError);
        }
        else if (opName == UsdMayaXformStackTokens->rotatePivot)
        {
          AL_MAYA_CHECK_ERROR2(setVec3(to, m_rotatePivot, value[0], value[1], value[2]), xformError);
        }
        else if (opName == UsdMayaXformStackTokens->rotate)
        {
          AL_MAYA_CHECK_ERROR2(setInt32(to, m_rotateOrder, uint32_t(convertRotationOrder(op.GetOpType()))), xformError);
          AL_MAYA_CHECK_ERROR2(setVec3(to, m_rotation,
                  MAngle(value[0], MAngle::kDegrees),
                  MAngle(value[1], MAngle::kDegrees),
                  MAngle(value[2], MAngle::kDegrees)), xformError);
        }
        else if (opName == UsdMayaXformStackTokens->rotateAxis)
        {
          AL_MAYA_CHECK_ERROR2(setVec3(to, m_rotateAxis, value[0] * degToRad, value[1] * degToRad, value[2] * degToRad), xformError);
        }
        else if (opName == UsdMayaXformStackTokens->scalePivotTranslate)
        {
          AL_MAYA_CHECK_ERROR2(setVec3(to, m_scalePivotTranslate, value[0], value[1], value[2]), xformError);
        }
        else if (opName == UsdMayaXformStackTokens->scalePivot)
        {
          AL_MAYA_CHECK_ERROR2(setVec3(to, m_scalePivot, value[0], value[1], value[2]), xformError);
        }
        else if (opName == UsdMayaXformStackTokens->shear)
        {
          AL_MAYA_CHECK_ERROR2(setVec3(to, m_shear, value[0], value[1], value[2]), xformError);
        }
        else if (opName == UsdMayaXformStackTokens->scale)
        {
          AL_MAYA_CHECK_ERROR2(setVec3(to, m_scale, value[0], value[1], value[2]), xformError);
        }
      }
      else
      if(attr_type == utils::UsdDataType::kVec3d)
      {
        GfVec3d value(0);

        const bool retValue = op.GetAs<GfVec3d>(&value, usdTime);
        if (!retValue)
        {
          continue;
        }

        const TfToken& opName = opClass.GetName();
        if (opName == UsdMayaXformStackTokens->translate)
        {
          AL_MAYA_CHECK_ERROR2(setVec3(to, m_translation, value[0], value[1], value[2]), xformError);
        }
        else if (opName == UsdMayaXformStackTokens->rotatePivotTranslate)
        {
          AL_MAYA_CHECK_ERROR2(setVec3(to, m_rotatePivotTranslate, value[0], value[1], value[2]), xformError);
        }
        else if (opName == UsdMayaXformStackTokens->rotatePivot)
        {
          AL_MAYA_CHECK_ERROR2(setVec3(to, m_rotatePivot, value[0], value[1], value[2]), xformError);
        }
        else if (opName == UsdMayaXformStackTokens->rotate)
        {
          AL_MAYA_CHECK_ERROR2(setInt32(to, m_rotateOrder, uint32_t(convertRotationOrder(op.GetOpType()))), xformError);
          AL_MAYA_CHECK_ERROR2(setVec3(to, m_rotation,
                  MAngle(value[0], MAngle::kDegrees),
                  MAngle(value[1], MAngle::kDegrees),
                  MAngle(value[2], MAngle::kDegrees)), xformError);
        }
        else if (opName == UsdMayaXformStackTokens->rotateAxis)
        {
          AL_MAYA_CHECK_ERROR2(setVec3(to, m_rotateAxis, value[0] * degToRad, value[1] * degToRad, value[2] * degToRad), xformError);
        }
        else if (opName == UsdMayaXformStackTokens->scalePivotTranslate)
        {
          AL_MAYA_CHECK_ERROR2(setVec3(to, m_scalePivotTranslate, value[0], value[1], value[2]), xformError);
        }
        else if (opName == UsdMayaXformStackTokens->scalePivot)
        {
          AL_MAYA_CHECK_ERROR2(setVec3(to, m_scalePivot, value[0], value[1], value[2]), xformError);
        }
        else if (opName == UsdMayaXformStackTokens->shear)
        {
          AL_MAYA_CHECK_ERROR2(setVec3(to, m_shear, value[0], value[1], value[2]), xformError);
        }
        else if (opName == UsdMayaXformStackTokens->scale)
        {
          AL_MAYA_CHECK_ERROR2(setVec3(to, m_scale, value[0], value[1], value[2]), xformError);
        }
      }
      else
      if(attr_type == utils::UsdDataType::kFloat)
      {
        float value = 0;

        const bool retValue = op.GetAs<float>(&value, usdTime);
        if (!retValue)
        {
          continue;
        }

        const TfToken& opName = opClass.GetName();
        if (opName == UsdMayaXformStackTokens->rotate)
        {
          switch(op.GetOpType())
          {
          case UsdGeomXformOp::TypeRotateX:
            AL_MAYA_CHECK_ERROR2(setAngle(to, m_rotationX, MAngle(value, MAngle::kDegrees)), xformError);
            break;

          case UsdGeomXformOp::TypeRotateY:
            AL_MAYA_CHECK_ERROR2(setAngle(to, m_rotationY, MAngle(value, MAngle::kDegrees)), xformError);
            break;

          case UsdGeomXformOp::TypeRotateZ:
            AL_MAYA_CHECK_ERROR2(setAngle(to, m_rotationZ, MAngle(value, MAngle::kDegrees)), xformError);
            break;

          default:
            break;
          }
        }
        else if (opName == UsdMayaXformStackTokens->rotateAxis)
        {
          switch(op.GetOpType())
          {
          case UsdGeomXformOp::TypeRotateX:
            AL_MAYA_CHECK_ERROR2(setAngle(to, m_rotateAxisX, MAngle(value, MAngle::kDegrees)), xformError);
            break;

          case UsdGeomXformOp::TypeRotateY:
            AL_MAYA_CHECK_ERROR2(setAngle(to, m_rotateAxisY, MAngle(value, MAngle::kDegrees)), xformError);
            break;

          case UsdGeomXformOp::TypeRotateZ:
            AL_MAYA_CHECK_ERROR2(setAngle(to, m_rotateAxisZ, MAngle(value, MAngle::kDegrees)), xformError);
            break;

          default:
            break;
          }
        }
      }
      else
      if(attr_type == utils::UsdDataType::kMatrix4d)
      {
        if(opClass.GetName() == UsdMayaXformStackTokens->shear)
        {
          GfMatrix4d value;
          const bool retValue = op.GetAs<GfMatrix4d>(&value, usdTime);
          if(!retValue)
          {
            continue;
          }

          const float shearX = value[1][0];
          const float shearY = value[2][0];
          const float shearZ = value[2][1];
          AL_MAYA_CHECK_ERROR2(setVec3(to, m_shear, shearX, shearY, shearZ), xformError);
        }
        // Don't have to worry about any transforms other than shear here, because we've only
        // matched against MayaStack() and CommonStack() - the MatrixStack() case is handled
        // in the generic transform case, below
      }
    }
  }
  else
  {
    GfMatrix4d value;
    if(xformSchema.GetLocalTransformation(&value, &resetsXformStack, usdTime))
    {
      MFnTransform(to).set(AL::usdmaya::utils::matrixToMTransformationMatrix(value));
    }
  }

  AL_MAYA_CHECK_ERROR2(setBool(to, m_inheritsTransform, !resetsXformStack), xformError);

  processMetaData(from, to, params);
  if (UsdAttribute myAttr = from.GetAttribute(UsdGeomTokens->visibility))
  {
    DgNodeHelper::setVisAttrAnim(to, m_visibility, myAttr);
  }

  return MS::kSuccess;
}

//----------------------------------------------------------------------------------------------------------------------
MStatus TransformTranslator::processMetaData(const UsdPrim& from, MObject& to, const ImporterParams& params)
{
  UsdMetadataValueMap map = from.GetAllAuthoredMetadata();
  auto it = map.begin();
  auto end = map.end();
  for(; it != end; ++it)
  {
    //const TfToken& token = it->first;
    //const VtValue& value = it->second;
    //const TfType& type = value.GetType();
  }
  return MS::kSuccess;
}

//----------------------------------------------------------------------------------------------------------------------
bool animationCheck(AnimationTranslator* animTranslator, MPlug plug)
{
  if(!animTranslator) return false;
  return animTranslator->isAnimated(plug, true);
}

//----------------------------------------------------------------------------------------------------------------------
UsdAttribute addTranslateOp(
    const UsdGeomXform& xformSchema,
    TfToken attrName,
    const GfVec3f& currentValue,
    const UsdTimeCode& time,
    bool invert=false)
{
  UsdGeomXformOp op = xformSchema.AddTranslateOp(UsdGeomXformOp::PrecisionFloat, attrName, invert);
  if (!invert)
  {
    op.Set(currentValue, time);
  }
  return op.GetAttr();
}

//----------------------------------------------------------------------------------------------------------------------
UsdAttribute addRotateOp(
    const UsdGeomXform &xformSchema,
    TfToken attrName,
    const int32_t& rotateOrder,
    const GfVec3f& rotation,
    const UsdTimeCode& time)
{
  UsdGeomXformOp op;
  switch(rotateOrder)
  {
  case MEulerRotation::kXYZ:
    op = xformSchema.AddRotateXYZOp(UsdGeomXformOp::PrecisionFloat, attrName);
    break;

  case MEulerRotation::kXZY:
    op = xformSchema.AddRotateXZYOp(UsdGeomXformOp::PrecisionFloat, attrName);
    break;

  case MEulerRotation::kYXZ:
    op = xformSchema.AddRotateYXZOp(UsdGeomXformOp::PrecisionFloat, attrName);
    break;

  case MEulerRotation::kYZX:
    op = xformSchema.AddRotateYZXOp(UsdGeomXformOp::PrecisionFloat, attrName);
    break;

  case MEulerRotation::kZXY:
    op = xformSchema.AddRotateZXYOp(UsdGeomXformOp::PrecisionFloat, attrName);
    break;

  case MEulerRotation::kZYX:
    op = xformSchema.AddRotateZYXOp(UsdGeomXformOp::PrecisionFloat, attrName);
    break;

  default:
    break;
  }
  op.Set(rotation, time);
  return op.GetAttr();
}

//----------------------------------------------------------------------------------------------------------------------
MStatus TransformTranslator::copyAttributes(const MObject& from, UsdPrim& to, const ExporterParams& params, const MDagPath& path)
{
  UsdGeomXform xformSchema(to);
  GfVec3f scale;
  GfVec3f shear;
  GfVec3f rotation;
  int32_t rotateOrder;
  GfVec3f rotateAxis;
  GfVec3f translation;
  GfVec3f scalePivot;
  GfVec3f rotatePivot;
  GfVec3f scalePivotTranslate;
  GfVec3f rotatePivotTranslate;
  bool inheritsTransform;
  bool visible;

  static const GfVec3f defaultScale(1.0f);
  static const GfVec3f defaultShear(0.0f);
  static const GfVec3f defaultRotation(0.0f);
  static const GfVec3f defaultRotateAxis(0.0f);
  static const GfVec3f defaultTranslation(0.0f);
  static const GfVec3f defaultScalePivot(0.0f);
  static const GfVec3f defaultRotatePivot(0.0f);
  static const GfVec3f defaultScalePivotTranslate(0.0f);
  static const GfVec3f defaultRotatePivotTranslate(0.0f);
  static const bool defaultVisible(true);

  const float radToDeg = 57.295779506f;
  AnimationTranslator* animTranslator = params.m_animTranslator;

  // Check if transform attributes are considered animated,
  // if true, we consider translation, rotation, rotateOrder and scale attributes are animated:
  bool transformAnimated =  false;
  if(params.m_extensiveAnimationCheck)
  {
    transformAnimated = animTranslator->isAnimatedTransform(from);
  }

  if(!params.m_exportInWorldSpace)
  {
    getBool(from, m_inheritsTransform, inheritsTransform);
    getBool(from, m_visible, visible);
    getVec3(from, m_scale, (float*)&scale);
    getVec3(from, m_shear, (float*)&shear);
    getVec3(from, m_rotation, (float*)&rotation);
    getInt32(from, m_rotateOrder, rotateOrder);
    getVec3(from, m_rotateAxis, (float*)&rotateAxis);
    getVec3(from, m_translation, (float*)&translation);
    getVec3(from, m_scalePivot, (float*)&scalePivot);
    getVec3(from, m_rotatePivot, (float*)&rotatePivot);
    getVec3(from, m_scalePivotTranslate, (float*)&scalePivotTranslate);
    getVec3(from, m_rotatePivotTranslate, (float*)&rotatePivotTranslate);

    // For insurance, we will make sure there aren't any ordered ops before we start
    xformSchema.ClearXformOpOrder();

    // This adds an op to the stack so we should do it after ClearXformOpOrder():
    xformSchema.SetResetXformStack(!inheritsTransform);

    bool plugAnimated = animationCheck(animTranslator, MPlug(from, m_visible));
    if (plugAnimated || visible != defaultVisible)
    {
      UsdAttribute visibleAttr = xformSchema.GetVisibilityAttr();

      if (plugAnimated && animTranslator)
      {
        animTranslator->forceAddTransformPlug(MPlug(from, m_visible), visibleAttr);
      }
      else
      {
        visibleAttr.Set(visible ? UsdGeomTokens->inherited : UsdGeomTokens->invisible);
      }
    }

    plugAnimated = transformAnimated || animationCheck(animTranslator, MPlug(from, m_translation));
    if(plugAnimated || translation != defaultTranslation)
    {
      UsdAttribute translateAttr = addTranslateOp(xformSchema, UsdMayaXformStackTokens->translate, translation, params.m_timeCode);
      if(plugAnimated && animTranslator) animTranslator->forceAddPlug(MPlug(from, m_translation), translateAttr);
    }

    plugAnimated = animationCheck(animTranslator, MPlug(from, m_rotatePivotTranslate));
    if(plugAnimated || rotatePivotTranslate != defaultRotatePivotTranslate)
    {
      UsdAttribute rotatePivotTranslateAttr = addTranslateOp(xformSchema, UsdMayaXformStackTokens->rotatePivotTranslate, rotatePivotTranslate, params.m_timeCode);
      if(plugAnimated && animTranslator) animTranslator->forceAddPlug(MPlug(from, m_rotatePivotTranslate), rotatePivotTranslateAttr);
    }

    plugAnimated = animationCheck(animTranslator, MPlug(from, m_rotatePivot));
    bool makeRotatePivot = plugAnimated || rotatePivot != defaultRotatePivot;
    if(makeRotatePivot)
    {
      UsdAttribute rotatePivotAttr = addTranslateOp(xformSchema, UsdMayaXformStackTokens->rotatePivot, rotatePivot, params.m_timeCode);
      if(plugAnimated && animTranslator) animTranslator->forceAddPlug(MPlug(from, m_rotatePivot), rotatePivotAttr);
    }

    plugAnimated = transformAnimated || animationCheck(animTranslator, MPlug(from, m_rotation));
    if(plugAnimated || rotation != defaultRotation)
    {
      rotation *= radToDeg;
      UsdAttribute rotateAttr = addRotateOp(xformSchema, UsdMayaXformStackTokens->rotate, rotateOrder, rotation, params.m_timeCode);
      if(plugAnimated && animTranslator) animTranslator->forceAddPlug(MPlug(from, m_rotation), rotateAttr, radToDeg);
    }

    plugAnimated = animationCheck(animTranslator, MPlug(from, m_rotateAxis));
    if(plugAnimated || rotateAxis != defaultRotateAxis)
    {
      rotateAxis *= radToDeg;
      UsdAttribute rotateAxisAttr = addRotateOp(xformSchema, UsdMayaXformStackTokens->rotateAxis, MEulerRotation::kXYZ, rotateAxis, params.m_timeCode);
      if(plugAnimated && animTranslator) animTranslator->forceAddPlug(MPlug(from, m_rotateAxis), rotateAxisAttr, radToDeg);
    }

    if(makeRotatePivot)
    {
      addTranslateOp(xformSchema, UsdMayaXformStackTokens->rotatePivot, rotatePivot, params.m_timeCode, true);
    }

    plugAnimated = animationCheck(animTranslator, MPlug(from, m_scalePivotTranslate));
    if(plugAnimated || scalePivotTranslate != defaultScalePivotTranslate)
    {
      UsdAttribute scalePivotTranslateAttr = addTranslateOp(xformSchema, UsdMayaXformStackTokens->scalePivotTranslate, scalePivotTranslate, params.m_timeCode);
      if(plugAnimated && animTranslator) animTranslator->forceAddPlug(MPlug(from, m_scalePivotTranslate), scalePivotTranslateAttr);
    }

    plugAnimated = animationCheck(animTranslator, MPlug(from, m_scalePivot));
    bool makeScalePivot = plugAnimated || scalePivot != defaultScalePivot;
    if(makeScalePivot)
    {
      UsdAttribute scalePivotAttr = addTranslateOp(xformSchema, UsdMayaXformStackTokens->scalePivot, scalePivot, params.m_timeCode);
      if(plugAnimated && animTranslator) animTranslator->forceAddPlug(MPlug(from, m_scalePivot), scalePivotAttr);
    }

    if(shear != defaultShear)
    {
      GfMatrix4d shearMatrix(
          1.0f, 0.0f, 0.0f, 0.0f,
          shear[0], 1.0f, 0.0f, 0.0f,
          shear[1], shear[2], 1.0f, 0.0f,
          0.0f, 0.0f, 0.0f, 1.0f);
      UsdGeomXformOp op = xformSchema.AddTransformOp(UsdGeomXformOp::PrecisionDouble, UsdMayaXformStackTokens->shear);
      op.Set(shearMatrix, params.m_timeCode);
    }

    plugAnimated = transformAnimated || animationCheck(animTranslator, MPlug(from, m_scale));
    if(plugAnimated || scale != defaultScale)
    {
      UsdGeomXformOp op = xformSchema.AddScaleOp(UsdGeomXformOp::PrecisionFloat, UsdMayaXformStackTokens->scale);
      op.Set(scale, params.m_timeCode);
      if(plugAnimated && animTranslator) animTranslator->forceAddPlug(MPlug(from, m_scale), op.GetAttr());
    }

    if(makeScalePivot)
    {
      addTranslateOp(xformSchema, UsdMayaXformStackTokens->scalePivot, scalePivot, params.m_timeCode, true);
    }
  }
  else
  {
    MMatrix wsm = path.inclusiveMatrix();
    auto op = xformSchema.AddTransformOp(UsdGeomXformOp::PrecisionDouble, UsdMayaXformStackTokens->transform);
    op.Set(*(const GfMatrix4d*)&wsm, params.m_timeCode);
    if(animTranslator) animTranslator->addWorldSpace(path, op.GetAttr());
  }

  return MS::kSuccess;
}

//----------------------------------------------------------------------------------------------------------------------
void TransformTranslator::copyAttributeValue(const MPlug& plug, UsdAttribute& usdAttr, const UsdTimeCode& timeCode)
{
  MObject node = plug.node();
  MObject attribute = plug.attribute();
  static const TfToken visToken = UsdGeomTokens->visibility;
  if (usdAttr.GetName() == visToken)
  {
    bool value;
    getBool(node, attribute, value);
    usdAttr.Set(value ? UsdGeomTokens->inherited : UsdGeomTokens->invisible, timeCode);
  }
}

//----------------------------------------------------------------------------------------------------------------------
} // translators
} // fileio
} // usdmaya
} // AL
//----------------------------------------------------------------------------------------------------------------------
