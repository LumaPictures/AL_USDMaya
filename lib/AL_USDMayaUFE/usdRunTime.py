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

'''Universal Front End USD run-time integration.

This module provides integration of the Maya run-time into the Universal
Front End.
'''

import ufe
import usdXformOpCmds
import maya.cmds as cmds
# For nameToDagPath().
import maya.app.renderSetup.common.utils as utils

from AL import usdmaya as AL_USDMaya

from pxr import Sdf

import re
import sys
from collections import deque
import logging

logger = logging.getLogger(__name__)

# Maya's UFE run-time name and ID.
kMayaRunTimeName = 'Maya-DG'
mayaRtid = 0

# Register this run-time with UFE under the following name.
kUSDRunTimeName = 'USD'

# Our run-time ID, allocated by UFE at registration time.  Initialize it
# with illegal 0 value.
USDRtid = 0

kIllegalUSDPath = 'Illegal USD run-time path %s.'

kNotGatewayNodePath = 'Tail of path %s is not a gateway node.'

# Type of the Maya shape node at the root of a USD hierarchy.
kMayaUsdGatewayNodeType = 'AL_usdmaya_ProxyShape'

# The normal Maya hierarchy handler, which we decorate for ProxyShape support.
# Keep a reference to it to restore on finalization.
mayaHierarchyHandler = None

# FIXME Python binding lifescope.
#
# In code where we return a Python object to C++ code, if we don't keep a
# Python object reference in Python, the Python object is destroyed, and only
# the C++ base class survives.  This "slices" the additional Python derived
# class functionality out of the object, and only the base class functionality
# remains.  See
#
# https://github.com/pybind/pybind11/issues/1145
#
# for a pybind11 pull request that describes the problem and fixes it.  If this
# pull request is merged into pybind11, we should update to that version and
# confirm the fix.
#
# In our case, this situation happens when returning a newly-created Python
# UsdSceneItem directly to C++ code.  Any such code should use the
# keepAlive mechanism.  Note that it is too early to be done in the
# UsdSceneItem __init__, as the keepAlive cleanup code checks that the item
# is in the global selection, which is obviously not the case when still in
# __init__.  PPT, 23-Nov-2017.
#
# Selection items to keep alive.
_keepAlive = {}

def keepAlive(item):
    '''Keep argument item alive by adding it to the keep alive cache.

    Before adding item to the cache, it is cleared of items no longer in the
    selection.
    '''
    global _keepAlive

    # First, clean out cache of items no longer needed.
    # As of 8-Jan-2018, this causes lifescope issues, so "leak" items in the
    # keepAlive map.  Since this Python module is temporary, pending a
    # permanent C++ implementation, not investigating.  PPT.
#     paths = _keepAlive.keys()
#     globalSelection = ufe.GlobalSelection.get()
#     for path in paths:
#         if path not in globalSelection:
#             del _keepAlive[path]

    # Next, add in new item.
    _keepAlive[item.path()] = item


def _getProxyShapeObj(path):
    # Ufe Paths may have |world in the beginning but getByName expects a
    # name or path without |world. (likely because it is using
    # MSelectionList.add)
    if path.startswith('|world'):
        path = path[len('|world'):]
    proxyShape = AL_USDMaya.ProxyShape.getByName(path)
    return proxyShape

def _getProxyShapePathKey(proxyShapeName):
    # need something hashable and unique by proxy shape so we can use a string
    # version of the dag path
    return str(utils.nameToDagPath(proxyShapeName).fullPathName())


# TODO: Everything is dynamic so we can rely on the stage returned being up to
# date. These entry points could be updated for caching, but we would need to
# add more rename, and usd callbacks to maintain accurate caches.
class _StageCache(object):
    '''Object to manage lookups from path to stage and back
    '''
    def __init__(self):
        # Map of AL_usdmaya_ProxyShape dag path to corresponding stage.
        # Note: Would need to listen for renaming of any parents of a proxy
        # shape in order to keep the dag path up to date.
        self._pathToStage = {}  # type: Dict[str, Usd.Stage]

        # Map of stage to corresponding AL_usdmaya_ProxyShape dag path. We will
        # also assume that a USD stage will not be instanced (even though
        # nothing in the data model prevents it).
        self._stageToPath = {}  # type: Dict[Usd.Stage, str]

    def _store(self, dagPath, stage):
        self._stageToPath[stage] = dagPath
        self._pathToStage[dagPath] = stage

    def _fetchStageForPath(self, dagPath):
        '''Get the stage that the proxy shape at this path is loading'''
        proxyShape = _getProxyShapeObj(dagPath)
        stage = proxyShape.getUsdStage()
        # if stage:
        #     self._store(dagPath, stage)
        return stage

    def _fetchPathForStage(self, stage):
        '''Get the dag path for a proxyShape loading this stage'''
        for proxyShapeName in cmds.ls(type=kMayaUsdGatewayNodeType):
            proxyShape = _getProxyShapeObj(proxyShapeName)
            potentialStage = proxyShape.getUsdStage()
            if stage != potentialStage:
                continue

            dagPath = _getProxyShapePathKey(proxyShapeName)
            # self._store(dagPath, stage)
            return dagPath

    def pathForStage(self, stage):
        '''Return the AL_usdmaya_ProxyShape node UFE path for the argument
        stage.

        Parameters
        ----------
        stage : Usd.Stage
        '''
        path = self._fetchPathForStage(stage)
        if not path:
            import traceback
            logger.error('Failed to get a path for stage: %s' % stage)
            traceback.print_stack()
        return path

    def stageForPath(self, path):
        '''Get USD stage corresponding to argument Maya Dag path.

        A stage is bound to a single Dag proxy shape.

        Parameters
        ----------
        path : str
        '''
        stage = self._fetchStageForPath(path)
        if not stage:
            import traceback
            logger.error('Failed to get a stage for: %s' % path)
            traceback.print_stack()
        return stage

    def dropStage(self, stage):
        '''Forget mapping to and from this stage'''
        pass

    def clear(self):
        '''clear all cached stage mappings'''
        pass


# singleton for caching stage to proxyShape dagPath mapping.
stageCache = _StageCache()

def getStage(path):
    '''Get USD stage corresponding to argument Maya Dag path.

    A stage is bound to a single Dag proxy shape.
    '''
    # we only care about the dag component of this path.
    if isinstance(path, ufe.Path):
        if path.segments > 1:
            path = path.segments[0]

    return stageCache.stageForPath(str(path))

def ufePathToPrim(path):
    '''Return the USD prim corresponding to the argument UFE path.'''
    # Assume that there are only two segments in the path, the first a Maya
    # Dag path segment to the proxy shape, which identifies the stage, and
    # the second the USD segment.
    segments = path.segments
    assert len(segments) == 2, kIllegalUSDPath % str(path)
    dagSegment = segments[0]
    stage = getStage(dagSegment)
    return stage.GetPrimAtPath(str(segments[1]))

def isRootChild(path):
    segments = path.segments
    assert len(segments) == 2, kIllegalUSDPath % str(path)
    return len(segments[1]) == 1

def getDefPrimSpec(prim):
    '''Return the highest-priority spec where the prim has a def primSpec.'''
    # The source layer is the one in which there exists a def primSpec, not
    # an over.
    for primSpec in prim.GetPrimStack():
        if primSpec.specifier == Sdf.SpecifierDef:
            return primSpec

def copyPrimSpec(srcSpec, editTarget, dstScenePath):
    '''Copy a prim spec to the destination prim spec at the edit target.'''
    # We use the edit target layer be the destination, an alternative would
    # be to use the source layer.
    dstLayer = editTarget.GetLayer()
    dstPrimPath = editTarget.MapToSpecPath(dstScenePath)
    # if parent doesnt exist in destination layer create it or copy will error.
    Sdf.CreatePrimInLayer(dstLayer, dstPrimPath.GetParentPath())
    return Sdf.CopySpec(srcSpec.layer,
                        srcSpec.path,
                        dstLayer,
                        dstPrimPath)

def createSiblingSceneItem(ufeSrcPath, siblingName):
    ufeSiblingPath = ufeSrcPath.sibling(ufe.PathComponent(siblingName))
    return UsdSceneItem.create(ufeSiblingPath, ufePathToPrim(ufeSiblingPath))

# Compiled regular expression to find a numerical suffix to a path component.
# It searches for any number of characters followed by a single non-numeric,
# then one or more digits at end of string.
_reProg = re.compile('(.*)([^0-9])([0-9]+)$')

def uniqueName(existingNames, srcName):
    # Split the source name into a base name and a numerical suffix (set to
    # 1 if absent).  Increment the numerical suffix until name is unique.
    match = _reProg.search(srcName)
    (base, suffix) = (srcName, 1) if match is None else \
                     (match.group(1)+match.group(2), int(match.group(3))+1)
    dstName = base + str(suffix)
    while dstName in existingNames:
        suffix += 1
        dstName = base + str(suffix)
    return dstName

_inPathChange = False

class InPathChange(object):
    def __enter__(self):
        global _inPathChange
        _inPathChange = True

    def __exit__(self, *args):
        global _inPathChange
        _inPathChange = False

def inPathChange():
    return _inPathChange

class UsdSceneItem(ufe.SceneItem):
    def __init__(self, path, prim):
        super(UsdSceneItem, self).__init__(path)
        self._prim = prim
        # FIXME Python binding lifescope.
        keepAlive(self)

    @staticmethod
    def create(path, prim):
        # Are we already keeping an item like this alive?  If so, use it.
        item = _keepAlive.get(path)
        if item is None:
            item = UsdSceneItem(path, prim)
        return item

    def prim(self):
        if not self._prim:
            # our reference to prim may have expired through a stage change,
            # so use the path to fetch a new one.
            self._prim = ufePathToPrim(self.path())
        return self._prim

    def nodeType(self):
        return self.prim().GetTypeName()

class UsdHierarchy(ufe.Hierarchy):
    '''USD run-time hierarchy interface.

    This class implements the hierarchy interface for normal USD prims, using
    standard USD calls to obtain a prim's parent and children.
    '''
    def __init__(self):
        super(UsdHierarchy, self).__init__()

    def setItem(self, item):
        self._item = item
        self._path = item.path()

    def sceneItem(self):
        return self._item

    def path(self):
        return self._path

    def prim(self):
        return self._item.prim()

    def hasChildren(self):
        return self.prim().GetChildren() != []

    def parent(self):
        return UsdSceneItem.create(self._path.pop(), self.prim().GetParent())

    def children(self):
        # Return USD children only, i.e. children within this run-time.
        usdChildren = self.prim().GetChildren()

        # The following is the desired code, lifescope issues notwithstanding.
        # children = [UsdSceneItem(self._path + child.GetName(), child)
        #             for child in usdChildren]
        children = [UsdSceneItem.create(self._path + child.GetName(), child)
                    for child in usdChildren]
        return children

    def appendChild(self, child):
        # First, check if we need to rename the child.
        childrenName = set(child.GetName() for child in self.prim().GetChildren())
        childName = str(child.path().back())
        if childName in childrenName:
            childName = uniqueName(childrenName, childName)

        # Set up all paths to perform the reparent.
        prim = child.prim()
        stage = prim.GetStage()
        ufeSrcPath = child.path()
        usdSrcPath = prim.GetPath()
        ufeDstPath = self._path + childName
        usdDstPath = self.prim().GetPath().AppendChild(childName)
        srcSpec = getDefPrimSpec(prim)
        if srcSpec is None:
            raise RuntimeError("No prim found at %s" % usdSrcPath)

        # In USD, reparent is implemented like rename, using copy to
        # destination, then remove from source.  See
        # UsdUndoRenameCommand._rename comments for details.
        with InPathChange():
            status = copyPrimSpec(srcSpec, self._stage.GetEditTarget(),
                                  usdDstPath)
            if not status:
                raise RuntimeError("Appending child %s to parent %s failed." %
                                   (str(ufeSrcPath), str(self._path)))

            stage.RemovePrim(usdSrcPath)
            ufeDstItem = UsdSceneItem.create(
                ufeDstPath, ufePathToPrim(ufeDstPath))
            notification = ufe.ObjectReparent(ufeDstItem, ufeSrcPath)
            ufe.Scene.notifyObjectPathChange(notification)

        # FIXME  No idea how to get the child prim index yet.  PPT, 16-Aug-2018.
        return ufe.AppendedChild(ufeDstItem, ufeSrcPath, 0)

class UsdRootChildHierarchy(UsdHierarchy):
    '''USD run-time hierarchy interface for children of the USD root prim.

    This class modifies its base class implementation to return the Maya USD
    gateway node as parent of USD prims that are children of the USD root prim.
    '''
    def __init__(self):
        super(UsdRootChildHierarchy, self).__init__()

    def parent(self):
        # If we're a child of the root, our parent node in the path is a Maya
        # node.  Ask the Maya hierarchy interface to create a selection item
        # for that path.
        parentPath = self._path.pop()
        assert parentPath.runTimeId() == mayaRtid, kNotGatewayNodePath % str(path)

        mayaHierarchyHandler = ufe.RunTimeMgr.instance().hierarchyHandler(mayaRtid)
        return mayaHierarchyHandler.createItem(parentPath)

class UsdHierarchyHandler(ufe.HierarchyHandler):
    '''USD run-time hierarchy handler.

    This hierarchy handler is the standard USD run-time hierarchy handler.  Its
    only special behavior is to return a UsdRootChildHierarchy interface object
    if it is asked for a hierarchy interface for a child of the USD root prim.
    These prims are special because we define their parent to be the Maya USD
    gateway node, which the UsdRootChildHierarchy interface implements.
    '''
    def __init__(self):
        super(UsdHierarchyHandler, self).__init__()
        self._usdRootChildHierarchy = UsdRootChildHierarchy()
        self._usdHierarchy = UsdHierarchy()

    def hierarchy(self, item):
        if isRootChild(item.path()):
            self._usdRootChildHierarchy.setItem(item)
            return self._usdRootChildHierarchy
        else:
            self._usdHierarchy.setItem(item)
            return self._usdHierarchy

    def createItem(self, path):
        return UsdSceneItem.create(path, ufePathToPrim(path))

class ProxyShapeHierarchy(ufe.Hierarchy):
    '''USD gateway node hierarchy interface.

    This class defines a hierarchy interface for a single kind of Maya node,
    the USD gateway node.  This node is special in that its parent is a Maya
    node, but its children are children of the USD root prim.
    '''
    def __init__(self, mayaHierarchyHandler):
        super(ProxyShapeHierarchy, self).__init__()
        self._mayaHierarchyHandler = mayaHierarchyHandler

    def setItem(self, item):
        self._item = item
        self._mayaHierarchy = self._mayaHierarchyHandler.hierarchy(item)
        self._usdRootPrim = None

    def _getUsdRootPrim(self):
        if self._usdRootPrim is None:
            stage = getStage(self._item.path())
            if stage:
                self._usdRootPrim = stage.GetPrimAtPath('/')
        return self._usdRootPrim

    def hasChildren(self):
        rootPrim = self._getUsdRootPrim()
        if not rootPrim:
            return False
        return len(rootPrim.GetChildren()) > 0

    def parent(self):
        return self._mayaHierarchy.parent()

    def children(self):
        # Return children of the USD root.
        rootPrim = self._getUsdRootPrim()
        if not rootPrim:
            return []
        usdChildren = rootPrim.GetChildren()
        parentPath = self._item.path()

        # We must create selection items for our children.  These will have as
        # path the path of the proxy shape, with a single path segment of a
        # single component appended to it.
        # The following is the desired code, lifescope issues notwithstanding.
        # children = [UsdSceneItem(parentPath + ufe.PathSegment(
        #     ufe.PathComponent(child.GetName()), USDRtid, '/'), child) for child
        #             in usdChildren]
        children = [UsdSceneItem.create(parentPath + ufe.PathSegment(
            ufe.PathComponent(child.GetName()), USDRtid, '/'), child) for child
                    in usdChildren]
        return children

class ProxyShapeHierarchyHandler(ufe.HierarchyHandler):
    '''Maya run-time hierarchy handler with support for USD gateway node.

    This hierarchy handler is NOT a USD run-time hierarchy handler: it is a
    Maya run-time hierarchy handler.  It decorates the standard Maya run-time
    hierarchy handler and replaces it, providing special behavior only if the
    requested hierarchy interface is for the Maya to USD gateway node.  In that
    case, it returns a special ProxyShapeHierarchy interface object, which
    knows how to handle USD children of the Maya ProxyShapeHierarchy node.

    For all other Maya nodes, this hierarchy handler simply delegates the work
    to the standard Maya hierarchy handler it decorates, which returns a
    standard Maya hierarchy interface object.
    '''
    def __init__(self, mayaHierarchyHandler):
        super(ProxyShapeHierarchyHandler, self).__init__()
        self._mayaHierarchyHandler = mayaHierarchyHandler
        self._proxyShapeHierarchy = ProxyShapeHierarchy(mayaHierarchyHandler)

    def hierarchy(self, item):
        if item.nodeType() == kMayaUsdGatewayNodeType:
            self._proxyShapeHierarchy.setItem(item)
            return self._proxyShapeHierarchy
        else:
            return self._mayaHierarchyHandler.hierarchy(item)

    def createItem(self, path):
        return self._mayaHierarchyHandler.createItem(path)

class UsdTransform3d(ufe.Transform3d):
    def __init__(self):
        super(UsdTransform3d, self).__init__()

    def setItem(self, item):
        self._path = item.path()
        self._item = item

    def sceneItem(self):
        return self._item

    def path(self):
        return self._path

    def prim(self):
        return self._item.prim()

    def translate(self, x, y, z):
        usdXformOpCmds.translateOp(self.prim(), self._path, x, y, z)

    def translation(self):
        x, y, z = (0, 0, 0)
        if self.prim().HasAttribute('xformOp:translate'):
            # Initially, attribute can be created, but have no value.
            v = self.prim().GetAttribute('xformOp:translate').Get()
            if v is not None:
                x, y, z = v
        return ufe.Vector3d(x, y, z)

    # FIXME Python binding lifescope.  Memento objects are returned directly to
    # C++, which does not keep them alive.  Use LIFO deque with maximum size to
    # keep them alive without consuming an unbounded amount of memory.  This
    # hack will fail for more than _MAX_MEMENTOS objects.  PPT, 12-Dec-2017.
    _MAX_MEMENTOS = 10000
    mementos = deque(maxlen=_MAX_MEMENTOS)


    def translateCmd(self):
        # FIXME Python binding lifescope.  Must keep command object alive.
        translateCmd = usdXformOpCmds.UsdTranslateUndoableCommand(self.prim(), self._path, self._item)
        UsdTransform3d.mementos.append(translateCmd)
        return translateCmd

    def rotate(self, x, y, z):
        usdXformOpCmds.rotateOp(self.prim(), self._path, x, y, z)

    def rotateCmd(self):
        # FIXME Python binding lifescope.  Must keep command object alive.
        rotateCmd = usdXformOpCmds.UsdRotateUndoableCommand(self.prim(), self._path, self._item)
        UsdTransform3d.mementos.append(rotateCmd)
        return rotateCmd

    def scale(self, x, y, z):
        usdXformOpCmds.scaleOp(self.prim(), self._path, x, y, z)

    def scaleCmd(self):
        # FIXME Python binding lifescope.  Must keep command object alive.
        scaleCmd = usdXformOpCmds.UsdScaleUndoableCommand(self.prim(), self._path, self._item)
        UsdTransform3d.mementos.append(scaleCmd)
        return scaleCmd

    def rotatePivotTranslate(self, x, y, z):
        usdXformOpCmds.rotatePivotTranslateOp(self.prim(), self._path, x, y, z)

    def rotatePivotTranslateCmd(self):
        # FIXME Python binding lifescope.  Must keep command object alive.
        translateCmd = usdXformOpCmds.UsdRotatePivotTranslateUndoableCommand(self.prim(), self._path, self._item)
        UsdTransform3d.mementos.append(translateCmd)
        return translateCmd

    def rotatePivot(self):
        x, y, z = (0, 0, 0)
        if self.prim().HasAttribute('xformOp:translate:pivot'):
            # Initially, attribute can be created, but have no value.
            v = self.prim().GetAttribute('xformOp:translate:pivot').Get()
            if v is not None:
                x, y, z = v
        return ufe.Vector3d(x, y, z)

    def scalePivot(self):
        return self.rotatePivot()

    def scalePivotTranslate(self, x, y, z):
        return self.rotatePivotTranslate(x, y, z)


    def segmentInclusiveMatrix(self):
        return usdXformOpCmds.primToUfeXform(self.prim())

    def segmentExclusiveMatrix(self):
        return usdXformOpCmds.primToUfeExclusiveXform(self.prim())

class UsdTransform3dHandler(ufe.Transform3dHandler):
    def __init__(self):
        super(UsdTransform3dHandler, self).__init__()
        self._usdTransform3d = UsdTransform3d()

    def transform3d(self, item):
        self._usdTransform3d.setItem(item)
        return self._usdTransform3d

class UsdUndoDeleteCommand(ufe.UndoableCommand):
    def __init__(self, prim):
        super(UsdUndoDeleteCommand, self).__init__()
        self._prim = prim
        self._state = prim.IsActive()

    def _perform(self, state):
        self._prim.SetActive(state)

    def undo(self):
        self._perform(self._state)

    def redo(self):
        self._perform(not self._state)

class UsdUndoDuplicateCommand(ufe.UndoableCommand):

    def __init__(self, srcPrim, ufeSrcPath):
        super(UsdUndoDuplicateCommand, self).__init__()
        self._srcPrim = srcPrim
        self._stage = srcPrim.GetStage()
        self._ufeSrcPath = ufeSrcPath
        self._usdDstPath = self.getDstPath(srcPrim)
        self._editTarget = self._stage.GetEditTarget()

    def usdDstPath(self):
        return self._usdDstPath

    @staticmethod
    def getDstPath(srcPrim):
        '''Return the USD destination path and layer.'''
        parent = srcPrim.GetParent()
        childrenName = set(child.GetName() for child in parent.GetChildren())
        # Find a unique name for the destination.  If the source name already
        # has a numerical suffix, increment it, otherwise append "1" to it.
        dstName = uniqueName(childrenName, srcPrim.GetName())
        usdDstPath = parent.GetPath().AppendChild(dstName)
        return usdDstPath

    def undo(self):
        # USD sends a ResyncedPaths notification after the prim is removed, but
        # at that point the prim is no longer valid, and thus a UFE post delete
        # notification is no longer possible.  To respect UFE object delete
        # notification semantics, which require the object to be alive when
        # the notification is sent, we send a pre delete notification here.
        notification = ufe.ObjectPreDelete(createSiblingSceneItem(
            self._ufeSrcPath, self._usdDstPath.elementString))
        ufe.Scene.notifyObjectDelete(notification)

        self._stage.RemovePrim(self._usdDstPath)

    def redo(self):
        # MAYA-92264: Pixar bug prevents redo from working.  Try again with USD
        # version 0.8.5 or later.  PPT, 28-May-2018.
        try:
            # The spec that will be copied is the highest priority primSpec that is not
            # an over.  An alternative would have beeen to call Sdf.CopySpec for
            # each layer in which there is an over or a def, until we reach the
            # layer with a def primSpec.  This would preserve the visual appearance
            # of the duplicate.  PPT, 12-Jun-2018.
            copyPrimSpec(getDefPrimSpec(self._srcPrim), self._editTarget, self._usdDstPath)
        except Exception as e:
            print e
            raise

class UsdUndoRenameCommand(ufe.UndoableCommand):

    def __init__(self, srcItem, newName):
        super(UsdUndoRenameCommand, self).__init__()
        prim = srcItem.prim()
        self._stage = prim.GetStage()
        editTarget = self._stage.GetEditTarget()
        self._ufeSrcPath = srcItem.path()
        self._usdSrcPath = prim.GetPath()
        self._usdDstPath = prim.GetParent().GetPath().AppendChild(str(newName))
        # Every call to rename() (through execute(), undo() or redo()) removes
        # a prim, which becomes expired.  Since USD UFE scene items contain a
        # prim, we must recreate them after every call to rename.
        self._ufeDstItem = None
        self._srcPrimSpec = getDefPrimSpec(prim)
        if not self._srcPrimSpec:
            raise RuntimeError("No prim found at %s" % prim.GetPath())
        self._srcLayer = self._srcPrimSpec.layer
        self._srcPath = self._srcPrimSpec.path
        self._dstlayer = editTarget.GetLayer()
        self._dstPath = editTarget.MapToSpecPath(self._usdDstPath)
        if self._srcLayer != editTarget.GetLayer():
            # TODO:
            # A bunch of possible behaviors here:
            # - perform rename on srcLayer (and best would be all layers with a prim spec
            #   at the path, including overs)
            #      * The main drawback here is that many layers could be edited and users
            #      would need to be mindful of saving them all out if they want changes to persist.
            #      * Also modifying a stage at a non "edit target" could be confusing, and
            #      wasteful if we end up causing heavy layers to be saved in the maya scene
            #      because we have dirtied them (They would also stop getting live updates).
            #      * However in a lower level asset authoring context this method would be
            #      a very powerful way to make sweeping changes to a complex usd stage.
            # - error if trying to edit a prim that was defined on a different layer than the
            #   edit target.
            #      This would have well defined behavior, but is potentially limiting if we are ever
            #      modifying a stage with multiple layers, and you would have to rename the
            #      spec on each layer.
            # - current: duplicate a renamed prim to the edit target but only remove src if it is
            #   on the edit target as well.
            #      This works great when editing single layers and the source prim is on the
            #      edit target, but otherwise is confusing to the user, so we print a warning...
            logger.warning('Prim was not defined on the edit target so it cannot be '
                           'renamed and will instead be duplicated.')

    def renamedItem(self):
        return self._ufeDstItem

    def _rename(self, srcLayer, srcPath, dstLayer, dstPath, ufeSrcPath, usdSrcPath, usdDstPath):
        '''Rename the prim hierarchy at usdSrcPath to usdDstPath.'''
        createdItem = None
        with InPathChange():
            # make sure parent exists in case we are copying to a different layer
            Sdf.CreatePrimInLayer(dstLayer, dstPath.GetParentPath())
            status = Sdf.CopySpec(srcLayer, srcPath, dstLayer, dstPath)
            if status:
                self._stage.RemovePrim(usdSrcPath)
                # The renamed scene item is a "sibling" of its original name.
                createdItem = createSiblingSceneItem(
                    ufeSrcPath, usdDstPath.elementString)
                # USD sends two ResyncedPaths() notifications, one for the CopySpec
                # call, the other for the RemovePrim call (new name added, old name
                # removed).  Unfortunately, the rename semantics are lost: there is
                # no notion that the two notifications belong to the same atomic
                # change.  So instead we indicate that we are in the midst of a path
                # change and Provide a single Rename notification ourselves here.
                notification = ufe.ObjectRename(createdItem, ufeSrcPath)
                ufe.Scene.notifyObjectPathChange(notification)
                # FIXME: see note in __init__
                # if prim still exists because it is defined on a layer besides the
                # edit target, we add it back here.
                if self._stage.GetPrimAtPath(usdSrcPath):
                    notification = ufe.ObjectAdd(ufe.Hierarchy.createItem(ufeSrcPath))
                    ufe.Scene.notifyObjectAdd(notification)

            return createdItem

    def undo(self):
        self._rename(self._dstlayer, self._dstPath, self._srcLayer, self._srcPath,
                     self._ufeDstItem.path(), self._usdDstPath,
                     self._usdSrcPath)

    def redo(self):
        self._ufeDstItem = \
            self._rename(self._srcLayer,  self._srcPath, self._dstlayer, self._dstPath,
                         self._ufeSrcPath, self._usdSrcPath, self._usdDstPath)

class UsdSceneItemOps(ufe.SceneItemOps):
    def __init__(self):
        super(UsdSceneItemOps, self).__init__()

    def setItem(self, item):
        self._path = item.path()
        self._item = item

    def sceneItem(self):
        return self._item

    def path(self):
        return self._path

    def prim(self):
        return self._item.prim()

    # FIXME Python binding lifescope.  Command objects are returned directly to
    # C++, which does not keep them alive.  Use LIFO deque with maximum size to
    # keep them alive without consuming an unbounded amount of memory.  This
    # hack will fail for more than _MAX_UNDO_COMMANDS objects.
    # PPT, 1-May-2018.
    _MAX_UNDO_COMMANDS = 10000
    undoCommands = deque(maxlen=_MAX_UNDO_COMMANDS)

    def deleteItem(self):
        # toggling active state is more interesting than just deactivating it,
        # but we may want to have a shift delete to do the activate so that bulk
        # operations are more predictable.
        # Note: this is a moot point currently because the there's no way to
        # select a "deactivated" ufe object
        prim = self.prim()
        newState = not prim.GetActive()
        # not sure if this would help:
        # if not newState:
        #     notification = ufe.ObjectPreDelete(self._item)
        #     ufe.Scene.notifyObjectDelete(notification)
        return prim.SetActive(newState)

    def deleteItemCmd(self):
        # FIXME Python binding lifescope.  Must keep command object alive.
        deleteCmd = UsdUndoDeleteCommand(self.prim())
        deleteCmd.execute()
        UsdSceneItemOps.undoCommands.append(deleteCmd)
        return deleteCmd

    def duplicateItem(self):
        prim = self.prim()
        (usdDstPath, layer) = UsdUndoDuplicateCommand.primInfo(prim)
        status = UsdUndoDuplicateCommand.duplicate(
            layer, prim.GetPath(), usdDstPath)
        # The duplicate is a sibling of the source.
        return createSiblingSceneItem(
            self.path(), usdDstPath.elementString) if status else None

    def duplicateItemCmd(self):
        # FIXME Python binding lifescope.  Must keep command object alive.
        duplicateCmd = UsdUndoDuplicateCommand(self.prim(), self._path)
        duplicateCmd.execute()
        UsdSceneItemOps.undoCommands.append(duplicateCmd)
        return ufe.Duplicate(createSiblingSceneItem(
            self.path(), duplicateCmd.usdDstPath().elementString), duplicateCmd)

    def renameItem(self, newName):
        UsdUndoRenameCommand(self._item, newName).execute()

    def renameItemCmd(self, newName):
        # FIXME Python binding lifescope.  Must keep command object alive.
        renameCmd = UsdUndoRenameCommand(self._item, newName)
        renameCmd.execute()
        UsdSceneItemOps.undoCommands.append(renameCmd)
        return ufe.Rename(renameCmd.renamedItem(), renameCmd)

class UsdSceneItemOpsHandler(ufe.SceneItemOpsHandler):
    def __init__(self):
        super(UsdSceneItemOpsHandler, self).__init__()
        self._usdSceneItemOps = UsdSceneItemOps()

    def sceneItemOps(self, item):
        self._usdSceneItemOps.setItem(item)
        return self._usdSceneItemOps

def initialize():
    # Replace the Maya hierarchy handler with ours.
    global mayaRtid
    global USDRtid
    mayaRtid = ufe.RunTimeMgr.instance().getId(kMayaRunTimeName)
    mayaHierarchyHandler = ufe.RunTimeMgr.instance().hierarchyHandler(mayaRtid)
    ufe.RunTimeMgr.instance().setHierarchyHandler(
        mayaRtid, ProxyShapeHierarchyHandler(mayaHierarchyHandler))

    USDRtid = ufe.RunTimeMgr.instance().register(
        kUSDRunTimeName, UsdHierarchyHandler(),
        UsdTransform3dHandler(), UsdSceneItemOpsHandler())

def finalize():
    # Restore the normal Maya hierarchy handler, and unregister.
    global mayaHierarchyHandler
    ufe.RunTimeMgr.instance().setHierarchyHandler(mayaRtid, mayaHierarchyHandler)
    ufe.RunTimeMgr.instance().unregister(USDRtid)
    mayaHierarchyHandler = None
    stageCache.clear()
