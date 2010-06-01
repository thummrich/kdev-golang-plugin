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

#include "iassistant.h"
#include <ktexteditor/view.h>
#include <ktexteditor/document.h>
#include <kaction.h>
#include <QMetaType>

using namespace KDevelop;

Q_DECLARE_METATYPE(KSharedPtr<IAssistantAction>)

KAction* KDevelop::IAssistantAction::toKAction() const {
    KAction* ret = new KAction(KIcon(icon()), description(), 0);
    qRegisterMetaType<KSharedPtr<IAssistantAction> >("KSharedPtr<IAssistantAction>()");
    
    //Add the data as a KSharedPtr to the action, so this assistant stays alive at least as long as the KAction
    ret->setData(QVariant::fromValue(KSharedPtr<IAssistantAction>(const_cast<IAssistantAction*>(this))));
    
    connect(ret, SIGNAL(triggered(bool)), SLOT(execute()), Qt::QueuedConnection);
    return ret;
}

KDevelop::IAssistant::~IAssistant() {
}

KDevelop::IAssistantAction::IAssistantAction() : KSharedObject(*(QObject*)this) {
}

KDevelop::IAssistantAction::~IAssistantAction() {
}

QIcon KDevelop::IAssistantAction::icon() const {
    return QIcon();
}

QString KDevelop::IAssistantAction::toolTip() const {
    return QString();
}

unsigned int KDevelop::IAssistantAction::flags() const {
    return NoFlag;
}

KDevelop::IAssistant::IAssistant() : KSharedObject(*(QObject*)this) {
}


QIcon KDevelop::IAssistant::icon() const {
    return QIcon();
}

QString KDevelop::IAssistant::title() const {
    return QString();
}

void KDevelop::IAssistant::doHide() {
    kDebug() << "hiding";
    emit hide();
}

QList< KDevelop::IAssistantAction::Ptr > KDevelop::IAssistant::actions() const {
    return m_actions;
}

void KDevelop::IAssistant::addAction(KDevelop::IAssistantAction::Ptr action) {
    m_actions << action;
}

void KDevelop::IAssistant::clearActions() {
    m_actions.clear();
}

KTextEditor::View* KDevelop::ITextAssistant::view() const {
  return m_view;
}

KTextEditor::Cursor KDevelop::ITextAssistant::invocationCursor() const {
  return m_invocationCursor;
}

KDevelop::ITextAssistant::ITextAssistant(KTextEditor::View* view) {
  m_view = view;
  connect(view, SIGNAL(cursorPositionChanged(KTextEditor::View*,KTextEditor::Cursor)), SLOT(cursorPositionChanged(KTextEditor::View*,KTextEditor::Cursor)));
  connect(view->document(), SIGNAL(textInserted(KTextEditor::Document*,KTextEditor::Range)), SLOT(textInserted(KTextEditor::Document*,KTextEditor::Range)));
  m_invocationCursor = view->cursorPosition();
}

ITextAssistant::~ITextAssistant()
{

}

void KDevelop::ITextAssistant::textInserted(KTextEditor::Document* document, KTextEditor::Range range) {
    if(document->text(range).contains("\n"))
        emit hide();
}

void KDevelop::ITextAssistant::cursorPositionChanged(KTextEditor::View* /*view*/, KTextEditor::Cursor cursor) {
  if(abs((m_invocationCursor - cursor).line()) > 2)
    emit hide();
}

QString KDevelop::DummyAssistantAction::description() const {
    return m_description;
}

KDevelop::DummyAssistantAction::DummyAssistantAction(QString desc) : m_description(desc) {
}

void KDevelop::DummyAssistantAction::execute() {
}


#include "iassistant.moc"
