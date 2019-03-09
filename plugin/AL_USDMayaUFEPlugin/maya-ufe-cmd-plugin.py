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

"""Maya plugin module for Maya UFE integration.

This module is a prototype UFE integration into Maya providing services for all
Maya UFE plugins, regardless of their run-time.  The production technology for
this will be a C++ plugin, with publicly available C++ headers, so that
third-party Maya UFE plugins can use the services in this plugin.

As of 17-Aug-2018, there are two Maya UFE integration services provided:
undoable selection and parenting.

Operations in Maya are performed on the selection.  This imposes the
requirement that selection in Maya must be an undoable operation, so that
operations after an undo have the proper selection as input.  Maya selections
done through non-UFE UIs already go through the undoable select command.
Selections done through UFE UIs must use the undoable command in this plugin.

Parenting in Maya can be used using the selection only, parenting all selected
objects except the last under the last.  However, this is a very awkward
workflow, and therefore parenting using non-selected items is far more common.
Because of this, we have created the ufeParent command in this plugin.

"""

import maya.api.OpenMaya as OpenMaya

from ufeScripts import ufeSelectCmd
from ufeScripts import ufeParentCmd

ufeVersion = '0.1'

# Using the Maya Python API 2.0.
def maya_useNewAPI():
    pass

commands = [ufeSelectCmd.SelectAppendCmd, ufeSelectCmd.SelectRemoveCmd,
            ufeSelectCmd.SelectClearCmd, ufeSelectCmd.SelectReplaceWithCmd,
            ufeParentCmd.ParentCmd]

def initializePlugin(mobject):
    """ Initialize all the needed nodes """
    mplugin = OpenMaya.MFnPlugin(mobject, "Autodesk", ufeVersion, "Any")

    for cmd in commands:
        try:
            mplugin.registerCommand(cmd.kCmdName, cmd.creator)
        except:
            OpenMaya.MGlobal.displayError('Register failed for %s' % cmd.kCmdName)

def uninitializePlugin(mobject):
    """ Uninitialize all the nodes """
    mplugin = OpenMaya.MFnPlugin(mobject, "Autodesk", ufeVersion, "Any")

    for cmd in commands:
        try:
            mplugin.deregisterCommand(cmd.kCmdName)
        except:
            OpenMaya.MGlobal.displayError('Unregister failed for %s' % cmd.kCmdName)
