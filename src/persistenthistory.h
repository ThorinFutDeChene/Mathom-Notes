/*
 * Mathom Notes: guarded cross-session undo history.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef PERSISTENTHISTORY_H
#define PERSISTENTHISTORY_H

class BNPView;
class QUndoCommand;

class PersistentHistory
{
public:
    // Writes a bounded, recoverable journal while Note objects still exist.
    static bool save(BNPView *view);
    // Must run after document tree and tag definitions are loaded.
    static int restore(BNPView *view);
    // Distinguishes recovered commands from actions created after relaunch.
    static bool isRecovered(const QUndoCommand *command);
};

#endif
