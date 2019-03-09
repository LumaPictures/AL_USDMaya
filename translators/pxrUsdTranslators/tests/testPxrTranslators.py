import unittest
import tempfile
import os

import maya.cmds as mc

from pxr import Usd, Sdf, UsdGeom, Gf

class TestTranslator(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        mc.file(f=True, new=True)
        mc.loadPlugin('pxrUsd')
        mc.loadPlugin('AL_USDMayaPlugin')

    @classmethod
    def tearDown(cls):
        mc.file(f=True, new=True)
        
    def testExportProxyShapes(self):
        import AL.usdmaya
        
        tempFile = tempfile.NamedTemporaryFile(
            suffix=".usda", prefix="AL_USDMayaTests_exportProxyShape_", delete=True)
        
        mc.createNode("transform", n="world")
        mc.createNode("transform", n="geo", p="world")

        SPHERE_USD = "{}/sphere2.usda".format(os.environ.get('TEST_DIR'))
        
        # create one proxyShape with a time offset
        mc.select(clear=1)
        proxyShapeNode = mc.AL_usdmaya_ProxyShapeImport(file=SPHERE_USD)[0]
        proxyShape = AL.usdmaya.ProxyShape.getByName(proxyShapeNode)
        proxyParentNode1 = mc.listRelatives(proxyShapeNode, fullPath=1, parent=1)[0]
        proxyParentNode1 = mc.parent(proxyParentNode1, "geo")[0]
        print(proxyParentNode1)
        # force the stage to load
        stage = proxyShape.getUsdStage()
        self.assertTrue(stage)
        mc.setAttr(proxyShapeNode + ".timeOffset", 40)
        mc.setAttr(proxyShapeNode + ".timeScalar", 2)
        
        # create another proxyShape with a few session layer edits
        mc.select(clear=1)
        proxyShapeNode2 = mc.AL_usdmaya_ProxyShapeImport(file=SPHERE_USD)[0]
        proxyShape2 = AL.usdmaya.ProxyShape.getByName(proxyShapeNode2)
        proxyParentNode2 = mc.listRelatives(proxyShapeNode2, fullPath=1, parent=1)[0]
        proxyParentNode2 = mc.parent(proxyParentNode2, "geo")[0]
        print(proxyParentNode2)
        # force the stage to load
        stage2 = proxyShape2.getUsdStage()
        self.assertTrue(stage2)
        
        
        session = stage2.GetSessionLayer()
        stage2.SetEditTarget(session)
        extraPrimPath = "/pExtraPrimPath"
        secondSpherePath = "/pSphereShape2"
        stage2.DefinePrim("/pSphere1" + extraPrimPath)
        existingSpherePath = "/pSphere1" + secondSpherePath
        self.assertTrue(stage2.GetPrimAtPath(existingSpherePath))
        stage2.DefinePrim(existingSpherePath).SetActive(False)

        # create a third proxyShape with some transformations
        mc.select(clear=1)
        proxyShapeNode3 = mc.AL_usdmaya_ProxyShapeImport(file=SPHERE_USD)[0]
        proxyShape3 = AL.usdmaya.ProxyShape.getByName(proxyShapeNode3)
        proxyParentNode3 = mc.listRelatives(proxyShapeNode3, fullPath=1, parent=1)[0]
        proxyParentNode3 = mc.parent(proxyParentNode3, "geo")[0]
        print(proxyParentNode3)
        # force the stage to load
        stage3 = proxyShape3.getUsdStage()
        self.assertTrue(stage3)

        # translate maya node by 1
        mc.select(proxyParentNode3, replace=1)
        mc.move(1, 0, 0)
        mc.rotate(90, 0, 0)

        # make edits on top prim which should be added on export.
        session = stage3.GetSessionLayer()
        stage3.SetEditTarget(session)
        self.assertTrue(stage3.GetPrimAtPath(existingSpherePath))
        topPrim = stage3.GetPrimAtPath("/pSphere1")
        xform = UsdGeom.Xformable(topPrim)
        translateOp = xform.AddTranslateOp()
        translateOp.Set((-2, -2, -2))
        scaleOp = xform.AddScaleOp()
        scaleOp.Set((5, 5, 5))
        # make edits below top prim which should be preserved
        self.assertTrue(stage3.GetPrimAtPath(existingSpherePath))
        stage3.DefinePrim(existingSpherePath).SetActive(False)

        print('== proxy shape 3 session layer ==')
        print(session.ExportToString())
        print('== end session layer ==')

        # perform export
        mc.select("world")
        mc.usdExport(f=tempFile.name)

        with open(tempFile.name, 'r') as f:
            for l in f.readlines():
                print l
        
        resultStage = Usd.Stage.Open(tempFile.name)
        self.assertTrue(resultStage)
        rootLayer = resultStage.GetRootLayer()
        
        refPrimPath = "/world/geo/" + proxyParentNode1
        refPrimPath2 = "/world/geo/" + proxyParentNode2
        refPrimPath3 = "/world/geo/" + proxyParentNode3
        print("Ref Prim Path 1: " + refPrimPath)
        print("Ref Prim Path 2: " + refPrimPath2)
        print("Ref Prim Path 3: " + refPrimPath3)
        print("Resulting stage contents:")
        print(rootLayer.ExportToString())
        
        # Check proxyShape1
        # make sure references were created and that they have correct offset + scale
        refSpec = rootLayer.GetPrimAtPath(refPrimPath)
        self.assertTrue(refSpec)
        self.assertTrue(refSpec.hasReferences)
        refs = refSpec.referenceList.GetAddedOrExplicitItems()
        self.assertEqual(refs[0].layerOffset, Sdf.LayerOffset(40, 2))
        
        # Check proxyShape2
        # make sure the session layer was properly grafted on
        refPrim2 = resultStage.GetPrimAtPath(refPrimPath2)
        self.assertTrue(refPrim2.IsValid())
        self.assertEqual(refPrim2.GetTypeName(), "Xform")
        self.assertEqual(refPrim2.GetSpecifier(), Sdf.SpecifierDef)
        
        refSpec2 = rootLayer.GetPrimAtPath(refPrimPath2)
        self.assertTrue(refSpec2)
        self.assertTrue(refSpec2.hasReferences)
        # ref root should be a defined xform on the main export layer also
        self.assertEqual(refSpec2.typeName, "Xform")
        self.assertEqual(refSpec2.specifier, Sdf.SpecifierDef)
        
        spherePrimPath = refPrimPath2 + secondSpherePath
        spherePrim = resultStage.GetPrimAtPath(spherePrimPath)
        self.assertTrue(spherePrim.IsValid())
        self.assertFalse(spherePrim.IsActive())
        # check that the proper specs are being created
        specOnExportLayer = rootLayer.GetPrimAtPath(spherePrimPath)
        self.assertEqual(specOnExportLayer.specifier, Sdf.SpecifierOver)

        # Check proxyShape3
        # make sure the session layer was properly grafted on
        refPrim3 = resultStage.GetPrimAtPath(refPrimPath3)
        self.assertTrue(refPrim3.IsValid())
        self.assertEqual(refPrim3.GetTypeName(), "Xform")
        self.assertEqual(refPrim3.GetSpecifier(), Sdf.SpecifierDef)
        translate, rotate, scale, shear, rotationOrder = UsdGeom.XformCommonAPI(refPrim3).GetXformVectors(0)
        self.assertTrue(Gf.IsClose(translate,
                                   # this is correct because of the 90 rotation that happens
                                   # before the child transform
                                   Gf.Vec3d(-1.0, 2.0, -2.0),
                                   0.00001), 'value mismatch: %s' % translate)
        self.assertTrue(Gf.IsClose(Gf.Vec3d(rotate),
                                   Gf.Vec3d(90, 0, 0),
                                   0.00001), 'value mismatch: %s' % rotate)
        self.assertTrue(Gf.IsClose(Gf.Vec3d(scale),
                                   Gf.Vec3d(5, 5, 5),
                                   0.00001), 'value mismatch: %s' % scale)

        
tests = unittest.TestLoader().loadTestsFromTestCase(TestTranslator)
result = unittest.TextTestRunner(verbosity=2).run(tests)

mc.quit(exitCode=(not result.wasSuccessful()))