/*
* This file is part of KDevelop
*
* Copyright 2006 Adam Treat <treat@kde.org>
* Copyright 2006-2008 Hamish Rodda <rodda@kde.org>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU Library General Public License as
* published by the Free Software Foundation; either version 2 of the
* License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public
* License along with this program; if not, write to the
* Free Software Foundation, Inc.,
* 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/

#ifndef PARSEJOB_H
#define PARSEJOB_H

#include <QtCore/QPointer>
#include <KDE/KUrl>

#include <threadweaver/JobSequence.h>

#include "../duchain/indexedstring.h"
#include "documentchangetracker.h"
#include <language/duchain/topducontext.h>

namespace KDevelop
{
class BackgroundParser;
class TopDUContext;
class ReferencedTopDUContext;

/**
 * The base class for background parser jobs.
 */
class KDEVPLATFORMLANGUAGE_EXPORT ParseJob : public ThreadWeaver::JobSequence, public DocumentChangeTracker
{
    Q_OBJECT
public:
    ParseJob( const KUrl &url );
    virtual ~ParseJob();

    Q_SCRIPTABLE BackgroundParser* backgroundParser() const;
    Q_SCRIPTABLE void setBackgroundParser(BackgroundParser* parser);

    Q_SCRIPTABLE virtual int priority() const;

    /**
     * Determine whether the editor can provide the contents of the document or not.
     * Once this is called, the editor integrator saves the revision token, and no changes will
     * be made to the changedRanges().
     * You can then just call KTextEditor::SmartRange::text() on each of the changedRanges().
     * Or, you can parse the whole document, the text of which is available from contentsFromEditor().
     *
     * @NOTE: When this is called, make sure you call @p cleanupSmartRevision() properly.
     */
    Q_SCRIPTABLE bool contentsAvailableFromEditor();

    /**
     * Cleanup SmartRange revision after the job has run. 
     * You must call this before exiting your @p run() method.
     * @p abortJob() will call this automatically.
     */
    virtual void cleanupSmartRevision();

    /// Retrieve the contents of the file from the currently open editor.
    /// Ensure it is loaded by calling editorLoaded() first.
    /// The editor integrator seamlessly saves the revision token and applies it
    Q_SCRIPTABLE QString contentsFromEditor();

    /// Returns the revision token issued by the document's smart interface,
    /// or -1 if there was a problem.
    Q_SCRIPTABLE int revisionToken() const;

    /// \returns the indexed url of the document to be parsed.
    Q_SCRIPTABLE KDevelop::IndexedString document() const;

    /**
    * Sets a list of QObjects that should contain a slot
    * "void updateReady(KDevelop::IndexedString url, KDevelop::ReferencedTopDUContext topContext)".
    * The notification is guaranteed to be called once the parse-job finishes, from within its destructor.
    * The given top-context may be invalid if the update failed.
    */
    Q_SCRIPTABLE void setNotifyWhenReady(QList<QPointer<QObject> > notify);
    
    /// Sets the du-context that was created by this parse-job
    Q_SCRIPTABLE virtual void setDuChain(ReferencedTopDUContext duChain);
    /// Returns the set du-context, or zero of none was set.
    Q_SCRIPTABLE virtual ReferencedTopDUContext duChain() const;

    /// Overridden to allow jobs to determine if they've been requested to abort
    Q_SCRIPTABLE virtual void requestAbort();
    /// Determine if the job has been requested to abort
    Q_SCRIPTABLE bool abortRequested() const;
    /// Sets success to false, causing failed() to be emitted
    Q_SCRIPTABLE void abortJob();

    /// Overridden to convey whether the job succeeded or not.
    Q_SCRIPTABLE virtual bool success() const;

    /// Overridden to set the DependencyPolicy on subjobs.
    Q_SCRIPTABLE virtual void addJob(Job* job);

    /// Set the minimum features the resulting top-context should have
    Q_SCRIPTABLE void setMinimumFeatures(TopDUContext::Features features);
    
    /// Minimum set of features the resulting top-context should have
    Q_SCRIPTABLE TopDUContext::Features minimumFeatures() const;
    
    /// Allows statically specifying an amount of features required for an url.
    /// These features will automatically be or'ed with the minimumFeatures() returned
    /// by any ParseJob with the given url.
    /// Since this causes some additional complixity in update-checking, minimum features should not
    /// be set permanently.
    static void setStaticMinimumFeatures(IndexedString url, TopDUContext::Features features);
    
    /// Must be called exactly once for each call to setStaticMinimumFeatures, with the same features.
    static void unsetStaticMinimumFeatures(IndexedString url, TopDUContext::Features features);
    
    /// Returns the statically set minimum features for  the given url, or zero.
    static TopDUContext::Features staticMinimumFeatures(IndexedString url);
    
    /// Returns whether there is minimum features set up for some url
    static bool hasStaticMinimumFeatures();
    
    /**
     * Attempt to add \a dependency as a dependency of \a actualDependee, which must
     * be a subjob of this job, or null (in which case, the dependency is added
     * to this job).  If a circular dependency is detected, the dependency will
     * not be added and the method will return false.
     */
    Q_SCRIPTABLE bool addDependency(ParseJob* dependency, ThreadWeaver::Job* actualDependee = 0);

Q_SIGNALS:
    /**Can be used to give progress feedback to the background-parser. @param value should be between 0 and 1, where 0 = 0% and 1 = 100%
     * @param text may be a text that describes the current state of parsing
     * Do not trigger this too often, for performance reasons. */
    void progress(KDevelop::ParseJob*, float value, QString text);

private:
    class ParseJobPrivate* const d;
};

}
#endif
