//
// Copyright 2019 Animal Logic
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
//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#include <vector>
#include "AL/usdmaya/nodes/Engine.h"

namespace AL {
namespace usdmaya {
namespace nodes {

Engine::Engine(const SdfPath& rootPath, const SdfPathVector& excludedPaths)
  : UsdImagingGLEngine(rootPath, excludedPaths) {}


bool Engine::TestIntersectionBatch(
  const GfMatrix4d &viewMatrix,
  const GfMatrix4d &projectionMatrix,
  const GfMatrix4d &worldToLocalSpace,
  const SdfPathVector& paths,
  const UsdImagingGLRenderParams& params,
  const TfToken &intersectionMode,
  unsigned int pickResolution,
  HdxIntersector::HitVector& outHits)
{
  if (ARCH_UNLIKELY(_legacyImpl))
  {
    return false;
  }

  TF_VERIFY(_delegate);

  return TestIntersectionBatch(
      viewMatrix,
      projectionMatrix,
      worldToLocalSpace,
      paths,
      params,
      intersectionMode,
      pickResolution,
      _intersectCollection,
      *_taskController,
      _engine,
      _renderTags,
      outHits);
}

/*static*/
bool Engine::TestIntersectionBatch(
  const GfMatrix4d &viewMatrix,
  const GfMatrix4d &projectionMatrix,
  const GfMatrix4d &worldToLocalSpace,
  const SdfPathVector& paths,
  const UsdImagingGLRenderParams& params,
  const TfToken &intersectionMode,
  unsigned int pickResolution,
  HdRprimCollection& intersectCollection,
  HdxTaskController& taskController,
  HdEngine& engine,
  TfTokenVector& outRenderTags,
  HdxIntersector::HitVector& outHits)
{
  _UpdateHydraCollection(&intersectCollection, paths, params, &outRenderTags);

  HdxIntersector::Params qparams;
  qparams.viewMatrix = worldToLocalSpace * viewMatrix;
  qparams.projectionMatrix = projectionMatrix;
  qparams.alphaThreshold = params.alphaThreshold;
  switch (params.cullStyle)
  {
    case UsdImagingGLCullStyle::CULL_STYLE_NO_OPINION:
      qparams.cullStyle = HdCullStyleDontCare;
      break;
    case UsdImagingGLCullStyle::CULL_STYLE_NOTHING:
      qparams.cullStyle = HdCullStyleNothing;
      break;
    case UsdImagingGLCullStyle::CULL_STYLE_BACK:
      qparams.cullStyle = HdCullStyleBack;
      break;
    case UsdImagingGLCullStyle::CULL_STYLE_FRONT:
      qparams.cullStyle = HdCullStyleFront;
      break;
    case UsdImagingGLCullStyle::CULL_STYLE_BACK_UNLESS_DOUBLE_SIDED:
      qparams.cullStyle = HdCullStyleBackUnlessDoubleSided;
      break;
    default:
      qparams.cullStyle = HdCullStyleDontCare;
  }
  qparams.renderTags = outRenderTags;
  qparams.enableSceneMaterials = params.enableSceneMaterials;

  taskController.SetPickResolution(pickResolution);

  return taskController.TestIntersection(&engine, intersectCollection, qparams, intersectionMode, &outHits);
}

}
}
}
