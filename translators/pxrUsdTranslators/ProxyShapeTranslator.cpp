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
#include "ProxyShapeTranslator.h"

#include "pxr/pxr.h"

#include "AL/maya/utils/Utils.h"
#include "AL/usdmaya/fileio/translators/TranslatorBase.h"
#include "AL/usdmaya/nodes/ProxyShape.h"
#include "AL/usdmaya/DebugCodes.h"

#include "usdMaya/jobArgs.h"
#include "usdMaya/primWriterArgs.h"
#include "usdMaya/primWriterContext.h"
#include "usdMaya/primWriterRegistry.h"
#include "usdMaya/util.h"

#include "pxr/base/tf/token.h"
#include "pxr/usd/kind/registry.h"
#include "pxr/usd/sdf/schema.h"
#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/copyUtils.h"
#include "pxr/usd/usd/modelAPI.h"
#include "pxr/usd/usdGeom/tokens.h"
#include "pxr/usd/usdGeom/xformable.h"
#include "pxr/usd/usdGeom/xformOp.h"

PXR_NAMESPACE_OPEN_SCOPE

/* static */
bool
AL_USDMayaTranslatorProxyShape::Create(
        const UsdMayaPrimWriterArgs& args,
        UsdMayaPrimWriterContext* context)
{
  UsdStageRefPtr stage = context->GetUsdStage();
  SdfPath authorPath = context->GetAuthorPath();
  UsdTimeCode usdTime = context->GetTimeCode();

  context->SetExportsGprims(false);
  context->SetPruneChildren(true);
  context->SetModelPaths({authorPath});

  UsdPrim prim = stage->DefinePrim(authorPath);
  if (!prim)
  {
    MString errorMsg("Failed to create prim for USD reference proxyShape at path: ");
    errorMsg += MString(authorPath.GetText());
    MGlobal::displayError(errorMsg);
    return false;
  }

  // only write references when time is default
  if (!usdTime.IsDefault())
  {
    return true;
  }

  const MDagPath& currPath = args.GetMDagPath();
  const MFnDagNode proxyShapeNode(currPath);
  auto proxyShape = (AL::usdmaya::nodes::ProxyShape*)proxyShapeNode.userNode();

  std::string refPrimPathStr;
  MPlug usdRefPrimPathPlg = proxyShape->primPathPlug();
  if (!usdRefPrimPathPlg.isNull())
  {
    refPrimPathStr = AL::maya::utils::convert(usdRefPrimPathPlg.asString());
  }

  // Guard against a situation where the prim being referenced has
  // xformOp's specified in its xformOpOrder but the reference assembly
  // in Maya has an identity transform. We would normally skip writing out
  // the xformOpOrder, but that isn't correct since we would inherit the
  // xformOpOrder, which we don't want.
  // Instead, always write out an empty xformOpOrder if the transform writer
  // did not write out an xformOpOrder in its constructor. This guarantees
  // that we get an identity transform as expected (instead of inheriting).
  bool resetsXformStack;
  UsdGeomXformable xformable(prim);
  std::vector<UsdGeomXformOp> orderedXformOps =
          xformable.GetOrderedXformOps(&resetsXformStack);
  if (orderedXformOps.empty() && !resetsXformStack)
  {
    xformable.CreateXformOpOrderAttr().Block();
  }

  // Get proxy shape stage and graft session layer onto exported layer.
  // Do this before authoring anything to prim because CopySpec will
  // replace any scene description.
  UsdStageRefPtr shapeStage = proxyShape->usdStage();
  SdfPrimSpecHandle sessionSpec;
  if (shapeStage)
  {
    SdfPath srcPrimPath;
    if (!refPrimPathStr.empty())
    {
      srcPrimPath = SdfPath(refPrimPathStr);
    }
    else
    {
      srcPrimPath = shapeStage->GetDefaultPrim().GetPath();
    }

    sessionSpec = shapeStage->GetSessionLayer()->GetPrimAtPath(srcPrimPath);
    if (sessionSpec)
    {
      // Since there are currently some bugs with selectively copying properties in SdfCopySpec,
      // We just copy the children of the srcPrimPath. Below we will explicitly
      // copy any transformation ops because those are the most common, but ideally we would
      // copy specs for the root onto the authorPath also. (We would filter xforms so they
      // could still be appended below).
      TF_FOR_ALL(child, shapeStage->GetSessionLayer()->GetPrimAtPath(srcPrimPath)->GetNameChildren())
      {
        SdfCopySpec(shapeStage->GetSessionLayer(), child->GetSpec().GetPath(),
                    stage->GetRootLayer(), authorPath.AppendChild(child->GetSpec().GetNameToken()));
      }
    }
  }

  // Append any xformOps from the session layer root prim onto the stage.
  // This will ensure we produce the same usd result as is displayed in maya.
  if (sessionSpec)
  {
    // If xforms exist both on the maya node and on the target prim of the
    // session layer, we want to add a suffix to the maya transformations.
    // Otherwise we just add the prim transformations in.
    TfToken suffix;
    suffix = (orderedXformOps.empty() ? TfToken() : TfToken("maya_merged"));

    std::vector<SdfPropertySpec> opProps;
    if (SdfPropertySpecHandle sessionSpecOpOrder = sessionSpec->GetPropertyAtPath(SdfPath(".xformOpOrder")))
    {
      if (sessionSpecOpOrder->HasDefaultValue())
      {
        // UsdGeomXformOp::_tokens->inverseXformOpPrefix
        TfToken inverseXformOpPrefix("!invert!");

        VtTokenArray opTokValues;
        opTokValues = sessionSpecOpOrder->GetDefaultValue().Get<VtTokenArray>();
        // Add the additional session spec ops in the proper order
        for (const auto& opTokenValue: opTokValues)
        {
          SdfPath relativePropPath("." + opTokenValue.GetString());
          if (SdfPropertySpecHandle prop = sessionSpec->GetPropertyAtPath(relativePropPath))
          {
            TF_AXIOM(UsdGeomXformOp::IsXformOp(prop->GetNameToken()));
            TF_DEBUG(ALUSDMAYA_TRANSLATORS).Msg("Copying op from root session spec: %s, %s",
                                                sessionSpec->GetPath().GetText(), prop->GetName().c_str());
            if (prop->HasDefaultValue())
            {
              std::vector<TfToken> opNameComponents = SdfPath::TokenizeIdentifierAsTokens(prop->GetName());

              bool isInverse;
              TfToken opTypeToken;
              if (opNameComponents[0] == inverseXformOpPrefix)
              {
                isInverse = true;
                opTypeToken = opNameComponents[2];
              }
              else
              {
                isInverse = false;
                opTypeToken = opNameComponents[1];
              }

              UsdGeomXformOp op;
              op = xformable.AddXformOp(UsdGeomXformOp::GetOpTypeEnum(opTypeToken),
                                        UsdGeomXformOp::GetPrecisionFromValueTypeName(prop->GetTypeName()),
                                        suffix,
                                        isInverse);
              if (!isInverse)
              {
                // Note that there is a limitation here where we can only supports static transforms,
                // but for now that is also a limitation with AL_usdmaya_Transforms in general.
                op.Set(prop->GetDefaultValue(), UsdTimeCode::Default());
              }
            }
          }
        }
      }
    }
  }

  MPlug usdRefFilepathPlg = proxyShape->filePathPlug();
  if (!usdRefFilepathPlg.isNull()){
    UsdReferences refs = prim.GetReferences();
    std::string refAssetPath(AL::maya::utils::convert(usdRefFilepathPlg.asString()));

    std::string resolvedRefPath =
            stage->ResolveIdentifierToEditTarget(refAssetPath);

    if (!resolvedRefPath.empty())
    {
      // If an offset has been applied to the proxyShape, we use the values to
      // construct the reference offset so the resulting stage will be look the
      // same.
      auto timeOffsetPlug = proxyShape->timeOffsetPlug();
      auto timeScalarPlug = proxyShape->timeScalarPlug();
      SdfLayerOffset offset(timeOffsetPlug.asMTime().as(MTime::uiUnit()),
                            timeScalarPlug.asDouble());

      if (refPrimPathStr.empty())
      {
        refs.AddReference(refAssetPath, offset);
      }
      else
      {
        SdfPath refPrimPath(refPrimPathStr);
        refs.AddReference(SdfReference(refAssetPath, refPrimPath, offset));
      }
    }
    else
    {
      MString errorMsg("Could not resolve reference '");
      errorMsg += refAssetPath.c_str();
      errorMsg += "'; creating placeholder Xform for <";
      errorMsg += authorPath.GetText();
      errorMsg += ">";
      MGlobal::displayWarning(errorMsg);
      prim.SetDocumentation(errorMsg.asChar());
    }
  }

  bool makeInstanceable = args.GetExportRefsAsInstanceable();
  if (makeInstanceable)
  {
    // When bug/128076 is addressed, the IsGroup() check will become
    // unnecessary and obsolete.
    // XXX This test also needs to fail if there are sub-root overs
    // on the referenceAssembly!
    TfToken kind;
    UsdModelAPI(prim).GetKind(&kind);
    if (!prim.HasAuthoredInstanceable() &&
      !KindRegistry::GetInstance().IsA(kind, KindTokens->group))
    {
      prim.SetInstanceable(true);
    }
  }

  return true;
}

PXRUSDMAYA_DEFINE_WRITER(AL_usdmaya_ProxyShape, args, context)
{
  return AL_USDMayaTranslatorProxyShape::Create(args, context);
}

PXR_NAMESPACE_CLOSE_SCOPE
