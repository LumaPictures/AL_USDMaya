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
#include "test_usdmaya.h"
#include "AL/usdmaya/Utils.h"

#include "pxr/usd/usdGeom/xformable.h"

#include "maya/MEulerRotation.h"
#include "maya/MGlobal.h"
#include "maya/MFileIO.h"
#include "maya/MFnTransform.h"
#include "maya/MSelectionList.h"

#include <iostream>
#include <fstream>

TEST(ImportCommands, shear)
{
  constexpr double EPSILON = 1e-5;
  constexpr auto USDA_CONTENTS = R"ESC(#usda 1.0
(
defaultPrim = "top"
endTimeCode = 1
startTimeCode = 1
upAxis = "Y"
)

def Xform "top" {
def Xform "shear_components"
{
float xformOp:rotateY = 90
matrix4d xformOp:transform:shear = ( (1, 0, 0, 0), (0.25, 1, 0, 0), (0.5, 0.75, 1, 0), (0, 0, 0, 1) )
uniform token[] xformOpOrder = ["xformOp:rotateY", "xformOp:transform:shear"]
}

def Xform "shear_matrix"
{
matrix4d xformOp:transform = ( (0.0, 0.0, -1.0, 0.0), (0.0, 1.0, -0.25, 0.0), (1.0, 0.75, -0.5, 0.0), (0.0, 0.0, 0.0, 1.0) )
uniform token[] xformOpOrder = ["xformOp:transform"]
}

})ESC";

  const std::string temp_path = "/tmp/AL_USDMayaTests_ImportCommands_shear.usda";
  const double rad90 = GfDegreesToRadians(90.0);

  // write the usda to disk
  {
    std::ofstream usda_file;
    usda_file.open(temp_path);
    usda_file << USDA_CONTENTS;
    usda_file.close();
  }

  MFileIO::newFile(true);

  MString importCmd;
  importCmd.format(MString("AL_usdmaya_ImportCommand -f \"^1s\""), AL::usdmaya::convert(temp_path));
  MGlobal::executeCommand(importCmd);

  MSelectionList sel;
  sel.add("shear_components");
  MObject shearComponentsObj;
  sel.getDependNode(0, shearComponentsObj);
  ASSERT_FALSE(shearComponentsObj.isNull());
  MFnTransform shearComponentsFn(shearComponentsObj);

  sel.clear();
  sel.add("shear_matrix");
  MObject shearMatrixObj;
  sel.getDependNode(0, shearMatrixObj);
  ASSERT_FALSE(shearMatrixObj.isNull());
  MFnTransform shearMatrixFn(shearMatrixObj);

  MEulerRotation expectedRotation;
  double expectedShear[3];

  GfMatrix4d expectedMatrixVals;
  // Read the expected values from the usd stage
  {
    UsdStageRefPtr stage = UsdStage::Open(temp_path);
    UsdPrim shearComponentsPrim = stage->GetPrimAtPath(SdfPath("/top/shear_components"));
    UsdPrim shearMatrixPrim = stage->GetPrimAtPath(SdfPath("/top/shear_matrix"));
    ASSERT_TRUE(shearComponentsPrim.IsValid());
    ASSERT_TRUE(shearMatrixPrim.IsValid());
    UsdGeomXformable shearComponentsXform(shearComponentsPrim);
    UsdGeomXformable shearMatrixXform(shearMatrixPrim);

    // Read the components prim to get expected component rotate / shear.
    bool resetsXform;
    auto componentsXformOps = shearComponentsXform.GetOrderedXformOps(&resetsXform);
    ASSERT_EQ(2, componentsXformOps.size());
    ASSERT_EQ(UsdGeomXformOp::TypeRotateY, componentsXformOps[0].GetOpType());
    float expectedYDegrees;
    componentsXformOps[0].Get(&expectedYDegrees);
    expectedRotation.x = 0.0;
    expectedRotation.y = GfDegreesToRadians(expectedYDegrees);
    expectedRotation.z = 0.0;
    ASSERT_EQ(UsdGeomXformOp::TypeTransform, componentsXformOps[1].GetOpType());
    ASSERT_EQ("xformOp:transform:shear", componentsXformOps[1].GetOpName());
    GfMatrix4d shearMatrix;
    componentsXformOps[1].Get(&shearMatrix);
    expectedShear[0] = shearMatrix[1][0];
    expectedShear[1] = shearMatrix[2][0];
    expectedShear[2] = shearMatrix[2][1];

    // Read the matrix from shearMatrixXform as the expected value
    auto matrixXformOps = shearMatrixXform.GetOrderedXformOps(&resetsXform);
    ASSERT_EQ(1, matrixXformOps.size());
    ASSERT_EQ(UsdGeomXformOp::TypeTransform, matrixXformOps[0].GetOpType());
    shearMatrixXform.GetLocalTransformation(&expectedMatrixVals, &resetsXform);

    // Assert the two xform matrices are about equal
    GfMatrix4d componentsMatrix;
    shearComponentsXform.GetLocalTransformation(&componentsMatrix, &resetsXform);
    for (int i = 0; i < 4; ++i) {
      for (int j = 0; j < 4; ++j) {
        SCOPED_TRACE(TfStringPrintf("i: %d - j: %d", i, j));
        ASSERT_NEAR(expectedMatrixVals[i][j], componentsMatrix[i][j], EPSILON);
      }
    }
  }

  // Now set up a MTransformationMatrix, using the read component values, and
  // confirm that it's matrix is as expected
  MTransformationMatrix expectedXform;
  expectedXform.rotateTo(expectedRotation);
  expectedXform.setShear(expectedShear, MSpace::kObject);
  MMatrix expectedMatrix = expectedXform.asMatrix();

//  std::cout << "Expected rotation: (" << expectedRotation.x << ", " << expectedRotation.y << ", " << expectedRotation.z << ")" << std::endl;
//  std::cout << "Expected shear: (" << expectedShear[0] << ", " << expectedShear[1] << ", " << expectedShear[2] << ")" << std::endl;
//  std::cout << "Expected maya matrix:" << std::endl;
//  for (int i = 0; i < 4; ++i) {
//    for (int j = 0; j < 4; ++j) {
//      std::cout << expectedMatrix[i][j] << ", ";
//    }
//    std::cout << endl;
//  }

  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      SCOPED_TRACE(TfStringPrintf("i: %d - j: %d", i, j));
      ASSERT_NEAR(expectedMatrixVals[i][j], expectedMatrix[i][j], EPSILON);
    }
  }

  // Make sure that for /top/shear_components, the transform brought in
  // is component-wise exactly correct...
  MVector translation;
  MEulerRotation rotation;
  double shear[3];
  MTransformationMatrix xform;

  translation = shearComponentsFn.getTranslation(MSpace::kObject);
  EXPECT_EQ(0.0, translation.x);
  EXPECT_EQ(0.0, translation.y);
  EXPECT_EQ(0.0, translation.z);

  shearComponentsFn.getRotation(rotation);
  EXPECT_EQ(0.0, rotation.x);
  EXPECT_EQ(rad90, rotation.y);
  EXPECT_EQ(0.0, rotation.z);

  shearComponentsFn.getShear(shear);
  EXPECT_EQ(.25, shear[0]);
  EXPECT_EQ(.5, shear[1]);
  EXPECT_EQ(.75, shear[2]);

  xform = shearComponentsFn.transformation();

  EXPECT_TRUE(xform.isEquivalent(expectedXform, EPSILON));

  // For /top/shear_matrix, we only want to make sure the resultant
  // maya matrix is correct - we don't care about how individual
  // components get decomposed
  xform = shearMatrixFn.transformation();
  EXPECT_TRUE(xform.isEquivalent(expectedXform, EPSILON));
}
