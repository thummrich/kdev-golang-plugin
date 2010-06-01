/*
    Copyright 2009 David Nolden <david.nolden.kdevelop@art-master.de>
    
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

/**
 * This is an internal header
*/

#ifndef VCSDIFFPATCHSOURCES_H
#define VCSDIFFPATCHSOURCES_H

#include <ktemporaryfile.h>
#include <qtextstream.h>
#include <interfaces/ipatchsource.h>
#include "vcsdiff.h"
#include <kdebug.h>
#include <qtextedit.h>
#include "interfaces/ibasicversioncontrol.h"

namespace KDevelop {
class VcsCommitDialog;
}

class QWidget;

class VCSDiffPatchSource : public KDevelop::IPatchSource {
    public:
    VCSDiffPatchSource(const KDevelop::VcsDiff& diff) ;
        
    virtual KUrl baseDir() const ;
    
    virtual KUrl file() const ;
    
    virtual QString name() const ;
    
    virtual void update() ;
    
    virtual bool isAlreadyApplied() const { return true; }
    
    KUrl m_base, m_file;
    QString m_name;
};

class VCSCommitDiffPatchSource : public VCSDiffPatchSource {
    Q_OBJECT
    public:
    VCSCommitDiffPatchSource(const KDevelop::VcsDiff& vcsdiff, QMap<KUrl, QString> selectable, KDevelop::IBasicVersionControl* vcs);
    
    ~VCSCommitDiffPatchSource() ;
    
    virtual bool canSelectFiles() const ;
    
    QMap<KUrl, QString> additionalSelectableFiles() const ;
    
    virtual QWidget* customWidget() const ;
    
    virtual QString finishReviewCustomText() const ;
    
    virtual bool canCancel() const;
    
    virtual void cancelReview();
    
    virtual bool finishReview(QList< KUrl > selection) ;
Q_SIGNALS:
    void reviewFinished(QString message, QList<KUrl> selection);
public:
    QPointer<QWidget> m_commitMessageWidget;
    QPointer<QTextEdit> m_commitMessageEdit;
    QMap<KUrl, QString> m_selectable;
    KDevelop::IBasicVersionControl* m_vcs;
};

///Sends the diff to the patch-review plugin.
///Returns whether the diff was shown successfully.
bool showVcsDiff(KDevelop::IPatchSource* vcsDiff);

#endif // VCSDIFFPATCHSOURCES_H
