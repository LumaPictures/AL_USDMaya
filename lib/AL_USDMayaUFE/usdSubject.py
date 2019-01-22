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

'''Universal Front End USD Observer pattern experiment.

This experimental module is to investigate providing UFE Observer support
for USD, that is, to translate USD notifications into a UFE equivalent.'''

from AL import usdmaya as AL_USDMaya

import maya.api.OpenMaya as OpenMaya
import maya.cmds as cmds

from pxr import Tf
from pxr import Usd

import ufe
import usdRunTime

import logging

logger = logging.getLogger(__name__)
# Subject singleton for observation of all USD stages.
_stagesSubject = None

def dagPathToUfe(dagPath):
    # This function can only create UFE Maya scene items with a single segment,
    # as it is only given a Dag path as input.
    segment = dagPathToPathSegment(dagPath)
    return ufe.Path(segment);

def dagPathToPathSegment(dagPath):
    # Unfortunately, there does not seem to be a Maya API equivalent to
    # TdagPathIterator.  There must be a better way.  PPT, 3-Oct-2018.
    nbComponents = dagPath.length()
    components = []
    for i in xrange(nbComponents):
        obj = dagPath.node()
        fn = OpenMaya.MFnDependencyNode(obj)
        components.insert(0, ufe.PathComponent(fn.name()))
        dagPath.pop()
    # Prepend world at the start of the segment.
    components.insert(0, ufe.PathComponent('world'))
    return ufe.PathSegment(components, 1, '|');


def stagePath(stage):
    '''Return the AL_usdmaya_ProxyShape node UFE path for the argument stage.'''
    pathString = usdRunTime.stageCache.pathForStage(stage)
    selectionList = OpenMaya.MSelectionList()
    selectionList.add(pathString)
    objDagPath = selectionList.getDagPath(0)
    ufePath = dagPathToUfe(objDagPath)
    return ufePath


def isTransform3D(usdChangedPath):
    '''Return whether a USD changed path notification denotes a UFE
    Transform3d change.'''
    # Very rough initial implementation.  For now, compare USD path element
    # string value.
    return usdChangedPath.elementString == '.xformOp:translate'

class _StagesSubject(object):
    '''Subject class to observe Maya scene.

    This class observes Maya file open, to register a USD observer on each
    stage the Maya scene contains.  This USD observer translates USD
    notifications into UFE notifications.
    '''
    filePathPlug = OpenMaya.MNodeClass(usdRunTime.kMayaUsdGatewayNodeType) \
        .attribute('filePath')

    def __init__(self):
        super(_StagesSubject, self).__init__()

        # Map of per-stage listeners, indexed by stage.
        self._stageListeners = {}

        # Workaround to MAYA-65920: at startup, MSceneMessage.kAfterNew file
        # callback is incorrectly called by Maya before the
        # MSceneMessage.kBeforeNew file callback, which should be illegal.
        # Detect this and ignore illegal calls to after new file callbacks.
        # PPT, 19-Jan-16.
        self._beforeNewCbCalled = False

        self._cbIds = []
        self._nodeCbIds = {}
        cbArgs = [(OpenMaya.MSceneMessage.kBeforeNew, self._beforeNewCb,
                   'before new'),
                  (OpenMaya.MSceneMessage.kBeforeOpen, self._beforeOpenCb,
                   'before open'),
                  (OpenMaya.MSceneMessage.kAfterOpen, self._afterOpenCb,
                   'after open'),
                  (OpenMaya.MSceneMessage.kAfterNew, self._afterNewCb,
                   'after new')]

        for (msg, cb, data) in cbArgs:
            self._cbIds.append(
                OpenMaya.MSceneMessage.addCallback(msg, cb, data))

        self._cbIds.append(
            OpenMaya.MDGMessage.addNodeAddedCallback(
                self._nodeAddedCb,
                usdRunTime.kMayaUsdGatewayNodeType))
        self._cbIds.append(
            OpenMaya.MDGMessage.addNodeRemovedCallback(
                self._nodeRemovedCb,
                usdRunTime.kMayaUsdGatewayNodeType))

    def finalize(self):
        toRemove = self._cbIds
        for callbackIds in self._nodeCbIds.values():
            toRemove.extend(callbackIds)
        OpenMaya.MMessage.removeCallbacks(toRemove)
        self._cbIds = []
        self._nodeCbIds = {}

    def _beforeNewCb(self, data):
        self._beforeNewCbCalled = True

        for listener in self._stageListeners.itervalues():
            listener.Revoke()
        self._stageListeners = {}

    def _beforeOpenCb(self, data):
        self._beforeNewCb(data)
        usdRunTime.stageCache.clear()

    def _afterNewCb(self, data):
        # Workaround to MAYA-65920: detect and avoid illegal callback sequence.
        if not self._beforeNewCbCalled:
            return

        self._beforeNewCbCalled = False

        self._afterOpenCb(data)

    def _afterOpenCb(self, data):
        pass

    def _nodeAddedCb(self, node, clientData):
        # stage is not initialized until filePath attribute is set
        # FIXME: There may be other times that a new stage is loaded, so it
        # would be better to hook this up to a callback within the AL plugin.
        id1 = OpenMaya.MNodeMessage.addAttributeChangedCallback(
                node,
                self._onStageLoad,
                node)
        self._nodeCbIds[OpenMaya.MObjectHandle(node).hashCode()] = [id1]

    def _nodeRemovedCb(self, node, clientData):
        name = OpenMaya.MFnDagNode(node).partialPathName()
        self.removeProxyShape(name)
        hash_ = OpenMaya.MObjectHandle(node).hashCode()
        callbackIds = self._nodeCbIds.get(hash_, None)
        if callbackIds:
            OpenMaya.MMessage.removeCallbacks(callbackIds)

    def _onStageLoad(self, msg, plug, otherPlug, node):
        if plug != self.filePathPlug:
            return

        proxyShapeName = OpenMaya.MFnDagNode(node).partialPathName()
        proxyShape = AL_USDMaya.ProxyShape.getByName(proxyShapeName)

        stage = proxyShape.getUsdStage()
        if not stage:
            logger.warning('No stage found %s' % stage)

        # Observe stage changes, must keep a reference to listener.
        # FIXME: Need to fix so that we do not hold on to stage references if
        # they are no longer valid or needed. (i.e. new stage loaded in proxy
        # shape or something else)
        if stage not in self._stageListeners:
            logger.info('adding listener to stage!')
            self._stageListeners[stage] = Tf.Notice.Register(
                    Usd.Notice.ObjectsChanged, self.stageChanged, stage)

    def removeProxyShape(self, proxyShapeName):
        proxyShape = AL_USDMaya.ProxyShape.getByName(proxyShapeName)
        stage = proxyShape.getUsdStage()

        listener = self._stageListeners.get(stage)
        if listener:
            listener.Revoke()
            self._stageListeners.pop(stage)

        usdRunTime.stageCache.dropStage(stage)

    def stageChanged(self, notice, stage):
        '''Call the stageChanged() methods on stage observers.

        Notice will be of type Usd.Notice.ObjectsChanged.
        '''
        logger.debug('stageChanged() called! %s' % stage)
        # If the stage path has not been initialized yet, do nothing
        rootUfePath = stagePath(stage)
        if not rootUfePath:
            logger.warning('Could not find a proxy shape for stage: %s'
                           % stage)
            return

        logger.debug('processing stage changed...\n'
                     'rootUfePath: %s\n '
                     'ResyncedPaths: %s\n'
                     'ChangedInfoPaths: %s\n'%
                     (rootUfePath,
                      notice.GetResyncedPaths(),
                      notice.GetChangedInfoOnlyPaths()))
        for changedPath in notice.GetResyncedPaths():
            usdPrimPathStr = str(changedPath.GetPrimPath())
            ufePath = rootUfePath + ufe.PathSegment(
                usdPrimPathStr, 2, '/')
            prim = stage.GetPrimAtPath(changedPath)
            # Changed paths can be properties, in which case we will
            # have an invalid null prim here.
            if not prim:
                continue

            if prim and not usdRunTime.inPathChange():
                # resync means that the entire subtree could be invalid, so
                # we need to check/delete the children no matter what.
                # So for now we take the simple but always correct approach:
                # blow subtree away and recreate top node if necessary.
                # Note: Ideally we would just remove the objects children, but
                # it looks like there is no notification for that and there is
                # no way to ask ufe scene for existing children of a scene item.
                item = ufe.Hierarchy.createItem(ufePath)
                notification = ufe.ObjectPostDelete(item)
                ufe.Scene.notifyObjectDelete(notification)
                if prim.IsActive():
                    notification = ufe.ObjectAdd(item)
                    ufe.Scene.notifyObjectAdd(notification)

        for changedPath in notice.GetChangedInfoOnlyPaths():
            usdPrimPathStr = str(changedPath.GetPrimPath())
            ufePath = rootUfePath + ufe.PathSegment(
                usdPrimPathStr, 2, '/')
            # FIXME  We would need to determine if the change is a Transform3d
            # change.  A quick and dirty way would be to compare if
            # changedPath.elementString is equal to the string
            # ".xformOp:translate" (see isTransform3D()).  There might be more
            # robust or higher performance ways to do this.  PPT, 16-Jan-2018.
            ufe.Transform3d.notify(ufePath)

def getStages():
    '''Get list of USD stages.'''
    stageCache = AL_USDMaya.StageCache.Get()
    return stageCache.GetAllStages()

def initialize():
    # Only intended to be called by the plugin initialization, to
    # initialize the stage model.
    global _stagesSubject

    _stagesSubject = _StagesSubject()

def finalize():
    # Only intended to be called by the plugin finalization, to
    # finalize the stage model.
    global _stagesSubject

    _stagesSubject.finalize()
    del _stagesSubject
