/*  This file is part of KDevelop
    Copyright 2009 Aleix Pol <aleixpol@kde.org>
    Copyright 2009 David Nolden <david.nolden.kdevelop@art-master.de>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include "qthelpdocumentation.h"
#include <QWebView>
#include <QLabel>
#include <KLocale>
#include <interfaces/icore.h>
#include <interfaces/idocumentationcontroller.h>
#include "qthelpnetwork.h"
#include "qthelpplugin.h"
#include <QtGui/QTreeView>
#include <QHelpContentModel>
#include <QHeaderView>

QtHelpPlugin* QtHelpDocumentation::s_provider=0;

QtHelpDocumentation::QtHelpDocumentation(const QString& name, const QMap<QString, QUrl>& info)
    : m_name(name), m_info(info), m_current(info.constBegin()), lastView(0)
{}

QtHelpDocumentation::QtHelpDocumentation(const QString& name, const QMap<QString, QUrl>& info, const QString& key)
    : m_name(name), m_info(info), m_current(m_info.find(key))
{ Q_ASSERT(m_current!=m_info.constEnd()); }

QString QtHelpDocumentation::description() const
{
    QUrl url(m_current.value());
    QByteArray data = s_provider->engine()->fileData(url);

    //Extract a short description from the html data
    QString dataString = QString::fromLatin1(data); ///@todo encoding
    QString fragment = url.fragment();
    
    QString p = "((\\\")|(\\\'))";
    QString exp = "< a name = " + p + fragment + p + " > < / a >";
    QString optionalSpace = "( )*";
    exp.replace(' ', optionalSpace);
    QRegExp findFragment(exp);
    int pos = findFragment.indexIn(dataString);
    if(fragment.isEmpty()) {
        pos = 0;
    }else{
        //Check if there is a title opening-tag right before the fragment, and if yes add it, so we have a nicely formatted caption
        QString titleRegExp("< h\\d class = \".*\" >");
        titleRegExp.replace(" ", optionalSpace);
        QRegExp findTitle(titleRegExp);
        int titleStart = findTitle.lastIndexIn(dataString, pos);
        int titleEnd = titleStart + findTitle.matchedLength();
        if(titleStart != -1) {
            QString between = dataString.mid(titleEnd, pos-titleEnd).trimmed();
//                     if(between.isEmpty())
                pos = titleStart;
        }
    }
    
    if(pos != -1) {
        
        QString exp = "< a name = " + p + "((\\S)*)" + p + " > < / a >";
        exp.replace(" ", optionalSpace);
        QRegExp nextFragmentExpression(exp);
        int endPos = nextFragmentExpression.indexIn(dataString, pos+(fragment.size() ? findFragment.matchedLength() : 0));
        if(endPos == -1)
            endPos = dataString.size();

        {
            //Find the end of the last paragraph or newline, so we don't add prefixes of the following fragment
            QString newLineRegExp("< br / > | < / p >");
            newLineRegExp.replace(" ", optionalSpace);
            QRegExp lastNewLine(newLineRegExp);
            int newEnd = dataString.lastIndexOf(lastNewLine, endPos);
            if(newEnd != -1 && newEnd > pos)
                endPos = newEnd + lastNewLine.matchedLength();
        }
        
        {
            //Find the title, and start from there
            QString titleRegExp("< h\\d class = \"title\" >");
            titleRegExp.replace(" ", optionalSpace);
            QRegExp findTitle(titleRegExp);
            int idx = findTitle.indexIn(dataString);
            if(idx > pos && idx < endPos)
                pos = idx;
        }
        
        
        QString thisFragment = dataString.mid(pos, endPos - pos);
        
        {
            //Completely remove the first large header found, since we don't need a header
            QString headerRegExp("< h\\d.*>.*< / h\\d >");
            headerRegExp.replace(" ", optionalSpace);
            QRegExp findHeader(headerRegExp);
            findHeader.setMinimal(true);
            int idx = findHeader.indexIn(thisFragment);
            if(idx != -1) {
                thisFragment.remove(idx, findHeader.matchedLength());
            }
        }
        
        {
            //Replace all gigantic header-font sizes with <big>

            {
                QString sizeRegExp("< h\\d ");
                sizeRegExp.replace(" ", optionalSpace);
                QRegExp findSize(sizeRegExp);
                thisFragment.replace(findSize, "<big ");
            }
            {
                QString sizeCloseRegExp("< / h\\d >");
                sizeCloseRegExp.replace(" ", optionalSpace);
                QRegExp closeSize(sizeCloseRegExp);
                thisFragment.replace(closeSize, "</big><br />");
            }
        }
        
        {
            //Replace paragraphs by newlines
            
            QString begin("< p >");
            begin.replace(" ", optionalSpace);
            
            QRegExp findBegin(begin);
            thisFragment.replace(findBegin, "");

            QString end("< /p >");
            end.replace(" ", optionalSpace);
            
            QRegExp findEnd(end);
            thisFragment.replace(findEnd, "<br />");
        }
        
        {
            //Remove links, because they won't work
            QString link("< a href = " + p + ".*" + p);
            link.replace(" ", optionalSpace);
            QRegExp exp(link, Qt::CaseSensitive);
            exp.setMinimal(true);
            thisFragment.replace(exp, "<a ");
        }
        
        return thisFragment;
    }
    
    return QStringList(m_info.keys()).join(", ");
}

QWidget* QtHelpDocumentation::documentationWidget(QWidget* parent)
{
    QWidget* ret;
    if(m_info.isEmpty()) { //QtHelp sometimes has empty info maps. e.g. availableaudioeffects i 4.5.2
        ret=new QLabel(i18n("Could not find any documentation for '%1'", m_name), parent);
    } else {
        QWebView* view=new QWebView(parent);
        view->page()->setNetworkAccessManager(new HelpNetworkAccessManager(s_provider->engine(), 0));
        view->page()->setLinkDelegationPolicy(QWebPage::DelegateAllLinks);
        view->setContextMenuPolicy(Qt::ActionsContextMenu);
        
        foreach(const QString& name, m_info.keys()) {
            QtHelpAlternativeLink* act=new QtHelpAlternativeLink(name, this, view);
            
            act->setCheckable(true);
            act->setChecked(name==m_current.key());
            view->addAction(act);
        }
        QObject::connect(view, SIGNAL(linkClicked(QUrl)), SLOT(jumpedTo(QUrl)));
        
        view->load(m_current.value());
        ret=view;
        lastView=view;
    }
    return ret;
}

void QtHelpDocumentation::jumpedTo(const QUrl& newUrl)
{
    Q_ASSERT(lastView);
    s_provider->jumpedTo(newUrl);
    lastView->load(newUrl);
}

KDevelop::IDocumentationProvider* QtHelpDocumentation::provider() const
{
    return s_provider;
}

QtHelpAlternativeLink::QtHelpAlternativeLink(const QString& name, const QtHelpDocumentation* doc, QObject* parent)
    : QAction(name, parent), mDoc(doc), mName(name)
{
    connect(this, SIGNAL(triggered()), SLOT(showUrl()));
}

void QtHelpAlternativeLink::showUrl()
{
    KSharedPtr<KDevelop::IDocumentation> newDoc(new QtHelpDocumentation(mName, mDoc->info(), mName));
    KDevelop::ICore::self()->documentationController()->showDocumentation(newDoc);
}

QWidget* HomeDocumentation::documentationWidget(QWidget* parent)
{
    QTreeView* w=new QTreeView(parent);
    w->header()->setVisible(false);
    w->setModel(QtHelpDocumentation::s_provider->engine()->contentModel());
    
    connect(w, SIGNAL(clicked(QModelIndex)), SLOT(clicked(QModelIndex)));
    return w;
}

void HomeDocumentation::clicked(const QModelIndex& idx)
{
    QHelpContentModel* model=QtHelpDocumentation::s_provider->engine()->contentModel();
    QHelpContentItem* it=model->contentItemAt(idx);
    QMap<QString, QUrl> info;
    info.insert(it->title(), it->url());
    
    KSharedPtr<KDevelop::IDocumentation> newDoc(new QtHelpDocumentation(it->title(), info));
    KDevelop::ICore::self()->documentationController()->showDocumentation(newDoc);
}

QString HomeDocumentation::name() const
{
    return i18n("QtHelp Home Page");
}

KDevelop::IDocumentationProvider* HomeDocumentation::provider() const
{
    return QtHelpDocumentation::s_provider;
}
