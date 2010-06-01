/*
   Copyright 2007 David Nolden <david.nolden.kdevelop@art-master.de>

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


#include "abstractnavigationwidget.h"

#include <QtCore/QMap>
#include <QtCore/QStringList>
#include <QtCore/QMetaObject>
#include <QtGui/QScrollBar>
#include <QtGui/QBoxLayout>

#include <klocale.h>

#include "../declaration.h"
#include "../ducontext.h"
#include "../duchainlock.h"
#include "../functiondeclaration.h"
#include "../functiondefinition.h"
#include "../forwarddeclaration.h"
#include "../namespacealiasdeclaration.h"
#include "../classfunctiondeclaration.h"
#include "../classmemberdeclaration.h"
#include "../topducontext.h"
#include "abstractnavigationcontext.h"
#include "abstractdeclarationnavigationcontext.h"
#include "navigationaction.h"
#include "useswidget.h"
#include "../../../interfaces/icore.h"
#include "../../../interfaces/idocumentcontroller.h"
#include <qapplication.h>
#include <qevent.h>

namespace KDevelop {

AbstractNavigationWidget::AbstractNavigationWidget()
  : m_browser(0), m_currentWidget(0)
{
  setPalette( QApplication::palette() );
  setFocusPolicy(Qt::NoFocus);
  resize(100, 100);
}

const int maxNavigationWidgetWidth = 580;

QSize AbstractNavigationWidget::sizeHint() const
{
  if(m_browser) {
    updateIdealSize();
    QSize ret = QSize(qMin(m_idealTextSize.width(), maxNavigationWidgetWidth), qMin(m_idealTextSize.height(), 300));
    
    if(m_currentWidget) {
      ret.setHeight( ret.height() + m_currentWidget->sizeHint().height() );
      if(m_currentWidget->sizeHint().width() > ret.width())
        ret.setWidth(m_currentWidget->sizeHint().width());
      if(ret.width() < 500) //When we embed a widget, give it some space, even if it doesn't have a large size-hint
        ret.setWidth(500);
      
    }
    return ret;
  } else
    return QWidget::sizeHint();
}

void AbstractNavigationWidget::initBrowser(int height) {
  Q_UNUSED(height);
  m_browser = new KTextBrowser;
  
  // since we can embed arbitrary HTML we have to make sure it stays readable by forcing a black-white palette
  m_browser->setPalette( QPalette( Qt::black, Qt::white ) );

  m_browser->setOpenLinks(false);
  m_browser->setOpenExternalLinks(false);

  QVBoxLayout* layout = new QVBoxLayout;
  layout->addWidget(m_browser);
  layout->setMargin(0);
  setLayout(layout);

  connect( m_browser, SIGNAL(anchorClicked(const QUrl&)), this, SLOT(anchorClicked(const QUrl&)) );
  
  foreach(QWidget* w, findChildren<QWidget*>())
    w->setContextMenuPolicy(Qt::NoContextMenu);
}

AbstractNavigationWidget::~AbstractNavigationWidget() {
  if(m_currentWidget)
    layout()->removeWidget(m_currentWidget);
    
}

void AbstractNavigationWidget::setContext(NavigationContextPointer context, int initBrows)
{
  if(m_browser == 0)
    initBrowser(initBrows);
    
  if(!context) {
    kDebug() << "no new context created";
    return;
  }
  if(context == m_context && (!context || context->alreadyComputed()))
    return;
  m_context = context;
  update();
  
  emit sizeHintChanged();
}

void AbstractNavigationWidget::updateIdealSize() const {
  if(m_context && !m_idealTextSize.isValid()) {
    QTextDocument doc;
    doc.setHtml(m_currentText);
    if(doc.idealWidth() > maxNavigationWidgetWidth) {
      doc.setPageSize( QSize(maxNavigationWidgetWidth, 30) );
      m_idealTextSize.setWidth(maxNavigationWidgetWidth);
    }else{
      m_idealTextSize.setWidth(doc.idealWidth());    
    }
    m_idealTextSize.setHeight(doc.size().height());
  }
}

void AbstractNavigationWidget::update() {
  setUpdatesEnabled(false);
  Q_ASSERT( m_context );
  
  QString html = m_context->html();
  if(!html.isEmpty()) {
    int scrollPos = m_browser->verticalScrollBar()->value();
    
    m_browser->setHtml( html );
    
    m_currentText = html;
    m_idealTextSize = QSize();

    QSize hint = sizeHint();
    if(hint.height() >= m_idealTextSize.height()) {
      m_browser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }else{
      m_browser->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    }
    
    m_browser->verticalScrollBar()->setValue(scrollPos);
    m_browser->scrollToAnchor("currentPosition");
    m_browser->show();
  }else{
    m_browser->hide();
  }
  
  if(m_currentWidget) {
    layout()->removeWidget(m_currentWidget);
    m_currentWidget->setParent(0);
  }

  m_currentWidget = m_context->widget();
  
  m_browser->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  m_browser->setMaximumHeight(10000);
  
  if(m_currentWidget) {
    //This connection is a bit hacky..
    connect(m_currentWidget, SIGNAL(navigateDeclaration(KDevelop::IndexedDeclaration)),  this, SLOT(navigateDeclaration(KDevelop::IndexedDeclaration)));
    layout()->addWidget(m_currentWidget);
    if(m_context->isWidgetMaximized()) {
      //Leave unused room to the widget
      m_browser->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
      m_browser->setMaximumHeight(25);
    }
  }

  setUpdatesEnabled(true);
}

NavigationContextPointer AbstractNavigationWidget::context() {
  return m_context;
}

void AbstractNavigationWidget::navigateDeclaration(KDevelop::IndexedDeclaration decl) {
  DUChainReadLocker lock( DUChain::lock() );
  setContext(m_context->accept(decl));
}

void AbstractNavigationWidget::anchorClicked(const QUrl& url) {
  DUChainReadLocker lock( DUChain::lock() );

  //We may get deleted while the call to acceptLink, so make sure we don't crash in that case
  QPointer<AbstractNavigationWidget> thisPtr(this);
  NavigationContextPointer oldContext = m_context;
  NavigationContextPointer nextContext = m_context->acceptLink(url.toString());
  
  if(thisPtr)
    setContext( nextContext );
}

void AbstractNavigationWidget::keyPressEvent(QKeyEvent* event) {
  QWidget::keyPressEvent(event);
}

void AbstractNavigationWidget::executeContextAction(QString action) {
  DUChainReadLocker lock( DUChain::lock() );
  //We may get deleted while the call to acceptLink, so make sure we don't crash in that case
  QPointer<AbstractNavigationWidget> thisPtr(this);
  NavigationContextPointer oldContext = m_context;
  NavigationContextPointer nextContext = m_context->executeLink(action);
  
  if(thisPtr)
    setContext( nextContext );
}

void AbstractNavigationWidget::next() {
  DUChainReadLocker lock( DUChain::lock() );
  Q_ASSERT( m_context );
  m_context->nextLink();
  update();
}

void AbstractNavigationWidget::previous() {
  DUChainReadLocker lock( DUChain::lock() );
  Q_ASSERT( m_context );
  m_context->previousLink();
  update();
}

void AbstractNavigationWidget::accept() {
  DUChainReadLocker lock( DUChain::lock() );
  Q_ASSERT( m_context );
  
  QPointer<AbstractNavigationWidget> thisPtr(this);
  NavigationContextPointer oldContext = m_context;
  NavigationContextPointer nextContext = m_context->accept();
  
  if(thisPtr)
    setContext( nextContext );
}

void AbstractNavigationWidget::back() {
  DUChainReadLocker lock( DUChain::lock() );

  QPointer<AbstractNavigationWidget> thisPtr(this);
  NavigationContextPointer oldContext = m_context;
  NavigationContextPointer nextContext = m_context->back();
  
  if(thisPtr)
    setContext( nextContext );
}

void AbstractNavigationWidget::up() {
  DUChainReadLocker lock( DUChain::lock() );
  m_context->up();
  update();
}

void AbstractNavigationWidget::down() {
  DUChainReadLocker lock( DUChain::lock() );
  m_context->down();
  update();
}

void AbstractNavigationWidget::embeddedWidgetAccept() {
  accept();
}
void AbstractNavigationWidget::embeddedWidgetDown() {
  down();
}

void AbstractNavigationWidget::embeddedWidgetRight() {
  next();
}

void AbstractNavigationWidget::embeddedWidgetLeft() {
  previous();
}

void AbstractNavigationWidget::embeddedWidgetUp() {
  up();
}


void AbstractNavigationWidget::wheelEvent(QWheelEvent* event )
{
    QWidget::wheelEvent(event);
    event->accept();
    return;
}



}

#include "abstractnavigationwidget.moc"
