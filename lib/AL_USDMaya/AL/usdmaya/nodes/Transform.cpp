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
#include "AL/usdmaya/StageData.h"
#include "AL/usdmaya/TypeIDs.h"
#include "AL/usdmaya/DebugCodes.h"
#include "AL/usdmaya/nodes/Transform.h"
#include "AL/usdmaya/nodes/TransformationMatrix.h"

#include "maya/MDataBlock.h"
#include "maya/MEvaluationNodeIterator.h"
#include "maya/MGlobal.h"
#include "maya/MNodeMessage.h"
#include "maya/MPlugArray.h"
#include "maya/MPxTransformationMatrix.h"
#include "maya/MPxTransform.h"
#include "maya/MTime.h"

#include "pxr/base/tf/fileUtils.h"
#include "pxr/usd/usd/stageCacheContext.h"
#include "pxr/usd/usdGeom/imageable.h"
#include "pxr/usd/usdGeom/tokens.h"
#include "pxr/usd/usdUtils/stageCache.h"


namespace AL {
namespace usdmaya {
namespace nodes {

//----------------------------------------------------------------------------------------------------------------------
AL_MAYA_DEFINE_NODE(Transform, AL_USDMAYA_TRANSFORM, AL_usdmaya);

//----------------------------------------------------------------------------------------------------------------------
MObject Transform::m_primPath = MObject::kNullObj;
MObject Transform::m_inStageData = MObject::kNullObj;
MObject Transform::m_outStageData = MObject::kNullObj;
MObject Transform::m_time = MObject::kNullObj;
MObject Transform::m_timeOffset = MObject::kNullObj;
MObject Transform::m_timeScalar = MObject::kNullObj;
MObject Transform::m_outTime = MObject::kNullObj;
MObject Transform::m_localTranslateOffset = MObject::kNullObj;
MObject Transform::m_pushToPrim = MObject::kNullObj;
MObject Transform::m_readAnimatedValues = MObject::kNullObj;

// I may need to worry about transforms being deleted accidentally.
// I'm not sure how best to do this
//----------------------------------------------------------------------------------------------------------------------
void Transform::postConstructor()
{
  transform()->setMObject(thisMObject());
}

//----------------------------------------------------------------------------------------------------------------------
Transform::~Transform()
{
}

//----------------------------------------------------------------------------------------------------------------------
MPxTransformationMatrix* Transform::createTransformationMatrix()
{
  TF_DEBUG(ALUSDMAYA_EVALUATION).Msg("Transform::createTransformationMatrix\n");
  return new TransformationMatrix;
}

//----------------------------------------------------------------------------------------------------------------------
MStatus Transform::connectionMade(const MPlug& plug, const MPlug& otherPlug, bool asSrc)
{
  TF_DEBUG(ALUSDMAYA_EVALUATION).Msg("Transform::connectionMade %s\n", plug.name().asChar());
  if(plug == m_inStageData)
  {
    {
      StageData* data = dynamic_cast<StageData*>(otherPlug.asMDataHandle().asPluginData());
      if(data && data->stage)
      {
        MDataBlock dataBlock = forceCache();
        MString path = inputStringValue(dataBlock, m_primPath);
        SdfPath primPath(path.asChar());
        UsdPrim usdPrim = data->stage->GetPrimAtPath(primPath);
        transform()->setPrim(usdPrim);
        outputBoolValue(dataBlock, m_pushToPrim, transform()->pushToPrimEnabled());
        outputBoolValue(dataBlock, m_readAnimatedValues, transform()->readAnimatedValues());
        dirtyMatrix();
      }
      else
      {
        if(data && !data->stage)
        {
          MGlobal::displayWarning(MString("[Transform] usd stage not found"));
        }
        transform()->setPrim(UsdPrim());
        dirtyMatrix();
      }
    }
    return MS::kSuccess;
  }
  return MS::kUnknownParameter;
}

//----------------------------------------------------------------------------------------------------------------------
MStatus Transform::connectionBroken(const MPlug& plug, const MPlug& otherPlug, bool asSrc)
{
  TF_DEBUG(ALUSDMAYA_EVALUATION).Msg("Transform::connectionBroken\n");
  if(plug == m_inStageData)
  {
    transform()->setPrim(UsdPrim());
    return MS::kSuccess;
  }
  return MS::kUnknownParameter;
}

//----------------------------------------------------------------------------------------------------------------------
MStatus Transform::initialise()
{
  TF_DEBUG(ALUSDMAYA_EVALUATION).Msg("Transform::initialise\n");
  const char* const errorString = "Transform::initialise";
  try
  {
    setNodeType(kTypeName);

    addFrame("USD Prim Information");
    m_primPath = addStringAttr("primPath", "pp", kReadable | kWritable | kStorable | kConnectable | kAffectsWorldSpace, true);
    m_inStageData = addDataAttr("inStageData", "isd", StageData::kTypeId, kWritable | kStorable | kConnectable | kHidden | kAffectsWorldSpace);
    m_outStageData = addDataAttr("outStageData", "osd", StageData::kTypeId, kReadable | kStorable | kConnectable | kHidden | kAffectsWorldSpace);

    addFrame("USD Timing Information");
    m_time = addTimeAttr("time", "tm", MTime(0.0), kKeyable | kConnectable | kReadable | kWritable | kStorable | kAffectsWorldSpace);
    m_timeOffset = addTimeAttr("timeOffset", "tmo", MTime(0.0), kKeyable | kConnectable | kReadable | kWritable | kStorable | kAffectsWorldSpace);
    m_timeScalar = addDoubleAttr("timeScalar", "tms", 1.0, kKeyable | kConnectable | kReadable | kWritable | kStorable | kAffectsWorldSpace);
    m_outTime = addTimeAttr("outTime", "otm", MTime(0.0), kConnectable | kReadable | kAffectsWorldSpace);

    addFrame("USD Experimental Features");
    m_localTranslateOffset = addVectorAttr("localTranslateOffset", "lto", MVector(0,0,0), kReadable | kWritable | kStorable | kConnectable | kAffectsWorldSpace);
    m_pushToPrim = addBoolAttr("pushToPrim", "ptp", false, kReadable | kWritable | kStorable);
    m_readAnimatedValues = addBoolAttr("readAnimatedValues", "rav", true, kReadable | kWritable | kStorable | kAffectsWorldSpace);

    mustCallValidateAndSet(m_time);
    mustCallValidateAndSet(m_timeOffset);
    mustCallValidateAndSet(m_timeScalar);
    mustCallValidateAndSet(m_localTranslateOffset);
    mustCallValidateAndSet(m_pushToPrim);
    mustCallValidateAndSet(m_primPath);
    mustCallValidateAndSet(m_readAnimatedValues);

    AL_MAYA_CHECK_ERROR(attributeAffects(m_time, rotate), errorString);
    AL_MAYA_CHECK_ERROR(attributeAffects(m_time, scale), errorString);
    AL_MAYA_CHECK_ERROR(attributeAffects(m_time, translate), errorString);
    AL_MAYA_CHECK_ERROR(attributeAffects(m_time, matrix), errorString);
    AL_MAYA_CHECK_ERROR(attributeAffects(m_time, worldMatrix), errorString);
    AL_MAYA_CHECK_ERROR(attributeAffects(m_primPath, m_outStageData), errorString);
    AL_MAYA_CHECK_ERROR(attributeAffects(m_inStageData, m_outStageData), errorString);
    AL_MAYA_CHECK_ERROR(attributeAffects(m_readAnimatedValues, m_outStageData), errorString);
  }
  catch(const MStatus& status)
  {
    return status;
  }

  addBaseTemplate("AEtransformMain");
  addBaseTemplate("AEtransformNoScroll");
  addBaseTemplate("AEtransformSkinCluster");
  generateAETemplate();

  return MS::kSuccess;
}

//----------------------------------------------------------------------------------------------------------------------
MStatus Transform::compute(const MPlug& plug, MDataBlock& dataBlock)
{
  TF_DEBUG(ALUSDMAYA_EVALUATION).Msg("Transform::compute %s\n", plug.name().asChar());
  if(plug == m_time)
  {
    updateTransform(dataBlock);
    return MS::kSuccess;
  }
  else
  if(plug == m_outTime)
  {
    MTime theTime = (inputTimeValue(dataBlock, m_time) - inputTimeValue(dataBlock, m_timeOffset)) * inputDoubleValue(dataBlock, m_timeScalar);
    outputTimeValue(dataBlock, m_outTime, theTime);
    return MS::kSuccess;
  }
  return MPxTransform::compute(plug, dataBlock);
}

//----------------------------------------------------------------------------------------------------------------------
void Transform::updateTransform(MDataBlock& dataBlock)
{
  TF_DEBUG(ALUSDMAYA_EVALUATION).Msg("Transform::updateTransform\n");

  // compute updated time value
  MTime theTime = (inputTimeValue(dataBlock, m_time) - inputTimeValue(dataBlock, m_timeOffset)) * inputDoubleValue(dataBlock, m_timeScalar);
  outputTimeValue(dataBlock, m_outTime, theTime);

  // update the transformation matrix to the values at the specified time
  TransformationMatrix* m = transform();
  m->updateToTime(UsdTimeCode(theTime.as(MTime::uiUnit())));

  // if translation animation is present, update the translate attribute (or just flag it as clean if no animation exists)
  if(m->hasAnimatedTranslation())
  {
    outputVectorValue(dataBlock, MPxTransform::translate, m->translation(MSpace::kTransform));
  }
  else
  {
    dataBlock.setClean(MPxTransform::translate);
  }

  // if rotation animation is present, update the rotate attribute (or just flag it as clean if no animation exists)
  if(m->hasAnimatedRotation())
  {
    outputEulerValue(dataBlock, MPxTransform::rotate, m->eulerRotation(MSpace::kTransform));
  }
  else
  {
    dataBlock.setClean(MPxTransform::rotate);
  }

  // if scale animation is present, update the scale attribute (or just flag it as clean if no animation exists)
  if(m->hasAnimatedScale())
  {
    outputVectorValue(dataBlock, MPxTransform::scale, m->scale(MSpace::kTransform));
  }
  else
  {
    dataBlock.setClean(MPxTransform::scale);
  }

  // if translation animation is present, update the translate attribute (or just flag it as clean if no animation exists)
  if(m->hasAnimatedMatrix())
  {
    outputVectorValue(dataBlock, MPxTransform::scale, m->scale(MSpace::kTransform));
    outputEulerValue(dataBlock, MPxTransform::rotate, m->eulerRotation(MSpace::kTransform));
    outputVectorValue(dataBlock, MPxTransform::translate, m->translation(MSpace::kTransform));
  }
  else
  {
    dataBlock.setClean(MPxTransform::scale);
    dataBlock.setClean(MPxTransform::rotate);
    dataBlock.setClean(MPxTransform::translate);
  }
}

//----------------------------------------------------------------------------------------------------------------------
// If any value changes, that affects the resulting transform (the non-animated m_localTranslateOffset value is a good
// example), then it only needs to be set here. If an attribute drives one of the TRS components (e.g. 'time' modifies
// the translate / rotate / scale values), then it needs to be set here, and it also needs to be handled in the compute
// method. That doesn't feel quite right to me, to that is how it appears to work? (If you have any better ideas,
// I'm all ears!).
//
MStatus Transform::validateAndSetValue(const MPlug& plug, const MDataHandle& handle, const MDGContext& context)
{
  if (plug.isNull())
    return MS::kFailure;

  if (plug.isLocked())
    return MS::kSuccess;

  if (plug.isChild() && plug.parent().isLocked())
    return MS::kSuccess;

  TF_DEBUG(ALUSDMAYA_EVALUATION).Msg("Transform::validateAndSetValue %s\n", plug.name().asChar());

  // If the time values are changed, store the new values, and then update the transform
  if (plug == m_time || plug == m_timeOffset || plug == m_timeScalar)
  {
    MStatus status = MS::kSuccess;
    MDataBlock dataBlock = forceCache(*(MDGContext *)&context);
    if(plug == m_time)
    {
      outputTimeValue(dataBlock, m_time, handle.asTime());
    }
    else
    if(plug == m_timeOffset)
    {
      outputTimeValue(dataBlock, m_timeOffset, handle.asTime());
    }
    else
    if(plug == m_timeScalar)
    {
      outputDoubleValue(dataBlock, m_timeScalar, handle.asDouble());
    }

    updateTransform(dataBlock);
    return MS::kSuccess;
  }
  else
  // The local translate offset doesn't drive the TRS, so set the value here, and the transformation update
  // should be handled by the MPxTransform without any additional faffing around in compute.
  if(plug == m_localTranslateOffset || plug.parent() == m_localTranslateOffset)
  {
    MPlug parentPlug = plug.parent();
    MVector offset;
    // getting access to the X/Y/Z components of the translation offset is a bit of a faff
    if(plug == m_localTranslateOffset)
    {
      offset = handle.asVector();
    }
    else
    if(parentPlug.child(0) == plug)
    {
      offset.x = handle.asDouble();
    }
    else
    if(parentPlug.child(1) == plug)
    {
      offset.y = handle.asDouble();
    }
    else
    if(parentPlug.child(2) == plug)
    {
      offset.z = handle.asDouble();
    }

    MDataBlock dataBlock = forceCache(*(MDGContext *)&context);
    outputVectorValue(dataBlock, m_localTranslateOffset, offset);
    transform()->setLocalTranslationOffset(offset);
    return MS::kSuccess;
  }
  else
  if(plug == m_pushToPrim)
  {
    MDataBlock dataBlock = forceCache(*(MDGContext *)&context);
    transform()->enablePushToPrim(handle.asBool());
    outputBoolValue(dataBlock, m_pushToPrim, handle.asBool());
    return MS::kSuccess;
  }
  else
  if(plug == m_readAnimatedValues)
  {
    MDataBlock dataBlock = forceCache(*(MDGContext *)&context);
    transform()->enableReadAnimatedValues(handle.asBool());
    outputBoolValue(dataBlock, m_readAnimatedValues, handle.asBool());
    updateTransform(dataBlock);
    return MS::kSuccess;
  }
  else
  if(plug == m_primPath)
  {
    MDataBlock dataBlock = forceCache(*(MDGContext *)&context);
    MString path = handle.asString();
    outputStringValue(dataBlock, m_primPath, path);
    SdfPath primPath(path.asChar());
    StageData* data = inputDataValue<StageData>(dataBlock, m_inStageData);
    if(data)
    {
      UsdPrim usdPrim = data->stage->GetPrimAtPath(primPath);
      transform()->setPrim(usdPrim);
      outputBoolValue(dataBlock, m_pushToPrim, transform()->pushToPrimEnabled());
      outputBoolValue(dataBlock, m_readAnimatedValues, transform()->readAnimatedValues());
      updateTransform(dataBlock);
    }
    else
    {
      transform()->setPrim(UsdPrim());
      outputBoolValue(dataBlock, m_pushToPrim, transform()->pushToPrimEnabled());
      outputBoolValue(dataBlock, m_readAnimatedValues, transform()->readAnimatedValues());
    }
    return MS::kSuccess;
  }

  return MPxTransform::validateAndSetValue(plug, handle, context);
}

//----------------------------------------------------------------------------------------------------------------------
UsdPrim Transform::getUsdPrim(MDataBlock& dataBlock) const
{
  TF_DEBUG(ALUSDMAYA_EVALUATION).Msg("Transform::getUsdPrim\n");
  UsdPrim usdPrim;
  StageData* outData = inputDataValue<StageData>(dataBlock, m_outStageData);
  if(outData && outData->stage)
  {
    usdPrim = (outData->primPath.IsEmpty()) ?
               outData->stage->GetPseudoRoot() :
               outData->stage->GetPrimAtPath(outData->primPath);
  }
  return usdPrim;
}

//----------------------------------------------------------------------------------------------------------------------
bool Transform::isStageValid() const
{
  TF_DEBUG(ALUSDMAYA_EVALUATION).Msg("Transform::isStageValid\n");
  MDataBlock dataBlock = const_cast<Transform*>(this)->forceCache();
  StageData* outData = inputDataValue<StageData>(dataBlock, m_outStageData);
  return outData && outData->stage;
}

//----------------------------------------------------------------------------------------------------------------------
} // nodes
} // usdmaya
} // AL
//----------------------------------------------------------------------------------------------------------------------
