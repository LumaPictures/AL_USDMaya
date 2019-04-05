#!/usr/bin/env python

#-
# ===========================================================================
# Copyright 2018 Autodesk, Inc.  All rights reserved.
#
# Use of this software is subject to the terms of the Autodesk license
# agreement provided at the time of installation or download, or which
# otherwise accompanies this software in either electronic or hard copy form.
# ===========================================================================
#+

#-
# ===========================================================================
#                       WARNING: PROTOTYPE CODE
#
# The code in this file is intended as an engineering prototype, to demonstrate
# UFE integration in Maya.  Its performance and stability may make it
# unsuitable for production use.
#
# Autodesk believes production quality would be better achieved with a C++
# version of this code.
#
# ===========================================================================
#+

'''Universal Front End USD transform operations

This module provides the logic to operate xform operations on USD objects
'''

import ufe

import maya.api.OpenMaya as OpenMaya

from pxr import Usd
from pxr import UsdGeom
from pxr import Gf
from math import degrees, radians

# ===========================================================================
#                           Utils
# ===========================================================================
class IncompatibleXformCommonOpOrder(Exception):
    pass

class IncompatibleXformMatrixOpOrder(Exception):
    pass

def tuples2dToList(tuples2D):
    listTuples = []
    for i, tuple in enumerate(tuples2D):
         listTuples.insert(i, list(tuple))
    return listTuples

def primToUfeXform(prim):
    xformCache = UsdGeom.XformCache()
    usdMatrix = xformCache.GetLocalToWorldTransform(prim)
    xform = ufe.Matrix4d()
    xform.matrix = tuples2dToList(usdMatrix)
    return xform

def primToUfeExclusiveXform(prim):
    xformCache = UsdGeom.XformCache()
    usdMatrix = xformCache.GetParentToWorldTransform(prim)
    xform = ufe.Matrix4d()
    xform.matrix = tuples2dToList(usdMatrix)
    return xform

# TODO: potentially eliminate this. Order matters in composing xform ops.
def convertToCompatibleCommonAPI(prim):
    """
        As we are using USD's XformCommonAPI which supports only the following xformOps :
            ["xformOp:translate", "xformOp:translate:pivot", "xformOp:rotateXYZ", "xformOp:scale", "!invert!xformOp:translate:pivot"]
        We are extending the supported xform Operations with :
            ["xformOp:rotateX", "xformOp:rotateY", "xformOp:rotateZ"]
        Where we convert these into xformOp:rotateXYZ.
    """
    xformable = UsdGeom.Xformable(prim)
    xformOps = xformable.GetOrderedXformOps()
    xformable.ClearXformOpOrder()
    primXform = UsdGeom.XformCommonAPI(prim)
    for op in xformOps:
        # RotateX
        if op.GetOpName() == "xformOp:rotateX":
            primXform.SetRotate(Gf.Vec3f(op.Get(),0,0))
        # RotateY
        elif op.GetOpName() == "xformOp:rotateY":
            primXform.SetRotate(Gf.Vec3f(0, op.Get(),0))
        # RotateZ
        elif op.GetOpName() == "xformOp:rotateZ":
            primXform.SetRotate(Gf.Vec3f(0, 0, op.Get()))
        # RotateXYZ
        elif op.GetOpName() == "xformOp:rotateXYZ":
            primXform.SetRotate(op.Get())
        # Scale
        elif op.GetOpName() == "xformOp:scale":
            primXform.SetScale(op.Get())
        # Translate
        elif op.GetOpName() == "xformOp:translate":
            primXform.SetTranslate(op.Get())
        # Scale / rotate pivot
        elif op.GetOpName() == "xformOp:translate:pivot":
            primXform.SetPivot(op.Get())
        # Scale / rotate pivot inverse: automatically added, nothing to do.
        elif op.GetOpName() == "!invert!xformOp:translate:pivot":
            pass
        # Not compatible
        else:
            # Restore old
            xformable.SetXformOpOrder(xformOps)
            raise IncompatibleXformCommonOpOrder("Incompatible xform op %s:" % op.GetOpName())
    return primXform

def convertToXformMatrixOp(prim):
    '''Checks to see if this prim is using an xformOpOrder with a single matrx form,
    and if so, returns the corresponding xformOp'''
    xformable = UsdGeom.Xformable(prim)
    xformOps = xformable.GetOrderedXformOps()
    if len(xformOps) != 1:
        raise IncompatibleXformMatrixOpOrder(
            "Xform can have at most one xformOp to be matrix-xform compatible: %s"
            % prim.GetName())
    op = xformOps[0]
    if op.GetOpType() != UsdGeom.XformOp.TypeTransform:
        raise IncompatibleXformMatrixOpOrder(
            "Xform's only op must be of type transform - %s was of type %s"
            % (op.GetOpName(), op.GetOpType().name))
    return op
        

# ===========================================================================
#                       Translate Operations
# ===========================================================================
def translateOp(prim, path, x, y, z,):
    ''' Absolute or relative translation of the given prim
    
    Returns (modifiedAttr, oldAttrValue, newAttrValue)'''
    try:
        primXform = UsdGeom.XformCommonAPI(prim)
        if not primXform:
            # This could mean that we have an incompatible xformOp in the stack
            try:
                primXform = convertToCompatibleCommonAPI(prim)
            except IncompatibleXformCommonOpOrder as e:
                try:
                    transformOp = convertToXformMatrixOp(prim)
                    oldXformMatrix = transformOp.Get()
                    newXformMatrix = type(oldXformMatrix)(oldXformMatrix)
                    newTranslation = oldXformMatrix.ExtractTranslation()
                    # no idea why, but it seems that when we have a single
                    # matrix xform, the translation fed in is relative... (?)
                    newTranslation[0] += x;
                    newTranslation[1] += y;
                    newTranslation[2] += z;
                    newXformMatrix.SetTranslateOnly(newTranslation)
                    transformOp.Set(newXformMatrix)
                    return transformOp.GetAttr(), oldXformMatrix, newXformMatrix
                except IncompatibleXformMatrixOpOrder as e:
                    raise Exception("xformOpOrder not a single matrix, or" 
                        " XformCommon compatible: %s" % (e,))
        oldTranslation = primXform.GetXformVectors(Usd.TimeCode.Default())[0]
        # if the prim is XformCommonAPI compatible, we seem to get absolute
        # values... (?)
        newTranslation = Gf.Vec3d(x, y, z)
        primXform.SetTranslate(newTranslation)
        return prim.GetAttribute('xformOp:translate'), oldTranslation, newTranslation
    except Exception as e:
        raise Exception("Failed to translate prim %s - %s" % (str(path), e))

class UsdTranslateUndoableCommand(ufe.TranslateUndoableCommand):
    '''
        Translation command of the given prim
        Ability to perform undo to restore the original translate value.
        As of 06/07/2018, redo is a no op as Maya re-does the operation for redo
    '''
    def __init__(self, prim, ufePath, item):
        super(UsdTranslateUndoableCommand, self).__init__(item)
        self._prim = prim
        self._path = ufePath
        
        self._xlateAttrib = None
        self._prevXlate = None
        self._newXlate = None

    def _perform(self):
        ''' No-op, Use translate to move the object '''
        pass

    def undo(self):
        self._xlateAttrib.Set(self._prevXlate)
        # (SD-06/07/2018) Haven't found a clean way to do it - would need to investigate

    def redo(self):
        self._xlateAttrib.Set(self._newXlate)

    def translate(self, x, y, z):
        attrib, oldVal, self._newXlate = translateOp(self._prim, self._path, x,y,z)
        # if dragging interactively, translate may be called multiple times for a
        # single undo item - so we use the first "old" value returned, and the last
        # "new" value
        if self._prevXlate is None:
            self._xlateAttrib = attrib
            self._prevXlate = oldVal
        return True

#===========================================================================
#                      Rotate Operations
#===========================================================================
def rotateOp(prim, path, x, y, z):
    ''' Absolute rotation (degrees) of the given prim '''
    primXform = UsdGeom.XformCommonAPI(prim)
    if not primXform.SetRotate(Gf.Vec3f(x,y,z)):
        # This could mean that we have an incompatible xformOp in the stack
        try:
            primXform = convertToCompatibleCommonAPI(prim)
            if not primXform.SetRotate(Gf.Vec3f(x,y,z)):
                raise Exception ("Unable to SetRotate on the 2nd try.")
        except Exception as e:
            raise Exception("Failed to rotate prim %s - %s" % (str(path), e))

class UsdRotateUndoableCommand(ufe.RotateUndoableCommand):
    '''
        Absolute rotation command of the given prim
        Ability to perform undo to restore the original rotation value.
        As of 06/07/2018, redo is a no op as Maya re-does the operation for redo
    '''
    def __init__(self, prim, ufePath, item):
        super(UsdRotateUndoableCommand, self).__init__(item)
        self._prim = prim
        self._noRotateOp = False
        self._path = ufePath
        self._failedInit = None

        # Since we want to change xformOp:rotateXYZ, and we need to store the prevRotate for
        # undo purpose, we need to make sure we convert it to common API xformOps (In case we have
        # rotateX, rotateY or rotateZ ops)
        try:
            convertToCompatibleCommonAPI(prim)
        except Exception as e:
            # Since Maya cannot catch this error at this moment, store it until we actually rotate
            self._failedInit = e
            return

        # Prim does not have a rotateXYZ attribute
        if not self._prim.HasAttribute('xformOp:rotateXYZ'):
            rotateOp(self._prim, self._path, 0,0,0)
            self._noRotateOp = True

        self._rotAttrib = self._prim.GetAttribute('xformOp:rotateXYZ')
        self._prevRotate = self._prim.GetAttribute('xformOp:rotateXYZ').Get()

    def _perform(self):
        ''' No-op, Use rotate to move the object '''
        pass

    def undo(self):
        # Check if initialization went ok.
        if not self._failedInit:
            self._rotAttrib.Set(self._prevRotate)
        # Todo : We would want to remove the xformOp
        # (SD-06/07/2018) Haven't found a clean way to do it - would need to investigate

    def redo(self):
        self._perform()

    def rotate(self, x, y, z):
        # Fail early - Initialization did not go as expected
        if self._failedInit:
            raise self._failedInit
        rotateOp(self._prim, self._path,x,y,z)
        return True

# ===========================================================================
#                       Scale Operations
# ===========================================================================
def scaleOp(prim, path, x, y, z,):
    ''' Absolute scale of the given prim '''
    primXform = UsdGeom.XformCommonAPI(prim)
    if not primXform.SetScale(Gf.Vec3f(x,y,z)):
         # This could mean that we have an incompatible xformOp in the stack
        try:
            primXform = convertToCompatibleCommonAPI(prim)
            if not primXform.SetScale(Gf.Vec3f(x,y,z)):
                raise Exception ("Unable to SetScale on the 2nd try.")
        except Exception as e:
            raise Exception("Failed to scale prim %s - %s" % (str(path), e))

class UsdScaleUndoableCommand(ufe.ScaleUndoableCommand):
    '''
        Absolute scale command of the given prim
        Ability to perform undo to restore the original scale value.
        As of 06/07/2018, redo is a no op as Maya re-does the operation for redo
    '''
    def __init__(self, prim, ufePath, item):
        super(UsdScaleUndoableCommand, self).__init__(item)
        self._prim = prim
        self._noScaleOp = False
        self._path = ufePath

        # Prim does not have a scale attribute
        if not self._prim.HasAttribute('xformOp:scale'):
            self._noScaleOp = True
            scaleOp(self._prim, self._path, 1,1,1)# Add a neutral scale xformOp

        self._scaleAttrib = self._prim.GetAttribute('xformOp:scale')
        self._prevScale = self._prim.GetAttribute('xformOp:scale').Get()

    def _perform(self):
        ''' No-op, Use scale to scale the object '''
        pass

    def undo(self):
        self._scaleAttrib.Set(self._prevScale)
        # Todo : We would want to remove the xformOp
        # (SD-06/07/2018) Haven't found a clean way to do it - would need to investigate

    def redo(self):
        self._perform()

    def scale(self, x, y, z):
        scaleOp(self._prim, self._path, x,y,z)
        return True

# ===========================================================================
#                       Pivot Operations
# ===========================================================================
def rotatePivotTranslateOp(prim, path, x, y, z,):
    ''' Absolute translation of the given prim's pivot point.'''
    primXform = UsdGeom.XformCommonAPI(prim)
    if not primXform.SetPivot(Gf.Vec3f(x,y,z)):
         # This could mean that we have an incompatible xformOp in the stack
        try:
            primXform = convertToCompatibleCommonAPI(prim)
            if not primXform.SetPivot(Gf.Vec3f(x,y,z)):
                raise Exception ("Unable to SetPivot on the 2nd try.")
        except Exception as e:
            raise Exception("Failed to set pivot for prim %s - %s" % (str(path), e))

class UsdRotatePivotTranslateUndoableCommand(ufe.TranslateUndoableCommand):
    '''
        Absolute translation command of the given prim's rotate pivot.
        Ability to perform undo to restore the original pivot value.
    '''
    attrName = 'xformOp:translate:pivot'

    def __init__(self, prim, ufePath, item):
        super(UsdRotatePivotTranslateUndoableCommand, self).__init__(item)
        self._prim = prim
        self._noPivotOp = False
        self._path = ufePath

        # Prim does not have a pivot translate attribute
        if not self._prim.HasAttribute(self.attrName):
            self._noPivotOp = True
            # Add an empty pivot translate.
            rotatePivotTranslateOp(self._prim, self._path, 0,0,0)

        self._xlateAttrib = self._prim.GetAttribute(self.attrName)
        self._prevXlate = self._xlateAttrib.Get()

    def undo(self):
        self._xlateAttrib.Set(self._prevXlate)
        # Todo : We would want to remove the xformOp
        # (SD-06/07/2018) Haven't found a clean way to do it - would need to investigate

    def redo(self):
        # As of 07-Jun-2018, redo is a no op as Maya re-does the operation for
        # redo.
        pass

    def translate(self, x, y, z):
        rotatePivotTranslateOp(self._prim, self._path, x,y,z)
        return True
