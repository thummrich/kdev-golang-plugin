/*
   Copyright 2008 David Nolden <david.nolden.kdevelop@art-master.de>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License version 2 as published by the Free Software Foundation.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include "documentchangeset.h"
#include "coderepresentation.h"
#include <qstringlist.h>
#include <editor/modificationrevisionset.h>
#include <interfaces/icore.h>
#include <interfaces/ilanguagecontroller.h>
#include <language/backgroundparser/backgroundparser.h>
#include <interfaces/idocumentcontroller.h>
#include <language/duchain/duchain.h>
#include <language/duchain/duchainlock.h>
#include <language/duchain/duchainutils.h>
#include <language/duchain/parsingenvironment.h>
#include <interfaces/isourceformattercontroller.h>
#include <interfaces/isourceformatter.h>
#include <interfaces/iproject.h>
#include <KLocalizedString>
#include <algorithm>

namespace KDevelop {

struct DocumentChangeSetPrivate
{
    DocumentChangeSet::ReplacementPolicy replacePolicy;
    DocumentChangeSet::FormatPolicy formatPolicy;
    DocumentChangeSet::DUChainUpdateHandling updatePolicy;
    DocumentChangeSet::ActivationPolicy activationPolicy;
    
    QMap< IndexedString, QList<DocumentChangePointer> > changes;
    
    DocumentChangeSet::ChangeResult addChange(DocumentChangePointer change);
    DocumentChangeSet::ChangeResult replaceOldText(CodeRepresentation * repr, const QString & newText, const QList<DocumentChangePointer> & sortedChangesList);
    DocumentChangeSet::ChangeResult generateNewText(const KDevelop::IndexedString & file, QList< KDevelop::DocumentChangePointer > & sortedChanges, const KDevelop::CodeRepresentation* repr, QString& output);
    DocumentChangeSet::ChangeResult removeDuplicates(const IndexedString & file, QList<DocumentChangePointer> & filteredChanges);
    void formatChanges();
    void updateFiles();
};

//Simple helper to clear up code clutter
namespace
{
inline bool changeIsValid(const DocumentChange & change, const QStringList & textLines)
{
    return change.m_range.start <= change.m_range.end &&
           change.m_range.end.line < textLines.size() &&
           change.m_range.start.line >= 0 &&
           change.m_range.start.column >= 0 &&
           change.m_range.start.column <= textLines[change.m_range.start.line].length() &&
           change.m_range.end.column >= 0 && 
           change.m_range.end.column <= textLines[change.m_range.end.line].length() && 
           change.m_range.start.line == change.m_range.end.line;
}

inline bool duplicateChanges(DocumentChangePointer previous, DocumentChangePointer current)
{
    //Given the option of considering a duplicate two changes in the same range but with different old texts to be ignored
    return previous->m_range == current->m_range &&
           previous->m_newText == current->m_newText &&
           (previous->m_oldText == current->m_oldText ||
           (previous->m_ignoreOldText && current->m_ignoreOldText));
}
}

DocumentChangeSet::DocumentChangeSet() : d(new DocumentChangeSetPrivate)
{
    d->replacePolicy = StopOnFailedChange;
    d->formatPolicy = AutoFormatChanges;
    d->updatePolicy = SimpleUpdate;
    d->activationPolicy = DoNotActivate;
}

DocumentChangeSet::DocumentChangeSet(const DocumentChangeSet & rhs) : d(new DocumentChangeSetPrivate(*rhs.d))
{
}


DocumentChangeSet& DocumentChangeSet::operator=(const KDevelop::DocumentChangeSet& rhs)
{
    *d = *rhs.d;
    return *this;
}

DocumentChangeSet::~DocumentChangeSet()
{
    delete d;
}

KDevelop::DocumentChangeSet::ChangeResult DocumentChangeSet::addChange(const KDevelop::DocumentChange& change) {
    return d->addChange(DocumentChangePointer(new DocumentChange(change)));
}

DocumentChangeSet::ChangeResult DocumentChangeSet::addChange(DocumentChangePointer change)
{
    return d->addChange(change);
}

DocumentChangeSet::ChangeResult DocumentChangeSetPrivate::addChange(DocumentChangePointer change) {
    if(change->m_range.start.line != change->m_range.end.line)
        return DocumentChangeSet::ChangeResult("Multi-line ranges are not supported");
    
    changes[change->m_document].append(change);
    return true;
}

void DocumentChangeSet::setReplacementPolicy ( DocumentChangeSet::ReplacementPolicy policy )
{
    d->replacePolicy = policy;
}

void DocumentChangeSet::setFormatPolicy ( DocumentChangeSet::FormatPolicy policy )
{
    d->formatPolicy = policy;
}

void DocumentChangeSet::setUpdateHandling ( DocumentChangeSet::DUChainUpdateHandling policy )
{
    d->updatePolicy = policy;
}

void DocumentChangeSet::setActivationPolicy(DocumentChangeSet::ActivationPolicy policy)
{
    d->activationPolicy = policy;
}

QMap< IndexedString, InsertArtificialCodeRepresentationPointer > DocumentChangeSet::temporaryCodeRepresentations() const
{
    QMap< IndexedString, InsertArtificialCodeRepresentationPointer > ret;
    
    ChangeResult result(true);
    
    foreach(const IndexedString &file, d->changes.keys())
    {
        CodeRepresentation::Ptr repr = createCodeRepresentation(file);

        if(!repr)
            continue;
        
        QList<DocumentChangePointer> sortedChangesList;
        result = d->removeDuplicates(file, sortedChangesList);
        if(!result)
            continue;

        QString newText;
        
        result = d->generateNewText(file, sortedChangesList, repr.data(), newText);
        if(!result)
            continue;
        
        InsertArtificialCodeRepresentationPointer code( new InsertArtificialCodeRepresentation(IndexedString(file.toUrl().fileName()), newText) );
        ret.insert(file, code);
    }
    
    return ret;
}

DocumentChangeSet::ChangeResult DocumentChangeSet::applyAllChanges() {
    QMap<IndexedString, CodeRepresentation::Ptr> codeRepresentations;
    QMap<IndexedString, QString> newTexts;
    QMap<IndexedString, QList<DocumentChangePointer> > filteredSortedChanges;
    ChangeResult result(true);
    
    QList<KDevelop::IndexedString > files(d->changes.keys());

    foreach(const IndexedString &file, files)
    {
        CodeRepresentation::Ptr repr = createCodeRepresentation(file);
        if(!repr)
            return ChangeResult(QString("Could not create a Representation for %1").arg(file.str()));
        
        codeRepresentations[file] = repr;
        
        QList<DocumentChangePointer>& sortedChangesList(filteredSortedChanges[file]);
        {
            result = d->removeDuplicates(file, sortedChangesList);
            if(!result)
                return result;
        }

        {
            result = d->generateNewText(file, sortedChangesList, repr.data(), newTexts[file]);
            if(!result)
                return result;
        }
    }
    
    QMap<IndexedString, QString> oldTexts;
    
    //Apply the changes to the files
    foreach(const IndexedString &file, files)
    {
        oldTexts[file] = codeRepresentations[file]->text();
        
        result = d->replaceOldText(codeRepresentations[file].data(), newTexts[file], filteredSortedChanges[file]);
        if(!result && d->replacePolicy == StopOnFailedChange)
        {
            //Revert all files 
            foreach(const IndexedString &revertFile, oldTexts.keys())
                codeRepresentations[revertFile]->setText(oldTexts[revertFile]);
            
            return result;
        }
    }
        
    ModificationRevisionSet::clearCache();

    d->updateFiles();
    
    if(d->activationPolicy == Activate)
        foreach(const IndexedString& file, files)
            ICore::self()->documentController()->openDocument(file.toUrl());
    
    return result;
}

DocumentChangeSet::ChangeResult DocumentChangeSetPrivate::replaceOldText(CodeRepresentation * repr,
                                                                         const QString & newText,
                                                                         const QList<DocumentChangePointer> & sortedChangesList)
{
    DynamicCodeRepresentation* dynamic = dynamic_cast<DynamicCodeRepresentation*>(repr);
    if(dynamic) {
        dynamic->startEdit();
        //Replay the changes one by one
        
        for(int pos = sortedChangesList.size()-1; pos >= 0; --pos)
        {
            const DocumentChange& change(*sortedChangesList[pos]);
            if(!dynamic->replace(change.m_range.textRange(), change.m_oldText, change.m_newText, change.m_ignoreOldText))
            {
                QString warningString = QString("Inconsistent change in %1 at %2:%3 -> %4:%5 = %6(encountered \"%7\") -> \"%8\"")
                                                .arg(change.m_document.str()).arg(change.m_range.start.line).arg(change.m_range.start.column)
                                                .arg(change.m_range.end.line).arg(change.m_range.end.column).arg(change.m_oldText)
                                                .arg(dynamic->rangeText(change.m_range.textRange())).arg(change.m_newText);

                if(replacePolicy == DocumentChangeSet::WarnOnFailedChange)
                {
                    kWarning() << warningString;
                }
                else if(replacePolicy == DocumentChangeSet::StopOnFailedChange)
                {
                    dynamic->endEdit();
                    return DocumentChangeSet::ChangeResult(warningString);
                }
                //If set to ignore failed changes just continue with the others
            }
        }
        
        dynamic->endEdit();
        return true;
    }
    
    //For files on disk
    if (!repr->setText(newText))
    {
        QString warningString = QString("Could not replace text in the document: %1").arg(sortedChangesList.begin()->data()->m_document.str());
        if(replacePolicy == DocumentChangeSet::WarnOnFailedChange)
        {
            kWarning() << warningString;
        }
        
        return DocumentChangeSet::ChangeResult(warningString);
    }
    
    return true;
}

DocumentChangeSet::ChangeResult DocumentChangeSetPrivate::generateNewText(const IndexedString & file,
                                                                          QList<DocumentChangePointer> & sortedChanges,
                                                                          const CodeRepresentation * repr,
                                                                          QString & output)
{

    ISourceFormatter* formatter = 0;
    if(ICore::self())
        formatter = ICore::self()->sourceFormatterController()->formatterForUrl(file.toUrl());

    //Create the actual new modified file
    QStringList textLines = repr->text().split('\n');

    KMimeType::Ptr mime = KMimeType::findByUrl(file.toUrl());
    
    for(int pos = sortedChanges.size()-1; pos >= 0; --pos) {
        DocumentChange& change(*sortedChanges[pos]);
        QString encountered;
        if(changeIsValid(change, textLines)  && //We demand this, although it should be fixed
            ((encountered = textLines[change.m_range.start.line].mid(change.m_range.start.column, change.m_range.end.column-change.m_range.start.column)) == change.m_oldText || change.m_ignoreOldText))
        {
            ///Problem: This does not work if the other changes significantly alter the context @todo Use the changed context
            QString leftContext = QStringList(textLines.mid(0, change.m_range.start.line+1)).join("\n");
            leftContext.chop(textLines[change.m_range.start.line].length() - change.m_range.start.column);

            QString rightContext = QStringList(textLines.mid(change.m_range.end.line)).join("\n").mid(change.m_range.end.column);

            if(formatter && formatPolicy == DocumentChangeSet::AutoFormatChanges)
                change.m_newText = formatter->formatSource(change.m_newText, mime, leftContext, rightContext);
            
            textLines[change.m_range.start.line].replace(change.m_range.start.column, change.m_range.end.column-change.m_range.start.column, change.m_newText);
        }else{
            QString warningString = QString("Inconsistent change in %1 at %2:%3 -> %4:%5 = \"%6\"(encountered \"%7\") -> \"%8\"")
                                            .arg(file.str()).arg(change.m_range.start.line).arg(change.m_range.start.column)
                                            .arg(change.m_range.end.line).arg(change.m_range.end.column).arg(change.m_oldText)
                                            .arg(encountered).arg(change.m_newText);
            
            if(replacePolicy == DocumentChangeSet::IgnoreFailedChange) {
                //Just don't do the replacement
            }else if(replacePolicy == DocumentChangeSet::WarnOnFailedChange)
                kWarning() << warningString;
            else
                return DocumentChangeSet::ChangeResult(warningString, sortedChanges[pos]);
                
        }
    }

    output = textLines.join("\n");
    return true;
}

//Removes all duplicate changes for a single file, and then returns (via filteredChanges) the filtered duplicates
DocumentChangeSet::ChangeResult DocumentChangeSetPrivate::removeDuplicates(const IndexedString & file,
                                                                           QList<DocumentChangePointer> & filteredChanges)
{
    QMultiMap<SimpleCursor, DocumentChangePointer> sortedChanges;
    
    foreach(const DocumentChangePointer &change, changes[file])
        sortedChanges.insert(change->m_range.end, change);
    
    //Remove duplicates
    QMultiMap<SimpleCursor, DocumentChangePointer>::iterator previous = sortedChanges.begin();
    for(QMultiMap<SimpleCursor, DocumentChangePointer>::iterator it = ++sortedChanges.begin(); it != sortedChanges.end(); ) {
        if(( *previous ) && ( *previous )->m_range.end > (*it)->m_range.start) {
            //intersection
            if(duplicateChanges(( *previous ), *it)) {
                //duplicate, remove one
                it = sortedChanges.erase(it);
                continue;
            }
            
            //When two changes contain each other, and the container change is set to ignore old text, then it should be safe to
            //just ignore the contained change, and apply the bigger change
            else if((*it)->m_range.contains(( *previous )->m_range) && (*it)->m_ignoreOldText  )
            {
                kDebug() << "Removing change: " << ( *previous )->m_oldText << "->" << ( *previous )->m_newText
                         << ", because it is contained by change: " << (*it)->m_oldText << "->" << (*it)->m_newText;
                sortedChanges.erase(previous);
            }
            //This case is for when both have the same end, either of them could be the containing range
            else if((*previous)->m_range.contains((*it)->m_range) && (*previous)->m_ignoreOldText  )
            {
                kDebug() << "Removing change: " << (*it)->m_oldText << "->" << (*it)->m_newText
                         << ", because it is contained by change: " << ( *previous )->m_oldText << "->" << ( *previous )->m_newText;
                it = sortedChanges.erase(it);
                continue;
            }
            else
                return DocumentChangeSet::ChangeResult(
                       QString("Inconsistent change-request at %1; intersecting changes: \"%2\"->\"%3\"@%4:%5->%6:%7 & \"%8\"->\"%9\"@%10:%11->%12:%13 ")
                       .arg(file.str(), ( *previous )->m_oldText, ( *previous )->m_newText).arg(( *previous )->m_range.start.line).arg(( *previous )->m_range.start.column)
                       .arg(( *previous )->m_range.end.line).arg(( *previous )->m_range.end.column).arg((*it)->m_oldText, (*it)->m_newText).arg((*it)->m_range.start.line)
                       .arg((*it)->m_range.start.column).arg((*it)->m_range.end.line).arg((*it)->m_range.end.column));
            
        }
        previous = it;
        ++it;
    }
    
    filteredChanges = sortedChanges.values();
    return true;
}

void DocumentChangeSetPrivate::updateFiles()
{
    if(updatePolicy != DocumentChangeSet::NoUpdate && ICore::self())
        foreach(const IndexedString &file, changes.keys())
        {
                if(!file.toUrl().isValid()) {
                    kWarning() << "Trying to apply changes to an invalid document";
                    continue;
                }
                
                ICore::self()->languageController()->backgroundParser()->addDocument(file.toUrl());
                foreach(const KUrl& doc, ICore::self()->languageController()->backgroundParser()->managedDocuments()) {
                    DUChainReadLocker lock(DUChain::lock());
                    TopDUContext* top = DUChainUtils::standardContextForUrl(doc);
                    if((top && top->parsingEnvironmentFile() && top->parsingEnvironmentFile()->needsUpdate()) || !top) {
                        lock.unlock();
                        ICore::self()->languageController()->backgroundParser()->addDocument(doc);
                    }
                }
        }
}

}
