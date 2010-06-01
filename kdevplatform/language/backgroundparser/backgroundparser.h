/*
 * This file is part of KDevelop
 *
 * Copyright 2006 Adam Treat <treat@kde.org>
 * Copyright 2007 Kris Wong <kris.p.wong@gmail.com>
 * Copyright 2007-2008 David Nolden <david.nolden.kdevelop@art-master.de>
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

#ifndef BACKGROUNDPARSER_H
#define BACKGROUNDPARSER_H

#include <QtCore/QMap>
#include <QtCore/QPair>
#include <QtCore/QMutex>
#include <QtCore/QHash>
#include <QtCore/QPointer>

#include <KDE/KUrl>

#include <KDE/KTextEditor/SmartRangeWatcher>

#include "../languageexport.h"
#include <interfaces/istatus.h>
#include <language/duchain/topducontext.h>

class QMutex;

namespace ThreadWeaver
{
class Weaver;
class Job;
}

namespace KDevelop
{

class ILanguageController;
class ParseJob;
class ParserDependencyPolicy;

class KDEVPLATFORMLANGUAGE_EXPORT BackgroundParser : public QObject, public IStatus, public KTextEditor::SmartRangeWatcher
{
    Q_OBJECT
    Q_INTERFACES( KDevelop::IStatus )

public:
    BackgroundParser(ILanguageController *languageController);
    ~BackgroundParser();

    virtual QString statusName() const;

    enum {
        BestPriority = -10000,  ///Best possible job-priority. No jobs should actually have this.
        NormalPriority = 0,     ///Standard job-priority. This priority is used for parse-jobs caused by document-editing/opening.
                                ///There is an additional parsing-thread reserved for jobs with this and better priority, to improve responsiveness.
        WorstPriority = 100000  ///Worst possible job-priority.
    };
    
    /**
     * Abort or dequeue all current running jobs with the specified @p parent.
     */
    Q_SCRIPTABLE void clear(QObject* parent);

    /**
     * Queries the background parser as to whether there is currently
     * a parse job for @p document, and if so, returns it.
     *
     * This may not contain all of the parse jobs that are intended
     * unless you call in from your job's ThreadWeaver::Job::aboutToBeQueued()
     * function.
     */
    Q_SCRIPTABLE ParseJob* parseJobForDocument(const KUrl& document) const;

    /**
     * The dependency policy which applies to all jobs (it is applied automatically).
     */
    Q_SCRIPTABLE ParserDependencyPolicy* dependencyPolicy() const;

    /**
     * Set how many ThreadWeaver threads the background parser should set up and use.
     */
    Q_SCRIPTABLE void setThreadCount(int threadCount);

    /**
     * Set the delay in miliseconds before the background parser starts parsing.
     */
    Q_SCRIPTABLE void setDelay(int miliseconds);

    /**
     * Inform the background parser that \a document has a given top smart \a range.
     *
     * This will be watched for modifications and background jobs scheduled accordingly.
     */
    Q_SCRIPTABLE void addManagedTopRange(const KUrl& document, KTextEditor::SmartRange* range);

    /**
     * Returns all documents that were added through addManagedTopRange. This is typically the currently
     * open documents.
     */
    Q_SCRIPTABLE QList<KUrl> managedDocuments();
    
    /**
     * Remove an associated top \a range from modification watching.
     */
    Q_SCRIPTABLE void removeManagedTopRange(KTextEditor::SmartRange* range);

Q_SIGNALS:
    /** 
	 * Emitted whenever a document parse-job has finished. 
	 * The job contains the du-chain(if one was created) etc.
	 *
	 * The job is deleted after this signal has been emitted.  Receivers should not hold
	 * references to it.
	 */
    void parseJobFinished(KDevelop::ParseJob* job);

    // Implementations of IStatus signals
    void clearMessage( KDevelop::IStatus* );
    void showMessage( KDevelop::IStatus*, const QString & message, int timeout = 0);
    void hideProgress( KDevelop::IStatus* );
    void showProgress( KDevelop::IStatus*, int minimum, int maximum, int value);
    void showErrorMessage( const QString&, int );

public Q_SLOTS:

    /**
     * Suspends execution of the background parser
     */
    void suspend();

    /**
     * Resumes execution of the background parser
     */
    void resume();

    ///Reverts all requests that were made for the given notification-target.
    ///priorities and requested features will be reverted as well.
    void revertAllRequests(QObject* notifyWhenReady);
    
    /**
     * Queues up the @p url to be parsed.
     * @p features The minimum features that should be computed for this top-context
     * @p priority A value that manages the order of parsing. Documents with lowest priority are parsed first.
     * @param notifyReady An optional pointer to a QObject that should contain a slot
     *                    "void updateReady(KDevelop::IndexedString url, KDevelop::ReferencedTopDUContext topContext)".
     *                    The notification is guaranteed to be called once for each call to addDocument. The given top-context
     *                    may be invalid if the update failed.
     */
    void addDocument(const KUrl& url, TopDUContext::Features features = TopDUContext::VisibleDeclarationsAndContexts, int priority = 0, QObject* notifyWhenReady = 0);

    /**
     * Queues up the list of @p urls to be parsed.
     * @p features The minimum features that should be computed for these top-contexts
     * @p priority A value that manages the order of parsing. Documents with lowest priority are parsed first.
     */
    void addDocumentList(const KUrl::List& urls, TopDUContext::Features features = TopDUContext::VisibleDeclarationsAndContexts, int priority = 0);

    /**
     * Removes the @p url that is registered for the given notification from the url.
     * @param notifyWhenReady Notifier the document was added with
     */
    void removeDocument(const KUrl& url, QObject* notifyWhenReady = 0);

    /**
     * Forces the current queue to be parsed.
     */
    void parseDocuments();

    void updateProgressBar();

    ///Disables processing for all jobs that have a worse priority than @param priority
    ///This can be used to temporarily limit the processing to only the most important jobs.
    ///To only enable processing for important jobs, call setNeededPriority(0).
    ///This should only be used to temporarily alter the processing. A progress-bar
    ///will still be shown for the not yet processed jobs.
    void setNeededPriority(int priority);
    ///Disables all processing of new jobs, equivalent to setNeededPriority(BestPriority)
    void disableProcessing();
    ///Enables all processing of new jobs, equivalent to setNeededPriority(WorstPriority)
    void enableProcessing();
    
    ///Returns true if the given url is queued for parsing
    bool isQueued(KUrl url) const;

    ///Returns the number of currently active or queued jobs
    int queuedCount() const;

protected:
    void loadSettings(bool projectIsLoaded);
    void saveSettings(bool projectIsLoaded);

protected Q_SLOTS:
    void parseComplete(ThreadWeaver::Job *job);
    void parseProgress(KDevelop::ParseJob*, float value, QString text);
    void startTimer();
    void aboutToQuit();

protected:
    // Receive changed notifications
    using KTextEditor::SmartRangeWatcher::rangeContentsChanged;
    virtual void rangeContentsChanged(KTextEditor::SmartRange* range, KTextEditor::SmartRange* mostSpecificChild);

private:
    friend class BackgroundParserPrivate;
    class BackgroundParserPrivate *d;
};

}
#endif
