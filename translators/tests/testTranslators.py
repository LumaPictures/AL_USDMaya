import unittest
import tempfile
import maya.cmds as mc

from pxr import Tf, Usd, UsdGeom, Gf
import translatortestutils

class TestTranslator(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        mc.loadPlugin('AL_USDMayaPlugin')
            
    @classmethod
    def tearDown(cls):
        mc.file(f=True, new=True)
            
    def testMayaReference_TranslatorExists(self):
        """
        Test that the Maya Reference Translator exists
        """
        self.assertTrue(Tf.Type.Unknown != Tf.Type.FindByName('AL::usdmaya::fileio::translators::MayaReference'))
     
    def testMayaReference_TranslatorImport(self):
        """
        Test that the Maya Reference Translator imports correctly
        """
             
        mc.AL_usdmaya_ProxyShapeImport(file='./testMayaRef.usda')
        self.assertTrue(len(mc.ls('cube:pCube1')))
     
    def testMayaReference_PluginIsFunctional(self):
        mc.AL_usdmaya_ProxyShapeImport(file='./testMayaRef.usda')
        self.assertTrue(Tf.Type.Unknown != Tf.Type.FindByName('AL::usdmaya::fileio::translators::MayaReference'))
        self.assertEqual(1, len(mc.ls('cube:pCube1')))
        self.assertEqual('mayaRefPrim', mc.listRelatives('cube:pCube1', parent=1)[0])
        self.assertEqual((0.0, 0.0, 0.0), cmds.getAttr('mayaRefPrim.translate')[0])
        self.assertFalse(mc.getAttr('mayaRefPrim.translate', lock=1))
        self.assertEqual(1, len(mc.ls('otherNS:pCube1')))
        self.assertEqual('otherCube', mc.listRelatives('otherNS:pCube1', parent=1)[0])
        self.assertEqual((3.0, 2.0, 1.0), cmds.getAttr('otherCube.translate')[0])
        self.assertFalse(mc.getAttr('otherCube.translate', lock=1))
     
    def testMayaReference_RefLoading(self):
        '''Test that Maya References are only loaded when they need to be.'''
        import os
        import maya.api.OpenMaya as om
        import AL.usdmaya
             
        loadHistory = {}
             
    def recordRefLoad(refNodeMobj, mFileObject, clientData):
        '''Record when a reference path is loading.'''
        path = mFileObject.resolvedFullName()
        count = loadHistory.setdefault(path, 0)
        loadHistory[path] = count + 1
             
        id = om.MSceneMessage.addReferenceCallback(
        om.MSceneMessage.kBeforeLoadReference,
        recordRefLoad)
             
        mc.file(new=1, f=1)
        proxyName = mc.AL_usdmaya_ProxyShapeImport(file='./testMayaRefLoading.usda')[0]
        proxy = AL.usdmaya.ProxyShape.getByName(proxyName)
        refPath = os.path.abspath('./cube.ma')
        stage = AL.usdmaya.StageCache.Get().GetAllStages()[0]
        topPrim = stage.GetPrimAtPath('/world/optionalCube')
        loadVariantSet = topPrim.GetVariantSet('state')
             
        self.assertEqual(loadHistory[refPath], 2)  # one for each copy of ref
             
        proxy.resync('/')
        self.assertEqual(loadHistory[refPath], 2)  # refs should not have reloaded since nothing has changed.
             
        # now change the variant, so a third ALMayaReference should load
        loadVariantSet.SetVariantSelection('loaded')
        self.assertEqual(loadHistory[refPath], 3)  # only the new ref should load.
             
        loadVariantSet.SetVariantSelection('unloaded')
        self.assertEqual(loadHistory[refPath], 3)  # ref should unload, but nothing else should load.
             
        om.MMessage.removeCallback(id)
             
    def testMayaReference_RefKeepsRefEdits(self):
        '''Tests that, even if the MayaReference is swapped for a pure-usda representation, refedits are preserved'''
        import AL.usdmaya
             
        mc.file(new=1, f=1)
        mc.AL_usdmaya_ProxyShapeImport(file='./testMayaRefUnloadable.usda')
        stage = AL.usdmaya.StageCache.Get().GetAllStages()[0]
        topPrim = stage.GetPrimAtPath('/usd_top')
        mayaLoadingStyle = topPrim.GetVariantSet('mayaLoadingStyle')
         
    def assertUsingMayaReferenceVariant():
        self.assertEqual(1, len(mc.ls('cubeNS:pCube1')))
        self.assertFalse(stage.GetPrimAtPath('/usd_top/pCube1_usd').IsValid())
        self.assertEqual('mayaReference', mayaLoadingStyle.GetVariantSelection())
         
    def assertUsingUsdVariant():
        self.assertEqual(0, len(mc.ls('cubeNS:pCube1')))
        self.assertTrue(stage.GetPrimAtPath('/usd_top/pCube1_usd').IsValid())
        self.assertEqual('usd', mayaLoadingStyle.GetVariantSelection())
             
        # by default, variant should be set so that it's a maya reference
        assertUsingMayaReferenceVariant()
             
        # check that, by default, the cube is at 1,2,3
        self.assertEqual(mc.getAttr('cubeNS:pCube1.translate')[0], (1.0, 2.0, 3.0))
        # now make a ref edit
        mc.setAttr('cubeNS:pCube1.translate', 4.0, 5.0, 6.0)
        self.assertEqual(mc.getAttr('cubeNS:pCube1.translate')[0], (4.0, 5.0, 6.0))
             
        # now change the variant, so the ALMayaReference isn't part of the stage anymore
        mayaLoadingStyle.SetVariantSelection('usd')
        # confirm that worked
        assertUsingUsdVariant()
             
        # now switch back...
        mayaLoadingStyle.SetVariantSelection('mayaReference')
        assertUsingMayaReferenceVariant()
        # ...and then make sure that our ref edit was preserved
        self.assertEqual(mc.getAttr('cubeNS:pCube1.translate')[0], (4.0, 5.0, 6.0))
 
  
    def testMesh_TranslatorExists(self):
        """
        Test that the Maya Reference Translator exists
        """
        self.assertTrue(Tf.Type.Unknown != Tf.Type.FindByName('AL::usdmaya::fileio::translators::Mesh'))
 
 
    def testMesh_TranslateOff(self):
        """
        Test that by default that the the mesh isn't imported
        """
       
        # Create sphere in Maya and export a .usda file
        mc.polySphere()
        mc.select("pSphere1")
        tempFile = tempfile.NamedTemporaryFile(suffix=".usda", prefix="test_MeshTranslator_", delete=True)
        mc.file(tempFile.name, exportSelected=True, force=True, type="AL usdmaya export")
               
        # clear scene
        mc.file(f=True, new=True)
               
        mc.AL_usdmaya_ProxyShapeImport(file=tempFile.name)
        self.assertTrue(Tf.Type.Unknown != Tf.Type.FindByName('AL::usdmaya::fileio::translators::Mesh'))
        self.assertEqual(len(mc.ls('pSphere1')), 0)
        self.assertEqual(len(mc.ls(type='mesh')), 0)
 
    def testMesh_testTranslateOn(self):
        """
        Test that by default that the the mesh is imported
        """
        # setup scene with sphere
        translatortestutils.importStageWithSphere()
              
        # force the import
        mc.AL_usdmaya_TranslatePrim(ip="/pSphere1", fi=True, proxy="AL_usdmaya_Proxy")
      
        self.assertTrue(len(mc.ls('pSphere1')))
        self.assertEqual(len(mc.ls(type='mesh')), 1)
    
    def testMesh_TranslateRoundTrip(self):
        """
        Test that Translating->TearingDown->Translating roundtrip works
        """
        # setup scene with sphere
     
        # Create sphere in Maya and export a .usda file
        mc.polySphere()
        mc.group( 'pSphere1', name='parent' )
        mc.select("parent")
        tempFile = tempfile.NamedTemporaryFile(suffix=".usda", prefix="test_MeshTranslator_", delete=True)
        mc.file(tempFile.name, exportSelected=True, force=True, type="AL usdmaya export")
              
        # clear scene
        mc.file(f=True, new=True)
              
        mc.AL_usdmaya_ProxyShapeImport(file=tempFile.name)
             
     
        # force the import
        mc.AL_usdmaya_TranslatePrim(ip="/parent/pSphere1", fi=True, proxy="AL_usdmaya_Proxy")
        self.assertEqual(len(mc.ls('pSphere1')), 1)
        self.assertEqual(len(mc.ls(type='mesh')), 1)
        self.assertEqual(len(mc.ls('parent')), 1)
     
        # force the teardown 
        mc.AL_usdmaya_TranslatePrim(tp="/parent/pSphere1", fi=True, proxy="AL_usdmaya_Proxy")
        self.assertEqual(len(mc.ls('pSphere1')), 0)
        self.assertEqual(len(mc.ls(type='mesh')), 0)
        self.assertEqual(len(mc.ls('parent')), 0)
     
        # force the import
        mc.AL_usdmaya_TranslatePrim(ip="/parent/pSphere1", fi=True, proxy="AL_usdmaya_Proxy")
        self.assertEqual(len(mc.ls('pSphere1')), 1)
        self.assertEqual(len(mc.ls(type='mesh')), 1)
        self.assertEqual(len(mc.ls('parent')), 1)
              
      
    def testMesh_PretearDownEditTargetWrite(self):
        """
        Simple test to determine if the edit target gets written to preteardown 
        """
               
        # force the import
        d = translatortestutils.importStageWithSphere()
        stage = d.stage
        mc.AL_usdmaya_TranslatePrim(ip="/pSphere1", fi=True, proxy="AL_usdmaya_Proxy")
      
        stage.SetEditTarget(stage.GetSessionLayer())
       
        # Delete some faces
        mc.select("pSphere1.f[0:399]", r=True)
        mc.select("pSphere1.f[0]", d=True)
        mc.delete()
       
        preSession = stage.GetEditTarget().GetLayer().ExportToString();
        mc.AL_usdmaya_TranslatePrim(tp="/pSphere1", proxy="AL_usdmaya_Proxy")
        postSession = stage.GetEditTarget().GetLayer().ExportToString()
               
        # Ensure data has been written
        sessionStage = Usd.Stage.Open(stage.GetEditTarget().GetLayer())
        sessionSphere = sessionStage.GetPrimAtPath("/pSphere1")

        self.assertTrue(sessionSphere.IsValid())
        self.assertTrue(sessionSphere.GetAttribute("faceVertexCounts"))

    def testMeshTranslator_variantswitch(self):
        mc.AL_usdmaya_ProxyShapeImport(file='./testMeshVariants.usda')
        self.assertTrue(Tf.Type.Unknown != Tf.Type.FindByName('AL::usdmaya::fileio::translators::Mesh'))
        # test initial state has no meshes
        self.assertEqual(len(mc.ls(type='mesh')), 0)
    
        stage = translatortestutils.getStage()
        stage.SetEditTarget(stage.GetSessionLayer())
         
        variantPrim = stage.GetPrimAtPath("/TestVariantSwitch")
        variantSet = variantPrim.GetVariantSet("MeshVariants")
    
        # the MeshA should now be in the scene
        variantSet.SetVariantSelection("ShowMeshA")
        mc.AL_usdmaya_TranslatePrim(ip="/TestVariantSwitch/MeshA", fi=True, proxy="AL_usdmaya_Proxy") # force the import
        self.assertEqual(len(mc.ls('MeshA')), 1)
        self.assertEqual(len(mc.ls('MeshB')), 0)
        self.assertEqual(len(mc.ls(type='mesh')), 1)

        #######
        # the MeshB should now be in the scene

        variantSet.SetVariantSelection("ShowMeshB")
 
        #print stage.ExportToString()
        mc.AL_usdmaya_TranslatePrim(ip="/TestVariantSwitch/MeshB", fi=True, proxy="AL_usdmaya_Proxy") # force the import
        self.assertEqual(len(mc.ls('MeshA')), 0)
        self.assertEqual(len(mc.ls('MeshB')), 1)
        self.assertEqual(len(mc.ls(type='mesh')), 1)

 
        # the MeshA and MeshB should be in the scene
        variantSet.SetVariantSelection("ShowMeshAnB")
        mc.AL_usdmaya_TranslatePrim(ip="/TestVariantSwitch/MeshB", fi=True, proxy="AL_usdmaya_Proxy")
        mc.AL_usdmaya_TranslatePrim(ip="/TestVariantSwitch/MeshA", fi=True, proxy="AL_usdmaya_Proxy")
 
        self.assertEqual(len(mc.ls('MeshA')), 1)
        self.assertEqual(len(mc.ls('MeshB')), 1)
        self.assertEqual(len(mc.ls(type='mesh')), 2)
 
        # switch variant to empty
        variantSet.SetVariantSelection("")
        self.assertEqual(len(mc.ls(type='mesh')), 0)

    def testNurbsCurve_TranslatorExists(self):
        """
        Test that the NurbsCurve Translator exists
        """
        self.assertTrue(Tf.Type.Unknown != Tf.Type.FindByName('AL::usdmaya::fileio::translators::NurbsCurve'))

    def testNurbsCurve_TranslateOff(self):
        """
        Test that by default that the the mesh isn't imported
        """
        stage = translatortestutils.importStageWithNurbsCircle()
        self.assertEqual(len(mc.ls('nurbsCircle1')), 0)
        self.assertEqual(len(mc.ls(type='nurbsCurve')), 0)

    def testNurbsCurve_testTranslateOn(self):
        """
        Test that by default that the the mesh is imported
        """
        # setup scene with sphere
        translatortestutils.importStageWithNurbsCircle()

        # force the import
        mc.AL_usdmaya_TranslatePrim(ip="/nurbsCircle1", fi=True, proxy="AL_usdmaya_Proxy")

        self.assertEqual(len(mc.ls('nurbsCircle1')), 1)
        self.assertEqual(len(mc.ls(type='nurbsCurve')), 1)

    def testNurbsCurve_TranslateRoundTrip(self):
        """
        Test that Translating->TearingDown->Translating roundtrip works
        """
        # setup scene with nurbs circle
        # Create nurbs circle in Maya and export a .usda file
        mc.CreateNURBSCircle()
        mc.group( 'nurbsCircle1', name='parent' )
        mc.select("parent")
        tempFile = tempfile.NamedTemporaryFile(suffix=".usda", prefix="test_NurbsCurveTranslator_", delete=True)
        mc.file(tempFile.name, exportSelected=True, force=True, type="AL usdmaya export")

        # clear scene
        mc.file(f=True, new=True)
        mc.AL_usdmaya_ProxyShapeImport(file=tempFile.name)

        # force the import
        mc.AL_usdmaya_TranslatePrim(ip="/parent/nurbsCircle1", fi=True, proxy="AL_usdmaya_Proxy")
        self.assertEqual(len(mc.ls('nurbsCircle1')), 1)
        self.assertEqual(len(mc.ls(type='nurbsCurve')), 1)
        self.assertEqual(len(mc.ls('parent')), 1)

        # force the teardown
        mc.AL_usdmaya_TranslatePrim(tp="/parent/nurbsCircle1", fi=True, proxy="AL_usdmaya_Proxy")
        self.assertEqual(len(mc.ls('nurbsCircle1')), 0)
        self.assertEqual(len(mc.ls(type='nurbsCurve')), 0)
        self.assertEqual(len(mc.ls('parent')), 0)

        # force the import
        mc.AL_usdmaya_TranslatePrim(ip="/parent/nurbsCircle1", fi=True, proxy="AL_usdmaya_Proxy")
        self.assertEqual(len(mc.ls('nurbsCircle1')), 1)
        self.assertEqual(len(mc.ls(type='nurbsCurve')), 1)
        self.assertEqual(len(mc.ls('parent')), 1)

    def testNurbsCurve_PretearDownEditTargetWrite(self):
        """
        Simple test to determine if the edit target gets written to preteardown
        """

        # force the import
        stage = translatortestutils.importStageWithNurbsCircle()
        mc.AL_usdmaya_TranslatePrim(ip="/nurbsCircle1", fi=True, proxy="AL_usdmaya_Proxy")

        stage.SetEditTarget(stage.GetSessionLayer())

        # Delete a control vertex
        mc.select("nurbsCircle1.cv[6]", r=True)
        mc.delete()

        preSession = stage.GetEditTarget().GetLayer().ExportToString();
        mc.AL_usdmaya_TranslatePrim(tp="/nurbsCircle1", proxy="AL_usdmaya_Proxy")
        postSession = stage.GetEditTarget().GetLayer().ExportToString()

        # Ensure data has been written
        sessionStage = Usd.Stage.Open(stage.GetEditTarget().GetLayer())
        sessionNurbCircle = sessionStage.GetPrimAtPath("/nurbsCircle1")

        self.assertTrue(sessionNurbCircle.IsValid())
        cvcAttr = sessionNurbCircle.GetAttribute("curveVertexCounts")
        self.assertTrue(cvcAttr.IsValid())
        self.assertEqual(len(cvcAttr.Get()), 1)
        self.assertEqual(cvcAttr.Get()[0], 10)

    def testMeshTranslator_multipleTranslations(self):
        path = tempfile.NamedTemporaryFile(suffix=".usda", prefix="test_MeshTranslator_multipleTranslations_", delete=True)

        d = translatortestutils.importStageWithSphere('AL_usdmaya_Proxy')
        sessionLayer = d.stage.GetSessionLayer()
        d.stage.SetEditTarget(sessionLayer)

        spherePrimPath = "/"+d.sphereXformName
        offsetAmount = Gf.Vec3f(0,0.25,0)
        
        vertPoint = '{}.vtx[0]'.format(d.sphereXformName)
        
        spherePrimMesh = UsdGeom.Mesh.Get(d.stage, spherePrimPath)  

        # Test import,modify,teardown a bunch of times
        for i in xrange(3):
            # Determine expected result
            expectedPoint = spherePrimMesh.GetPointsAttr().Get()[0] + offsetAmount

            # Translate the prim into maya for editing
            mc.AL_usdmaya_TranslatePrim(forceImport=True, importPaths=spherePrimPath, proxy='AL_usdmaya_Proxy')

            # Move the point
            items = ['pSphere1.vtx[0]']
            mc.move(offsetAmount[0], offsetAmount[1], offsetAmount[2], items, relative=True) # just affect the Y
            mc.AL_usdmaya_TranslatePrim(teardownPaths=spherePrimPath, proxy='AL_usdmaya_Proxy')
 
            actualPoint = spherePrimMesh.GetPointsAttr().Get()[0]
            
            # Confirm that the edit has come back as expected
            self.assertAlmostEqual(actualPoint[0], expectedPoint[0])
            self.assertAlmostEqual(actualPoint[1], expectedPoint[1])
            self.assertAlmostEqual(actualPoint[2], expectedPoint[2])

tests = unittest.TestLoader().loadTestsFromTestCase(TestTranslator)
result = unittest.TextTestRunner(verbosity=2).run(tests)

mc.quit(exitCode=(not result.wasSuccessful()))