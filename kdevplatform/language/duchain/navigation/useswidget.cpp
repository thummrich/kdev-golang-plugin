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

#include "useswidget.h"
#include <language/duchain/duchainlock.h>
#include <language/duchain/duchain.h>
#include <language/duchain/uses.h>
#include <QVBoxLayout>
#include <QLabel>
#include <QCompleter>
#include <QProgressBar>
#include <KComboBox>
#include <qpushbutton.h>
#include <limits>
#include <klocalizedstring.h>
#include <qabstractitemview.h>
#include <ktexteditor/smartrange.h>
#include <QToolButton>
#include <language/duchain/use.h>
#include <kicon.h>
#include <interfaces/icore.h>
#include <interfaces/idocumentcontroller.h>
#include <ktexteditor/document.h>
#include <QFile>
#include <interfaces/iprojectcontroller.h>
#include <qtextdocument.h>
#include <qevent.h>
#include <qtextlayout.h>
#include <language/duchain/duchainutils.h>
#include "language/duchain/parsingenvironment.h"
#include "language/duchain/types/indexedtype.h"
#include <language/codegen/coderepresentation.h>
#include <language/backgroundparser/parsejob.h>
#include <interfaces/iproject.h>


using namespace KDevelop;

QString sizePrefix;// = "<small><small>";
QString sizeSuffix;// = "</small></small>";

const int tooltipContextSize = 3; //How many lines around the use are shown in the tooltip
const bool showUsesHeader = false;

///The returned text is fully escaped
///@param cutOff The total count of characters that should be cut of, all in all on both sides together.
///@param range The range that is highlighted, and that will be preserved during cutting, given that there is enough room beside it.
QString highlightAndEscapeUseText(QString line, uint cutOff, SimpleRange range) {
  uint leftCutRoom = range.start.column;
  uint rightCutRoom = line.length() - range.end.column;

  if(range.start.column < 0 || range.end.column > line.length() || cutOff > leftCutRoom + rightCutRoom)
    return QString(); //Not enough room for cutting off on sides

  uint leftCut = 0;
  uint rightCut = 0;

  if(leftCutRoom < rightCutRoom) {
    if(leftCutRoom * 2 >= cutOff) {
      //Enough room on both sides. Just cut.
      leftCut = cutOff / 2;
      rightCut = cutOff - leftCut;
    }else{
      //Not enough room in left side, but enough room all together
      leftCut = leftCutRoom;
      rightCut = cutOff - leftCut;
    }
  }else{
    if(rightCutRoom * 2 >= cutOff) {
      //Enough room on both sides. Just cut.
      rightCut = cutOff / 2;
      leftCut = cutOff - rightCut;
    }else{
      //Not enough room in right side, but enough room all together
      rightCut = rightCutRoom;
      leftCut = cutOff - rightCut;
    }
  }
  Q_ASSERT(leftCut + rightCut <= cutOff);

  line = line.left(line.length() - rightCut);
  line = line.mid(leftCut);
  range.start.column -= leftCut;
  range.end.column -= leftCut;

  Q_ASSERT(range.start.column >= 0 && range.end.column <= line.length());

  return Qt::escape(line.left(range.start.column)) + "<span style=\"background-color:yellow\">" + Qt::escape(line.mid(range.start.column, range.end.column - range.start.column)) + "</span>" + Qt::escape(line.mid(range.end.column, line.length() - range.end.column)) ;
}

OneUseWidget::OneUseWidget(IndexedDeclaration declaration, IndexedString document, SimpleRange range, const CodeRepresentation& code, KTextEditor::SmartRange* smartRange) : m_range(range), m_smartRange(smartRange), m_declaration(declaration), m_document(document) {

  //Make the sizing of this widget independent of the content, because we will adapt the content to the size
  setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

  m_sourceLine = code.line(m_range.start.line);
  if(m_smartRange)
    m_smartRange->addWatcher(this);

  connect(this, SIGNAL(linkActivated(QString)), this, SLOT(jumpTo()));

  DUChainReadLocker lock(DUChain::lock());
  QString text = sizePrefix + "<a href='open'>" + i18n("Line") + sizeSuffix + QString(" <b>%1%2%3</b></a>").arg(sizePrefix).arg(range.start.line).arg(sizeSuffix);
  text += sizePrefix;
  if(!m_sourceLine.isEmpty() && m_sourceLine.length() > m_range.end.column) {

    text += ' ' + highlightAndEscapeUseText(m_sourceLine, 0, m_range);

    //Useful tooltip:
    int start = m_range.start.line - tooltipContextSize;
    int end = m_range.end.line + tooltipContextSize + 1;

    QString toolTipText;
    for(int a = start; a < end; ++a) {
      QString lineText = code.line(a);
      if(!lineText.isEmpty())
        toolTipText += lineText + '\n';
    }
    setToolTip(toolTipText);
  }
  text += sizeSuffix;
  setText(text);
}

void OneUseWidget::jumpTo() {
  if(m_smartRange)
    m_range = *m_smartRange; ///@todo smart-locking
        //This is used to execute the slot delayed in the event-loop, so crashes are avoided
  ICore::self()->documentController()->openDocument(m_document.toUrl(), m_range.start.textCursor());
}

OneUseWidget::~OneUseWidget() {
  if(m_smartRange)
    m_smartRange->removeWatcher(this);
}

void OneUseWidget::resizeEvent ( QResizeEvent * event ) {
  ///Adapt the content
  QSize size = event->size();

  int cutOff = 0;
  int maxCutOff = m_sourceLine.length() - (m_range.end.column - m_range.start.column);

  //Reset so we also get more context while up-sizing
  setText(i18n("<a href='open'>Line <b>%1</b></a> %2", m_range.start.line+1, highlightAndEscapeUseText(m_sourceLine, cutOff, m_range)));

  while(sizeHint().width() > size.width() && cutOff < maxCutOff) {
    //We've got to save space
    setText(i18n("<a href='open'>Line <b>%1</b></a> %2", m_range.start.line+1, highlightAndEscapeUseText(m_sourceLine, cutOff, m_range)));
    cutOff += 5;
  }

  event->accept();

  QLabel::resizeEvent(event);
}

void OneUseWidget::rangeDeleted(KTextEditor::SmartRange* smartRange) {
  Q_ASSERT(smartRange == m_smartRange);
  m_smartRange = 0;
}

void NavigatableWidgetList::setShowHeader(bool show) {
  if(show && !m_headerLayout->parent())
    m_layout->insertLayout(0, m_headerLayout);
  else
    m_headerLayout->setParent(0);
}

NavigatableWidgetList::~NavigatableWidgetList() {
  delete m_headerLayout;
}

NavigatableWidgetList::NavigatableWidgetList(bool allowScrolling, uint maxHeight, bool vertical) : m_maxHeight(maxHeight), m_allowScrolling(allowScrolling) {
  m_layout = new QVBoxLayout;
  m_layout->setMargin(0);
  m_layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
  m_layout->setSpacing(0);
  setBackgroundRole(QPalette::Light);
  m_useArrows = false;

  if(vertical)
    m_itemLayout = new QVBoxLayout;
  else
    m_itemLayout = new QHBoxLayout;

  m_itemLayout->setMargin(0);
  m_itemLayout->setSpacing(0);
//   m_layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
//   setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Maximum);
  setWidgetResizable(true);

  m_headerLayout = new QHBoxLayout;
  m_headerLayout->setMargin(0);
  m_headerLayout->setSpacing(0);

  if(m_useArrows) {
    m_previousButton = new QToolButton();
    m_previousButton->setIcon(KIcon("go-previous"));

    m_nextButton = new QToolButton();
    m_nextButton->setIcon(KIcon("go-next"));

    m_headerLayout->addWidget(m_previousButton);
    m_headerLayout->addWidget(m_nextButton);
  }

  //hide these buttons for now, they're senseless

  m_layout->addLayout(m_headerLayout);
  m_layout->addLayout(m_itemLayout);

  if(maxHeight)
    setMaximumHeight(maxHeight);

  if(m_allowScrolling) {
    QWidget* contentsWidget = new QWidget;
    contentsWidget->setLayout(m_layout);
    setWidget(contentsWidget);
  }else{
    setLayout(m_layout);
  }
}

void NavigatableWidgetList::deleteItems() {
  foreach(QWidget* item, items())
    delete item;
}

void NavigatableWidgetList::addItem(QWidget* widget, int pos) {
  if(pos == -1)
    m_itemLayout->addWidget(widget);
  else
    m_itemLayout->insertWidget(pos, widget);
}

QList<QWidget*> NavigatableWidgetList::items() const {
  QList<QWidget*> ret;
  for(int a = 0; a < m_itemLayout->count(); ++a) {
    QWidgetItem* widgetItem = dynamic_cast<QWidgetItem*>(m_itemLayout->itemAt(a));
    if(widgetItem) {
      ret << widgetItem->widget();
    }
  }
  return ret;
}

bool NavigatableWidgetList::hasItems() const {
  return (bool)m_itemLayout->count();
}

void NavigatableWidgetList::addHeaderItem(QWidget* widget, Qt::Alignment alignment) {
  if(m_useArrows) {
    Q_ASSERT(m_headerLayout->count() >= 2); //At least the 2 back/next buttons
    m_headerLayout->insertWidget(m_headerLayout->count()-1, widget, alignment);
  }else{
    //We need to do this so the header doesn't get stretched
    widget->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    m_headerLayout->insertWidget(m_headerLayout->count(), widget, alignment);
//     widget->setMaximumHeight(20);
  }
}

///Returns whether the uses in the child should be a new uses-group
bool isNewGroup(DUContext* parent, DUContext* child) {
  if(parent->type() == DUContext::Other && child->type() == DUContext::Other)
    return false;
  else
    return true;
}

uint countUses(int usedDeclarationIndex, DUContext* context) {
  uint ret = 0;

  for(int useIndex = 0; useIndex < context->usesCount(); ++useIndex)
    if(context->uses()[useIndex].m_declarationIndex == usedDeclarationIndex)
      ++ret;

  foreach(DUContext* child, context->childContexts())
    if(!isNewGroup(context, child))
      ret += countUses(usedDeclarationIndex, child);
  return ret;
}

QList<OneUseWidget*> createUseWidgets(const CodeRepresentation& code, int usedDeclarationIndex, IndexedDeclaration decl, DUContext* context) {
  QList<OneUseWidget*> ret;
  for(int useIndex = 0; useIndex < context->usesCount(); ++useIndex)
    if(context->uses()[useIndex].m_declarationIndex == usedDeclarationIndex)
      ret << new OneUseWidget(decl, context->url(), context->uses()[useIndex].m_range, code, context->useSmartRange(useIndex));

  foreach(DUContext* child, context->childContexts())
    if(!isNewGroup(context, child))
      ret += createUseWidgets(code, usedDeclarationIndex, decl, child);

  return ret;
}

ContextUsesWidget::ContextUsesWidget(const CodeRepresentation& code, QList<IndexedDeclaration> usedDeclarations, IndexedDUContext context) : m_context(context) {

  DUChainReadLocker lock(DUChain::lock());
    QString headerText = i18n("Unknown context");
    uint usesCount = 0;
    setUpdatesEnabled(false);

    if(context.data()) {
      DUContext* ctx = context.data();

      if(ctx->scopeIdentifier(true).isEmpty())
        headerText = i18n("Global");
      else {
        headerText = ctx->scopeIdentifier(true).toString();
        if(ctx->type() == DUContext::Function || (ctx->owner() && ctx->owner()->isFunctionDeclaration()))
          headerText += "(..)";
      }

      QSet<int> hadIndices;

      foreach(const IndexedDeclaration &usedDeclaration, usedDeclarations) {
        int usedDeclarationIndex = ctx->topContext()->indexForUsedDeclaration(usedDeclaration.data(), false);
        if(hadIndices.contains(usedDeclarationIndex))
          continue;

        hadIndices.insert(usedDeclarationIndex);

        if(usedDeclarationIndex != std::numeric_limits<int>::max()) {
          foreach(OneUseWidget* widget, createUseWidgets(code, usedDeclarationIndex, usedDeclaration, ctx))
            addItem(widget);

          usesCount = countUses(usedDeclarationIndex, ctx);
        }
      }
    }

    QLabel* headerLabel = new QLabel(sizePrefix + "<b>" + i18n("Context") + "</b>" + " <a href='navigateToFunction'>" + Qt::escape(headerText) + "</a>" + sizeSuffix);
    addHeaderItem(headerLabel);
    if(usesCount) {
      QLabel* usesCountLabel = new QLabel("&nbsp;" + i18np("<i>(1 use)</i>", "<i>(%1 uses)</i>", usesCount));
      usesCountLabel->setAlignment(Qt::AlignLeft);
      addHeaderItem(usesCountLabel, Qt::AlignLeft);
    }

    setUpdatesEnabled(true);

    connect(headerLabel, SIGNAL(linkActivated(QString)), this, SLOT(linkWasActivated(QString)));
}

void ContextUsesWidget::linkWasActivated(QString link) {

  IndexedDeclaration decl;
  bool declValid = false;
  {
    DUChainReadLocker lock(DUChain::lock());

    if(m_context.context() && m_context.context()->owner())
      decl = IndexedDeclaration(m_context.context()->owner());
    declValid = decl.isValid();
  }

  if(link == "navigateToFunction" && declValid)
    emit navigateDeclaration(decl);
}

DeclarationsWidget::DeclarationsWidget(const CodeRepresentation& code, QList<IndexedDeclaration> declarations) : m_declarations(declarations) {

  DUChainReadLocker lock(DUChain::lock());
    setUpdatesEnabled(false);

    QLabel* headerLabel = new QLabel(sizePrefix + "<b>" + i18n("Declaration") + "</b>" + sizeSuffix);
    addHeaderItem(headerLabel);

    foreach(const IndexedDeclaration &decl, declarations)
      if(decl.data())
        addItem(new OneUseWidget(decl, decl.data()->url(), decl.data()->range(), code, decl.data()->smartRange()));

    setUpdatesEnabled(true);
}

TopContextUsesWidget::TopContextUsesWidget(IndexedDeclaration declaration, QList<IndexedDeclaration> allDeclarations, IndexedTopDUContext topContext) :
m_topContext(topContext),
m_declaration(declaration),
m_allDeclarations(allDeclarations) {
    setUpdatesEnabled(false);
    DUChainReadLocker lock(DUChain::lock());
    QHBoxLayout * labelLayout = new QHBoxLayout;
    QWidget* headerWidget = new QWidget;
    headerWidget->setLayout(labelLayout);
    headerWidget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

    QLabel* label = new QLabel;
    m_button = new QToolButton;
    labelLayout->addWidget(m_button, Qt::AlignLeft | Qt::AlignVCenter);
    labelLayout->addWidget(label, Qt::AlignLeft | Qt::AlignVCenter);

    QString projectName = i18n("No project");
    QString fileName = topContext.url().str();
    int usesCount = 0;

/*    usesCountLabel->setAlignment((Qt::Alignment)(Qt::AlignLeft | Qt::AlignVCenter));
    projectLabel->setAlignment((Qt::Alignment)(Qt::AlignLeft | Qt::AlignVCenter));*/

    if(topContext.data()) {
      KDevelop::IProject* project = ICore::self()->projectController()->findProjectForUrl(topContext.data()->url().toUrl());
      if(project) {
        projectName = project->name();
        fileName = project->relativeUrl(topContext.data()->url().toUrl()).toLocalFile();
      }
    }

    if(topContext.isLoaded())
      usesCount = DUChainUtils::contextCountUses(topContext.data(), declaration.data());

    label->setText(i18np("<b>Project:</b> %2 &nbsp; <b>File:</b> %3 &nbsp; <i>(1 use)</i>", "<b>Project:</b> %2 &nbsp; <b>File:</b> %3 &nbsp; <i>(%1 uses)</i>", usesCount, projectName, fileName));

    m_button->setIcon(KIcon("go-next"));
    connect(m_button, SIGNAL(clicked(bool)), this, SLOT(labelClicked()));
    addHeaderItem(headerWidget);
    setUpdatesEnabled(true);
}

QList<ContextUsesWidget*> buildContextUses(const CodeRepresentation& code, QList<IndexedDeclaration> declarations, DUContext* context) {
  QList<ContextUsesWidget*> ret;

  if(!context->parentContext() || isNewGroup(context->parentContext(), context)) {
    ContextUsesWidget* created = new ContextUsesWidget(code, declarations, context);
    if(created->hasItems())
      ret << created;
    else
        delete created;
  }

  foreach(DUContext* child, context->childContexts())
    ret += buildContextUses(code, declarations, child);

  return ret;
}

void TopContextUsesWidget::setExpanded(bool expanded) {
  if(!expanded) {
    deleteItems();
    m_button->setIcon(KIcon("go-next"));
  }else{
    m_button->setIcon(KIcon("go-down"));
    if(hasItems())
      return;
    DUChainReadLocker lock(DUChain::lock());
    TopDUContext* topContext = m_topContext.data();

    if(topContext && m_declaration.data()) {

      CodeRepresentation::Ptr code = createCodeRepresentation(topContext->url());
      setUpdatesEnabled(false);

      IndexedTopDUContext localTopContext(topContext);
      QList<IndexedDeclaration> localDeclarations;
      foreach(const IndexedDeclaration &decl, m_allDeclarations)
        if(decl.indexedTopContext() == localTopContext)
          localDeclarations << decl;

        if(!localDeclarations.isEmpty())
          addItem(new DeclarationsWidget(*code, localDeclarations));

      foreach(ContextUsesWidget* usesWidget, buildContextUses(*code, m_allDeclarations, topContext)) {
        addItem(usesWidget);
        connect(usesWidget, SIGNAL(navigateDeclaration(KDevelop::IndexedDeclaration)),  this, SIGNAL(navigateDeclaration(KDevelop::IndexedDeclaration)));
      }
      setUpdatesEnabled(true);
    }
  }
}

void TopContextUsesWidget::labelClicked() {
  if(hasItems()) {
    setExpanded(false);
    m_button->setIcon(KIcon("go-next"));
  }else{
    setExpanded(true);
    m_button->setIcon(KIcon("go-down"));
  }
}

UsesWidget::~UsesWidget() {
  delete m_collector;
}

UsesWidget::UsesWidget(IndexedDeclaration declaration, UsesWidgetCollector* customCollector) : NavigatableWidgetList(true) {
    DUChainReadLocker lock(DUChain::lock());
    setUpdatesEnabled(false);

    m_progressBar = new QProgressBar;
    addHeaderItem(m_progressBar);

    if(!customCollector) {
      m_collector = new UsesWidgetCollector(declaration);
    }else{
      m_collector = customCollector;
    }

    m_collector->setProcessDeclarations(true);
    m_collector->setWidget(this);
    m_collector->startCollecting();

    setUpdatesEnabled(true);
}

UsesWidget::UsesWidgetCollector::UsesWidgetCollector(IndexedDeclaration decl) : UsesCollector(decl), m_widget(0) {

}

void UsesWidget::UsesWidgetCollector::setWidget(UsesWidget* widget ) {
  m_widget = widget;
}


void UsesWidget::UsesWidgetCollector::maximumProgress(uint max) {
  if(m_widget->m_progressBar) {
    m_widget->m_progressBar->setMaximum(max);
    m_widget->m_progressBar->setMinimum(0);
    m_widget->m_progressBar->setValue(0);
  }else{
    kWarning() << "maximumProgress called twice";
  }
}

void UsesWidget::UsesWidgetCollector::progress(uint processed, uint total) {
  if(m_widget->m_progressBar) {
    m_widget->m_progressBar->setValue(processed);

    if(processed == total) {
      m_widget->setUpdatesEnabled(false);
      delete m_widget->m_progressBar;
      m_widget->m_progressBar = 0;
      m_widget->setShowHeader(false);
      m_widget->setUpdatesEnabled(true);
    }
  }else{
    kWarning() << "progress() called too often";
  }
}

void UsesWidget::UsesWidgetCollector::processUses( KDevelop::ReferencedTopDUContext topContext ) {
  DUChainReadLocker lock(DUChain::lock());
  kDebug() << "processing" << topContext->url().str();
    TopContextUsesWidget* widget = new TopContextUsesWidget(declaration(), declarations(), topContext.data());
    connect(widget, SIGNAL(navigateDeclaration(KDevelop::IndexedDeclaration)),  m_widget, SIGNAL(navigateDeclaration(KDevelop::IndexedDeclaration)));
    bool expand = false;
    bool toFront = false;
    if(ICore::self()->documentController()->activeDocument() &&  ICore::self()->documentController()->activeDocument()->url().equals(topContext->url().toUrl())) {
      expand = true; //Expand + move to front the item belonging to the current open document
      toFront = true;
    }

    if(m_widget->items().count() == 0)
      expand = true; //Expand the first item

    if(expand)
      widget->setExpanded(true);

    m_widget->addItem(widget, toFront ? 0 : -1);
}

QSize KDevelop::UsesWidget::sizeHint() const {
  QSize ret = QWidget::sizeHint();
  if(ret.height() < 300)
    ret.setHeight(300);
  return ret;
}


#include "useswidget.moc"
