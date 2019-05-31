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

#include "pxr/usdImaging/usdImaging/delegate.h"

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
  const TfToken &resolveMode,
  unsigned int pickResolution,
  HdxPickHitVector& outHits)
{
  if (ARCH_UNLIKELY(_legacyImpl))
  {
    return false;
  }

  TF_VERIFY(_delegate);
  TF_VERIFY(_taskController);

  // Forward scene materials enable option to delegate
  _delegate->SetSceneMaterialsEnabled(params.enableSceneMaterials);

  return TestIntersectionBatch(
      viewMatrix,
      projectionMatrix,
      worldToLocalSpace,
      paths,
      params,
      resolveMode,
      pickResolution,
      _intersectCollection,
      *_taskController,
      _engine,
      outHits);
}

/*static*/
bool Engine::TestIntersectionBatch(
  const GfMatrix4d &viewMatrix,
  const GfMatrix4d &projectionMatrix,
  const GfMatrix4d &worldToLocalSpace,
  const SdfPathVector& paths,
  const UsdImagingGLRenderParams& params,
  const TfToken &resolveMode,
  unsigned int pickResolution,
  HdRprimCollection& intersectCollection,
  HdxTaskController& taskController,
  HdEngine& engine,
  HdxPickHitVector& outHits)
{

  _UpdateHydraCollection(&intersectCollection, paths, params);

  TfTokenVector renderTags;
  _ComputeRenderTags(params, &renderTags);
  taskController.SetRenderTags(renderTags);

  HdxRenderTaskParams hdParams = _MakeHydraUsdImagingGLRenderParams(params);
  taskController.SetRenderParams(hdParams);

  HdxPickHitVector allHits;
  HdxPickTaskContextParams pickParams;
  pickParams.resolution = GfVec2i(pickResolution, pickResolution);
  if (resolveMode == HdxPickTokens->resolveNearestToCenter ||
      resolveMode == HdxPickTokens->resolveNearestToCamera) {
    pickParams.hitMode = HdxPickTokens->hitFirst;
  } else {
    pickParams.hitMode = HdxPickTokens->hitAll;
  }
  pickParams.resolveMode = resolveMode;
  pickParams.viewMatrix = worldToLocalSpace * viewMatrix;
  pickParams.projectionMatrix = projectionMatrix;
  pickParams.clipPlanes = params.clipPlanes;
  pickParams.collection = intersectCollection;
  pickParams.outHits = &allHits;
  VtValue vtPickParams(pickParams);

  engine.SetTaskContextData(HdxPickTokens->pickParams, vtPickParams);
  auto pickingTasks = taskController.GetPickingTasks();
  engine.Execute(taskController.GetRenderIndex(), &pickingTasks);

  return allHits.size() > 0;
}

}
}
}
