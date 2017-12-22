import unittest

import maya.standalone
import maya.cmds as mc

from pxr import Tf

class testTranslatorsPlugin(unittest.TestCase):

	@classmethod
	def setUpClass(cls):
		maya.standalone.initialize()
		mc.loadPlugin('AL_USDMayaPlugin')

	@classmethod
	def tearDownClass(cls):
		maya.standalone.uninitialize()

	def testPluginIsFunctional(self):
		mc.file(new=1, f=1)
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

	def testRefKeepsRefEdits(self):
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

if __name__ == '__main__':
    unittest.main()
