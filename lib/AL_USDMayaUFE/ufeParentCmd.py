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

"""Maya UFE parent command.

This command changes the parent of an existing UFE object.
"""

import maya.api.OpenMaya as OpenMaya

import maya.cmds as cmds

import ufe

# Messages should be localized.
kUfeParentCmd = 'UFE parent command %s arguments not set up.'

def parent(children, parent):
    return ParentCmd.execute(children, parent)

#==============================================================================
# CLASS ParentCmdBase
#==============================================================================

class ParentCmdBase(OpenMaya.MPxCommand):
    """Base class for the UFE parent command.

    This command is the base class for the concrete UFE parent command.
    """

    def __init__(self):
        super(ParentCmdBase, self).__init__()

    def isUndoable(self):
        return True

    def validateArgs(self):
        return True

    def doIt(self, args):
        # Completely ignore the MArgList argument, as it's unnecessary:
        # arguments to the commands are passed in Python object form
        # directly to the command's constructor.

        if self.validateArgs() is False:
            self.displayWarning(kUfeParentCmd % self.kCmdName)
        else:
            self.redoIt()

#==============================================================================
# CLASS ParentCmd
#==============================================================================

class ParentCmd(ParentCmdBase):
    """Change the parent of a UFE item."""

    kCmdName = 'ufeParent'

    # Command data.  Must be set before creating an instance of the command
    # and executing it.
    children = None
    parent = None

    # Command return data.  Set by doIt().  A list of the newly-parented
    # scene items.
    result = None

    @staticmethod
    def execute(children, parent):
        """Change the parent of children to be parent.

        The items list cannot be empty.  Returns the reparented items."""

        ParentCmd.children = children
        ParentCmd.parent = parent
        cmds.ufeParent()
        result = ParentCmd.result
        ParentCmd.children = None
        ParentCmd.parent = None
        ParentCmd.result = None
        return result

    @staticmethod
    def creator():
        return ParentCmd(ParentCmd.children, ParentCmd.parent)

    def __init__(self, children, parent):
        super(ParentCmd, self).__init__()
        self.children = children
        self.parent = parent
        # A list of reparented child scene items, possibly renamed.  This
        # is the first element in each undo data tuple.
        self.result = None
        # A list of tuples for undo purposes.  The data in each tuple is of
        # the form
        # (reparentedChildSceneItem, previousPath, childIndexInPreviousParent).
        self.undoData = None

    def validateArgs(self):
        return self.children is not None and self.parent is not None

    def doIt(self, args):
        super(ParentCmd, self).doIt(args)
        # Save the result out as a class member.
        ParentCmd.result = self.result

    def redoIt(self):
        parent = ufe.Hierarchy.hierarchy(self.parent)
        self.undoData = [parent.appendChild(child) for child in self.children]
        # Extract the child scene item from the undo data.
        self.result = [childData.child for childData in self.undoData]

    def undoIt(self):
        if self.result:
            # To restore the child to its previous parent, we need:
            # - the child
            # - the previous parent
            # - the previous child name
            # - the previous child index position in the child list.
            # We have the new items in self.result.  We must extract the
            # previous parent from the saved children paths, and restore
            # each child's name, as it may have changed.
            for (child, prevPath, prevIndex) in self.undoData:
                prevParentItem = ufe.Hierarchy.createItem(prevPath.pop())
                prevParent = ufe.Hierarchy.hierarchy(prevParentItem)
                prevParent.insertChild(child, prevPath.back(), prevIndex)
