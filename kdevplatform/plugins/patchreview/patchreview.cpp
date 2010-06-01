/***************************************************************************
Copyright 2006-2009 David Nolden <david.nolden.kdevelop@art-master.de>
***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "patchreview.h"

#include <kmimetype.h>
#include <klineedit.h>
#include <kmimetypechooser.h>
#include <kmimetypetrader.h>
#include <krandom.h>
#include <QTabWidget>
#include <QMenu>
#include <QFile>
#include <QTimer>
#include <QMutexLocker>
#include <QPersistentModelIndex>
#include <kfiledialog.h>
#include <interfaces/idocument.h>
#include <QStandardItemModel>
#include <interfaces/icore.h>
#include <kde_terminal_interface.h>
#include <kparts/part.h>
#include <kparts/factory.h>
#include <kdialog.h>
#include <ktemporaryfile.h>


#include "libdiff2/komparemodellist.h"
#include "libdiff2/kompare.h"
#include <kmessagebox.h>
#include <QMetaType>
#include <QVariant>
#include <ktexteditor/cursor.h>
#include <ktexteditor/document.h>
#include <ktexteditor/view.h>
#include <ktexteditor/markinterface.h>
#include <ktexteditor/smartinterface.h>
#include <interfaces/idocumentcontroller.h>
#include <kprocess.h>
#include <interfaces/iuicontroller.h>
#include <kaboutdata.h>

///Whether arbitrary exceptions that occurred while diff-parsing within the library should be caught
#define CATCHLIBDIFF

/* Exclude this file from doublequote_chars check as krazy doesn't understand
std::string*/
//krazy:excludeall=doublequote_chars
#include <language/editor/editorintegrator.h>
#include <krun.h>
#include <kparts/mainwindow.h>
#include <qtextdocument.h>
#include <util/activetooltip.h>
#include <ktextbrowser.h>
#include <kiconeffect.h>
#include <kcolorutils.h>
#include <kcolorscheme.h>
#include <sublime/controller.h>
#include <sublime/mainwindow.h>
#include <sublime/area.h>
#include <interfaces/iprojectcontroller.h>
#include "diffsettings.h"

using namespace KDevelop;

namespace {
  // Maximum number of files to open directly within a tab when the review is started
  const int maximumFilesToOpenDirectly = 15;
}

Q_DECLARE_METATYPE( const Diff2::DiffModel* )

PatchReviewToolView::PatchReviewToolView( QWidget* parent, PatchReviewPlugin* plugin ) 
: QWidget( parent ), m_reversed( false ), m_plugin( plugin ) 
{
    connect( plugin, SIGNAL(patchChanged()), SLOT(patchChanged()) );
    connect( ICore::self()->documentController(), SIGNAL(documentActivated(KDevelop::IDocument*)), this, SLOT(documentActivated(KDevelop::IDocument*)) );
    
    showEditDialog();
    patchChanged();
}

void PatchReviewToolView::patchChanged()
{
    fillEditFromPatch();
    kompareModelChanged();
}

PatchReviewToolView::~PatchReviewToolView() {
}

void PatchReviewToolView::updatePatchFromEdit() {

    IPatchSource::Ptr ips = m_plugin->patch();

    if ( !ips )
        return;
    LocalPatchSource* lpatch = dynamic_cast<LocalPatchSource*>(ips.data());
    if(!lpatch)
      return;

    lpatch->m_command = m_editPatch.command->text();
    lpatch->m_filename = m_editPatch.filename->url();
    lpatch->m_baseDir = m_editPatch.baseDir->url();
//     lpatch->m_depth = m_editPatch.depth->value();

    m_plugin->notifyPatchChanged();
}

void PatchReviewToolView::fillEditFromPatch() {

    IPatchSource::Ptr ipatch = m_plugin->patch();
    if ( !ipatch )
        return ;
    
    disconnect( m_editPatch.patchSelection, SIGNAL(currentIndexChanged(int)), this, SLOT(patchSelectionChanged(int)));
    
    m_editPatch.patchSelection->clear();
    foreach(IPatchSource::Ptr patch, m_plugin->knownPatches())
    {
      if(!patch)
        continue;
      m_editPatch.patchSelection->addItem(patch->icon(), patch->name());
      if(patch == ipatch)
        m_editPatch.patchSelection->setCurrentIndex(m_editPatch.patchSelection->count()-1);
    }
    
    connect( m_editPatch.patchSelection, SIGNAL(currentIndexChanged(int)), this, SLOT(patchSelectionChanged(int)));

    m_editPatch.cancelReview->setVisible(ipatch->canCancel());

    QString finishText = i18n("Finish Review");
    if(!ipatch->finishReviewCustomText().isEmpty())
      finishText = ipatch->finishReviewCustomText();
    kDebug() << "finish-text: " << finishText;
    m_editPatch.finishReview->setText(finishText);
    
    if(m_customWidget) {
      kDebug() << "removing custom widget";
      m_customWidget->hide();
      m_editPatch.verticalLayout->removeWidget(m_customWidget);
    }

    m_customWidget = ipatch->customWidget();
    if(m_customWidget) {
      m_editPatch.verticalLayout->insertWidget(0, m_customWidget);
      m_customWidget->show();
      kDebug() << "got custom widget";
    }
    
    LocalPatchSource* lpatch = dynamic_cast<LocalPatchSource*>(ipatch.data());
    if(!lpatch) {
      m_editPatch.tabWidget->hide();
      m_editPatch.baseDir->hide();
      m_editPatch.label->hide();
      return;
    }else{
      m_editPatch.tabWidget->show();
      m_editPatch.baseDir->show();
      m_editPatch.label->show();
    }
    
    m_editPatch.command->setText( lpatch->m_command );
    m_editPatch.filename->setUrl( lpatch->m_filename );
    m_editPatch.baseDir->setUrl( lpatch->m_baseDir );
//     m_editPatch.depth->setValue( lpatch->m_depth );

    if ( lpatch->m_command.isEmpty() )
        m_editPatch.tabWidget->setCurrentIndex( m_editPatch.tabWidget->indexOf( m_editPatch.fileTab ) );
    else
        m_editPatch.tabWidget->setCurrentIndex( m_editPatch.tabWidget->indexOf( m_editPatch.commandTab ) );
}

void PatchReviewToolView::patchSelectionChanged(int selection)
{
  m_editPatch.filesList->clear();
    if(selection >= 0 && selection < m_plugin->knownPatches().size()) {
      m_plugin->setPatch(m_plugin->knownPatches()[selection]);
    }
}

void PatchReviewToolView::slotEditCommandChanged() {
//     m_editPatch.filename->lineEdit()->setText( "" );
    updatePatchFromEdit();
}

void PatchReviewToolView::slotEditFileNameChanged() {
//     m_editPatch.command->setText( "" );
    updatePatchFromEdit();
}

void PatchReviewToolView::showEditDialog() {

    m_editPatch.setupUi( this );

    m_editPatch.filesList->header()->hide();
    m_editPatch.filesList->setRootIsDecorated(false);
    
    m_editPatch.previousHunk->setIcon(KIcon("arrow-up"));
    m_editPatch.nextHunk->setIcon(KIcon("arrow-down"));
    
    connect( m_editPatch.previousHunk, SIGNAL( clicked( bool ) ), this, SLOT( prevHunk() ) );
    connect( m_editPatch.nextHunk, SIGNAL( clicked( bool ) ), this, SLOT( nextHunk() ) );
    connect( m_editPatch.filesList, SIGNAL( doubleClicked( const QModelIndex& ) ), this, SLOT( fileDoubleClicked( const QModelIndex& ) ) );
    
    connect( m_editPatch.cancelReview, SIGNAL(clicked(bool)), m_plugin, SLOT(cancelReview()) );
    connect( m_editPatch.finishReview, SIGNAL(clicked(bool)), this, SLOT(finishReview()) );
    //connect( m_editPatch.cancelButton, SIGNAL( pressed() ), this, SLOT( slotEditCancel() ) );

    //connect( this, SIGNAL( finished( int ) ), this, SLOT( slotEditDialogFinished( int ) ) );

//     connect( m_editPatch.depth, SIGNAL(valueChanged(int)), SLOT(updatePatchFromEdit()) );
    connect( m_editPatch.filename, SIGNAL( textChanged( const QString& ) ), SLOT(slotEditFileNameChanged()) );
    connect( m_editPatch.baseDir, SIGNAL(textChanged(QString)), SLOT(updatePatchFromEdit()) );

    
    
    m_editPatch.baseDir->setMode(KFile::Directory);

    connect( m_editPatch.command, SIGNAL( textChanged( const QString& ) ), this, SLOT(slotEditCommandChanged()) );
//   connect( m_editPatch.commandToFile, SIGNAL( clicked( bool ) ), this, SLOT( slotToFile() ) );

    connect( m_editPatch.filename->lineEdit(), SIGNAL( returnPressed() ), this, SLOT(slotEditFileNameChanged()) );
    connect( m_editPatch.filename->lineEdit(), SIGNAL( editingFinished() ), this, SLOT(slotEditFileNameChanged()) );
    connect( m_editPatch.filename, SIGNAL( urlSelected( const KUrl& ) ), this, SLOT(slotEditFileNameChanged()) );
    connect( m_editPatch.command, SIGNAL(textChanged(QString)), this, SLOT(slotEditCommandChanged()) );
//     connect( m_editPatch.commandToFile, SIGNAL(clicked(bool)), m_plugin, SLOT(commandToFile()) );

    connect( m_editPatch.patchSelection, SIGNAL(currentIndexChanged(int)), this, SLOT(patchSelectionChanged(int)));
    
    connect( m_editPatch.updateButton, SIGNAL(clicked(bool)), m_plugin, SLOT(forceUpdate()) );

    connect( m_editPatch.showButton, SIGNAL(clicked(bool)), m_plugin, SLOT(showPatch()) );
    
    bool blocked = blockSignals( true );

    blockSignals( blocked );
}

void PatchReviewToolView::nextHunk() {
//   updateKompareModel();
    m_plugin->seekHunk( true );
}

void PatchReviewToolView::prevHunk() {
//   updateKompareModel();
    m_plugin->seekHunk( false );
}

KUrl PatchReviewPlugin::diffFile()
{
    return m_patch->file();
}

void PatchReviewPlugin::seekHunk( bool forwards, const KUrl& fileName ) {
    try {
        if ( !m_modelList.get() )
            throw "no model";

        for (int a = 0; a < m_modelList->modelCount(); ++a) {

            const Diff2::DiffModel* model = m_modelList->modelAt(a);
            if ( !model || !model->differences() )
                continue;

            KUrl file = m_patch->baseDir();
            
            file.addPath( model->sourcePath() );
            file.addPath( model->sourceFile() );
            
            if ( !fileName.isEmpty() && fileName != file )
                continue;

            IDocument* doc = ICore::self()->documentController()->documentForUrl( file );

            if ( doc && doc == ICore::self()->documentController()->activeDocument() && m_highlighters.contains(doc->url()) && m_highlighters[doc->url()] ) {
              
                ICore::self()->documentController()->activateDocument( doc );
                if ( doc->textDocument() ) {
                  
                    KTextEditor::SmartInterface* smart = dynamic_cast<KTextEditor::SmartInterface*>( doc->textDocument() );
                    Q_ASSERT(smart);
                    QMutexLocker lock(smart->smartMutex());

                    const QList< KTextEditor::SmartRange* > ranges = m_highlighters[doc->url()]->ranges();
                    
                    KTextEditor::View * v = doc->textDocument() ->activeView();
                    int bestLine = -1;
                    if ( v ) {
                        KTextEditor::Cursor c = v->cursorPosition();
                        for ( QList< KTextEditor::SmartRange* >::const_iterator it = ranges.begin(); it != ranges.end(); ++it ) {
                            int line;
                            
                            line = (*it)->start().line();

                            if ( forwards ) {
                                if ( line > c.line() && ( bestLine == -1 || line < bestLine ) )
                                    bestLine = line;
                            } else {
                                if ( line < c.line() && ( bestLine == -1 || line > bestLine ) )
                                    bestLine = line;
                            }
                        }
                        if ( bestLine != -1 ) {
                          lock.unlock();
                            v->setCursorPosition( KTextEditor::Cursor( bestLine, 0 ) );
                            return ;
                        }
                    }
                }
            }
        }

    } catch ( const QString & str ) {
        kDebug() << "seekHunk():" << str;
    } catch ( const char * str ) {
        kDebug() << "seekHunk():" << str;
    }
    kDebug() << "no matching hunk found";
}



void PatchReviewPlugin::addHighlighting(const KUrl& highlightFile, IDocument* document)
{
    try {
        if ( !modelList() )
            throw "no model";

        for (int a = 0; a < modelList()->modelCount(); ++a) {
            const Diff2::DiffModel* model = modelList()->modelAt(a);
            if ( !model )
                continue;

            KUrl file = m_patch->baseDir();
            
            file.addPath( model->sourcePath() );
            file.addPath( model->sourceFile() );

            if (file != highlightFile)
                continue;

            kDebug() << "highlighting" << file.prettyUrl();

            IDocument* doc = document;
            if(!doc)
              doc = ICore::self()->documentController()->documentForUrl( file );

            kDebug() << "highlighting file" << file << "with doc" << doc;
            
            if ( !doc || !doc->textDocument() )
                continue;

            removeHighlighting( file );

            m_highlighters[ file ] = new PatchHighlighter( model, doc, this );
        }

    } catch ( const QString & str ) {
        kDebug() << "highlightFile():" << str;
    } catch ( const char * str ) {
        kDebug() << "highlightFile():" << str;
    }
}

void PatchReviewPlugin::highlightPatch() {
    try {
        if ( !modelList() )
            throw "no model";

        for (int a = 0; a < modelList()->modelCount(); ++a) {
            const Diff2::DiffModel* model = modelList()->modelAt(a);
            if ( !model )
                continue;

            KUrl file = m_patch->baseDir();
            
            file.addPath( model->sourcePath() );
            file.addPath( model->sourceFile() );

            addHighlighting(file);
        }

    } catch ( const QString & str ) {
        kDebug() << "highlightFile():" << str;
    } catch ( const char * str ) {
        kDebug() << "highlightFile():" << str;
    }
}


void PatchReviewToolView::finishReview()
{
    QList<KUrl> selectedUrls;
    for(int a = 0; a< m_editPatch.filesList->topLevelItemCount(); ++a) {
      QTreeWidgetItem* item = m_editPatch.filesList->topLevelItem(a);
      if(item && item->checkState(0) == Qt::Checked) {
        QVariant v = item->data(0, Qt::UserRole);
        
        if( v.canConvert<KUrl>() ) {
          selectedUrls << v.value<KUrl>();
        }else if ( v.canConvert<const Diff2::DiffModel*>() ) {
          const Diff2::DiffModel* model = v.value<const Diff2::DiffModel*>();

          KUrl file = m_plugin->patch()->baseDir();
          
          file.addPath( model->sourcePath() );
          file.addPath( model->sourceFile() );
          
          selectedUrls << file;
        }
      }
    }
    kDebug() << "finishing review with" << selectedUrls;
    m_plugin->finishReview(selectedUrls);
}

void PatchReviewToolView::fileDoubleClicked( const QModelIndex& i ) {
    try {
        if ( !m_plugin->modelList() )
            throw "no model";
        
        QVariant v = i.data( Qt::UserRole );
        
        if( v.canConvert<KUrl>() ) {
          KUrl u = v.value<KUrl>();
          ICore::self()->documentController()->openDocument( u, KTextEditor::Cursor() );
          return;
        }
        
        if ( !v.canConvert<const Diff2::DiffModel*>() )
            throw "cannot convert";
        const Diff2::DiffModel* model = v.value<const Diff2::DiffModel*>();
        if ( !model )
            throw "bad model-value";

        KUrl file = m_plugin->patch()->baseDir();
        
        file.addPath( model->sourcePath() );
        file.addPath( model->sourceFile() );

        kDebug() << "opening" << file.toLocalFile();

        ICore::self()->documentController()->openDocument( file, KTextEditor::Cursor() );

        m_plugin->seekHunk( true, file );
    } catch ( const QString & str ) {
        kDebug() << "fileDoubleClicked():" << str;
    } catch ( const char * str ) {
        kDebug() << "fileDoubleClicked():" << str;
    }
}

KUrl PatchReviewToolView::urlForFileModel(const Diff2::DiffModel* model)
{
  KUrl file = m_plugin->patch()->baseDir();
  
  file.addPath( model->sourcePath() );
  file.addPath( model->sourceFile() );
  
  return file;
}

void PatchReviewToolView::kompareModelChanged()
{
    m_editPatch.filesList->clear();
    m_editPatch.filesList->setColumnCount(1);

    if (!m_plugin->modelList())
        return;

    QMap<KUrl, QString> additionalUrls = m_plugin->patch()->additionalSelectableFiles();
    
    const Diff2::DiffModelList* models = m_plugin->modelList()->models();
    if ( !models )
        return;
    Diff2::DiffModelList::const_iterator it = models->constBegin();
    QSet<KUrl> haveUrls;
    for(; it != models->constEnd(); ++it) {
        Diff2::DifferenceList * diffs = ( *it ) ->differences();
        int cnt = 0;
        if ( diffs )
            cnt = diffs->count();

        KUrl file = urlForFileModel(*it);
        haveUrls.insert(file);

        if(!QFileInfo(file.toLocalFile()).isReadable())
          continue;
          
        QTreeWidgetItem* item = new QTreeWidgetItem(m_editPatch.filesList);
        
        m_editPatch.filesList->insertTopLevelItem(0, item);

        const QString filenameArgument = ICore::self()->projectController()->prettyFileName(file, KDevelop::IProjectController::FormatPlain);

        QString text;
        if(additionalUrls.contains(file) && !additionalUrls[file].isEmpty()) {
            text = i18np("%2 (1 hunk, %3)", "%2 (%1 hunks, %3)", cnt, filenameArgument, additionalUrls[file]);
        } else {
            text = i18np("%2 (1 hunk)", "%2, (%1 hunks)", cnt, filenameArgument);
        }

        item->setData( 0, Qt::DisplayRole, text );
        item->setData( 0, Qt::UserRole, qVariantFromValue<const Diff2::DiffModel*>(*it));
        item->setCheckState( 0, Qt::Checked );
    }
    
    ///First add files that a project was found for, then the other ones
    ///findProject() excludes some useless files like backups, so we can use that to sort those files to the back
    for(int withProject = 1; withProject >= 0; --withProject)
    {
      for(QMap<KUrl, QString>::const_iterator it = additionalUrls.constBegin(); it != additionalUrls.constEnd(); ++it)
      {
        KUrl url = it.key();
        
          if(((bool)ICore::self()->projectController()->findProjectForUrl(url)) != (bool)withProject)
            continue;
        
        if(!haveUrls.contains(url))
        {
          haveUrls.insert(url);
          
          QTreeWidgetItem* item = new QTreeWidgetItem(m_editPatch.filesList);
          
          m_editPatch.filesList->insertTopLevelItem(0, item);
          QString text = ICore::self()->projectController()->prettyFileName(url, KDevelop::IProjectController::FormatPlain);
          if(!(*it).isEmpty())
            text += " (" + (*it) + ")";
          
          item->setData( 0, Qt::DisplayRole, text );
          QVariant v;
          v.setValue<KUrl>( url );
          item->setData( 0, Qt::UserRole, v );
          item->setCheckState( 0, Qt::Unchecked );
        }
      }
    }
}


void PatchReviewToolView::documentActivated(IDocument* doc)
{
    QModelIndexList i = m_editPatch.filesList->selectionModel() ->selectedIndexes();
    if ( !m_plugin->modelList() )
        return ;
    for(int a = 0; a < m_editPatch.filesList->topLevelItemCount(); ++a) {
      
        QTreeWidgetItem* item = m_editPatch.filesList->topLevelItem(a);
      
        QVariant v = item->data( 0, Qt::UserRole );
        if ( v.canConvert<const Diff2::DiffModel*>() ) {
            const Diff2::DiffModel * model = v.value<const Diff2::DiffModel*>();
            
            KUrl file = urlForFileModel(model);
            
            if(file == doc->url()) {
              m_editPatch.filesList->setCurrentItem(item);
              return;
            }
        }
    }
    m_editPatch.filesList->setCurrentIndex(QModelIndex());
}


void PatchHighlighter::rangeDeleted(KTextEditor::SmartRange* range)
{
    m_ranges.remove(range);
    m_differencesForRanges.remove(range);
}

QSize sizeHintForHtml(QString html, QSize maxSize) {
  QTextDocument doc;
  doc.setHtml(html);

  QSize ret;
  if(doc.idealWidth() > maxSize.width()) {
    doc.setPageSize( QSize(maxSize.width(), 30) );
    ret.setWidth(maxSize.width());
  }else{
    ret.setWidth(doc.idealWidth());    
  }
  ret.setHeight(doc.size().height());
  if(ret.height() > maxSize.height())
    ret.setHeight(maxSize.height());
  return ret;
}

void PatchHighlighter::showToolTipForMark(QPoint pos, KTextEditor::SmartRange* markRange, QPair< int, int > highlightMark)
{
  static QPointer<QWidget> currentTooltip;
  static KTextEditor::SmartRange* currentTooltipMark;
  if(currentTooltipMark == markRange && currentTooltip)
    return;
  delete currentTooltip;
  
  //Got the difference
  Diff2::Difference* diff = m_differencesForRanges[markRange];
  
  QString html;
#if 0
  if(diff->hasConflict())
    html += i18n("<b><span style=\"color:red\">Conflict</span></b><br/>");
#endif
  
  Diff2::DifferenceStringList lines;
  
  if(m_plugin->patch()->isAlreadyApplied() && !diff->applied())
    html += i18n("<b>Reverted.</b><br/>");
  else if(!m_plugin->patch()->isAlreadyApplied() && diff->applied())
    html += i18n("<b>Applied.</b><br/>");
  
  if(diff->applied()) {
    if(isInsertion(diff))
    {
      html += i18n("<b>Insertion</b><br/>");
    }else{
      if(isRemoval(diff))
        html += i18n("<b>Removal</b><br/>");
      html += i18n("<b>Previous:</b><br/>");
      lines = diff->sourceLines();
    }
  }else{
    if(isRemoval(diff)) {
      html += i18n("<b>Removal</b><br/>");
    }else{
      if(isInsertion(diff))
        html += i18n("<b>Insertion</b><br/>");
      
      html += i18n("<b>Alternative:</b><br/>");
      
      lines = diff->destinationLines();
    }
  }

  for(int a = 0; a < lines.size(); ++a) {
    Diff2::DifferenceString* line = lines[a];
    uint currentPos = 0;
    QString string = line->string();
    
    Diff2::MarkerList markers = line->markerList();

    for(int b = 0; b < markers.size(); ++b) {
      QString spanText = Qt::escape(string.mid(currentPos, markers[b]->offset() - currentPos));
      if(markers[b]->type() == Diff2::Marker::End && (currentPos != 0 || markers[b]->offset() != string.size()))
      {
        if(a == highlightMark.first && b == highlightMark.second)
          html += "<b><span style=\"background:#FF5555\">" + spanText + "</span></b>";
        else
          html += "<b><span style=\"background:#FFBBBB\">" + spanText + "</span></b>";
      }else{
        html += spanText;
      }
      currentPos = markers[b]->offset();
    }

    html += Qt::escape(string.mid(currentPos, string.length()-currentPos));
    html += "<br/>";
  }

  KTextBrowser* browser = new KTextBrowser;
  browser->setPalette( QApplication::palette() );
  browser->setHtml(html);
  
  int maxHeight = 500;
  
  browser->setMinimumSize(sizeHintForHtml(html, QSize((ICore::self()->uiController()->activeMainWindow()->width()*2)/3, maxHeight)));
  browser->setMaximumSize(browser->minimumSize() + QSize(10, 10));
  if(browser->minimumHeight() != maxHeight)
    browser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  
  QVBoxLayout* layout = new QVBoxLayout;
  layout->setMargin(0);
  layout->addWidget(browser);

  KDevelop::ActiveToolTip* tooltip = new KDevelop::ActiveToolTip(ICore::self()->uiController()->activeMainWindow(), pos + QPoint(5, -browser->sizeHint().height() - 30));
  tooltip->setLayout(layout);
  tooltip->resize( tooltip->sizeHint() + QSize(10, 10) );
  tooltip->move(pos - QPoint(0, 20 + tooltip->height()));
  tooltip->addExtendRect(QRect(pos - QPoint(15, 15), pos + QPoint(15, 15)));
  
  currentTooltip = tooltip;
  currentTooltipMark = markRange;
  
  ActiveToolTip::showToolTip(tooltip);
}

void PatchHighlighter::markClicked(KTextEditor::Document* doc, KTextEditor::Mark mark, bool& handled)
{
  if(handled)
    return;
  
  handled = true;
  
  if(doc->activeView()) ///This is a workaround, if the cursor is somewhere else, the editor will always jump there when a mark was clicked
    doc->activeView()->setCursorPosition(KTextEditor::Cursor(mark.line, 0));

  KTextEditor::SmartRange* range = rangeForMark(mark);
  
  if(range) {
    KTextEditor::SmartInterface* smart = dynamic_cast<KTextEditor::SmartInterface*>( doc );
    Q_ASSERT(smart);
    QMutexLocker lock(smart->smartMutex());
    
    QStringList currentText = range->text();
    Diff2::Difference* diff = m_differencesForRanges[range];
    
    removeLineMarker(range, diff);
    
    QString sourceText;
    QString targetText;
    
    for(int a = 0; a < diff->sourceLineCount(); ++a) {
      sourceText += diff->sourceLineAt(a)->string();
      if(!sourceText.endsWith("\n"))
        sourceText += "\n";
    }
    
    for(int a = 0; a < diff->destinationLineCount(); ++a) {
      targetText += diff->destinationLineAt(a)->string();
      if(!targetText.endsWith("\n"))
        targetText += "\n";
    }
    
    QString replace;
    QString replaceWith;
    
    if(!diff->applied()) {
      replace = sourceText;
      replaceWith = targetText;
    }else {
      replace = targetText;
      replaceWith = sourceText;
    }
    
    if(currentText.join("\n").simplified() != replace.simplified()) {
      KMessageBox::error(ICore::self()->uiController()->activeMainWindow(), i18n("Could not apply the change: Text should be \"%1\", but is \"%2\".", replace, currentText.join("\n")));
      return;
    }
    
    diff->apply(!diff->applied());
    
    KTextEditor::Cursor start = range->start();
    range->document()->replaceText(*range, replaceWith);
    range->start() = start;
    range->end() = start;
    
    uint replaceWithLines = replaceWith.count('\n');
    range->end().setLine(range->end().line() +  replaceWithLines);
    
    addLineMarker(range, diff);
  }
}

KTextEditor::SmartRange* PatchHighlighter::rangeForMark(KTextEditor::Mark mark)
{
    for(QMap< KTextEditor::SmartRange*, Diff2::Difference* >::const_iterator it = m_differencesForRanges.constBegin(); it != m_differencesForRanges.constEnd(); ++it) {
      if(it.key()->start().line() == mark.line)
      {
        return it.key();
      }
    }
    
    return 0;
}

void PatchHighlighter::markToolTipRequested(KTextEditor::Document* , KTextEditor::Mark mark, QPoint pos, bool& handled)
{
  if(handled)
    return;
  
  handled = true;
  
  int myMarksPattern = KTextEditor::MarkInterface::markType22 | KTextEditor::MarkInterface::markType23 | KTextEditor::MarkInterface::markType24 | KTextEditor::MarkInterface::markType25 | KTextEditor::MarkInterface::markType26 | KTextEditor::MarkInterface::markType27;
  if(mark.type & myMarksPattern) {
    //There is a mark in this line. Show the old text.
    KTextEditor::SmartRange* range = rangeForMark(mark);
    if(range)
        showToolTipForMark(pos, range);
  }
}


bool PatchHighlighter::isInsertion(Diff2::Difference* diff)
{
    return diff->sourceLineCount() == 0;
}


bool PatchHighlighter::isRemoval(Diff2::Difference* diff)
{
    return diff->destinationLineCount() == 0;
}

void PatchHighlighter::textInserted(KTextEditor::Document* doc, KTextEditor::Range range)
{
    if(range == doc->documentRange())
    {
      kWarning() << "re-doing";
      //The document was loaded / reloaded
    if ( !m_model->differences() )
        return ;
    KTextEditor::SmartInterface* smart = dynamic_cast<KTextEditor::SmartInterface*>( doc );
    if ( !smart )
        return;

    KTextEditor::MarkInterface* markIface = dynamic_cast<KTextEditor::MarkInterface*>( doc );
    if( !markIface )
      return;
    
    clear();
    
    QColor activeIconColor = QApplication::palette().color(QPalette::Active, QPalette::Highlight);
    QColor inActiveIconColor = QApplication::palette().color(QPalette::Active, QPalette::Base);
    
    KColorScheme scheme(QPalette::Active);
    
    QImage tintedInsertion = KIcon("insert-text").pixmap(16, 16).toImage();
    KIconEffect::colorize(tintedInsertion, scheme.foreground(KColorScheme::NegativeText).color(), 1.0);
    QImage tintedRemoval = KIcon("edit-delete").pixmap(16, 16).toImage();
    KIconEffect::colorize(tintedRemoval, scheme.foreground(KColorScheme::NegativeText).color(), 1.0);
    QImage tintedChange = KIcon("text-field").pixmap(16, 16).toImage();
    KIconEffect::colorize(tintedChange, scheme.foreground(KColorScheme::NegativeText).color(), 1.0);
    
    markIface->setMarkDescription(KTextEditor::MarkInterface::markType22, i18n("Insertion"));
    markIface->setMarkPixmap(KTextEditor::MarkInterface::markType22, QPixmap::fromImage(tintedInsertion));
    markIface->setMarkDescription(KTextEditor::MarkInterface::markType23, i18n("Removal"));
    markIface->setMarkPixmap(KTextEditor::MarkInterface::markType23, QPixmap::fromImage(tintedRemoval));
    markIface->setMarkDescription(KTextEditor::MarkInterface::markType24, i18n("Change"));
    markIface->setMarkPixmap(KTextEditor::MarkInterface::markType24, QPixmap::fromImage(tintedChange));
    
    markIface->setMarkDescription(KTextEditor::MarkInterface::markType25, i18n("Insertion"));
    markIface->setMarkPixmap(KTextEditor::MarkInterface::markType25, KIcon("insert-text").pixmap(16, 16));
    markIface->setMarkDescription(KTextEditor::MarkInterface::markType26, i18n("Removal"));
    markIface->setMarkPixmap(KTextEditor::MarkInterface::markType26, KIcon("edit-delete").pixmap(16, 16));
    markIface->setMarkDescription(KTextEditor::MarkInterface::markType27, i18n("Change"));
    markIface->setMarkPixmap(KTextEditor::MarkInterface::markType27, KIcon("text-field").pixmap(16, 16));

    QMutexLocker lock(smart->smartMutex());

    KTextEditor::SmartRange* topRange = smart->newSmartRange(doc->documentRange(), 0, KTextEditor::SmartRange::ExpandLeft | KTextEditor::SmartRange::ExpandRight);

    topRange->addWatcher(this);
    
    for ( Diff2::DifferenceList::const_iterator it = m_model->differences() ->constBegin(); it != m_model->differences() ->constEnd(); ++it ) {
        Diff2::Difference* diff = *it;
        int line, lineCount;
        Diff2::DifferenceStringList lines ;
        
        if(diff->applied()) {
          line = diff->destinationLineNumber();
          lineCount = diff->destinationLineCount();
          lines = diff->destinationLines();
        } else {
          line = diff->sourceLineNumber();
          lineCount = diff->sourceLineCount();
          lines = diff->sourceLines();
        }

        if ( line > 0 )
            line -= 1;
        
        KTextEditor::Cursor c( line, 0 );
        KTextEditor::Cursor endC( line + lineCount, 0 );
        if ( doc->lines() <= c.line() )
            c.setLine( doc->lines() - 1 );
        if ( doc->lines() <= endC.line() )
            endC.setLine( doc->lines() );

        if ( endC.isValid() && c.isValid() ) {
            KTextEditor::SmartRange * r = smart->newSmartRange( c, endC );
            r->setParentRange(topRange);
            r->addWatcher(this);
            
            m_differencesForRanges[r] = *it;

            addLineMarker(r, diff);
        }
    }

    m_ranges << topRange;

    smart->addHighlightToDocument(topRange);
    }
}

PatchHighlighter::PatchHighlighter( const Diff2::DiffModel* model, IDocument* kdoc, PatchReviewPlugin* plugin ) throw( QString )
  : m_doc( kdoc ), m_plugin(plugin), m_model(model)
{
//     connect( kdoc, SIGNAL( destroyed( QObject* ) ), this, SLOT( documentDestroyed() ) );
    connect( kdoc->textDocument(), SIGNAL(textInserted(KTextEditor::Document*,KTextEditor::Range)), this, SLOT(textInserted(KTextEditor::Document*,KTextEditor::Range)) );
    connect( kdoc->textDocument(), SIGNAL( destroyed( QObject* ) ), this, SLOT( documentDestroyed() ) );

    KTextEditor::Document* doc = kdoc->textDocument();
    if ( doc->lines() == 0 )
        return ;

    ///This requires the KDE 4.4 development branch from 12.8.2009
    connect(doc, SIGNAL(markToolTipRequested(KTextEditor::Document*,KTextEditor::Mark,QPoint,bool&)), this, SLOT(markToolTipRequested(KTextEditor::Document*,KTextEditor::Mark,QPoint,bool&)));
    connect(doc, SIGNAL(markClicked(KTextEditor::Document*,KTextEditor::Mark,bool&)), this, SLOT(markClicked(KTextEditor::Document*,KTextEditor::Mark,bool&)));
    
    textInserted(kdoc->textDocument(), kdoc->textDocument()->documentRange());
}

void PatchHighlighter::removeLineMarker(KTextEditor::SmartRange* range, Diff2::Difference* difference)
{
    KTextEditor::SmartInterface* smart = dynamic_cast<KTextEditor::SmartInterface*>( range->document() );
    if ( !smart )
        return;

    KTextEditor::MarkInterface* markIface = dynamic_cast<KTextEditor::MarkInterface*>( range->document() );
    if( !markIface )
      return;
    
    markIface->removeMark(range->start().line(), KTextEditor::MarkInterface::markType22);
    markIface->removeMark(range->start().line(), KTextEditor::MarkInterface::markType23);
    markIface->removeMark(range->start().line(), KTextEditor::MarkInterface::markType24);
    markIface->removeMark(range->start().line(), KTextEditor::MarkInterface::markType25);
    markIface->removeMark(range->start().line(), KTextEditor::MarkInterface::markType26);
    markIface->removeMark(range->start().line(), KTextEditor::MarkInterface::markType27);
}

void PatchHighlighter::addLineMarker(KTextEditor::SmartRange* range, Diff2::Difference* diff)
{
    KTextEditor::SmartInterface* smart = dynamic_cast<KTextEditor::SmartInterface*>( range->document() );
    if ( !smart )
        return;

    KTextEditor::MarkInterface* markIface = dynamic_cast<KTextEditor::MarkInterface*>( range->document() );
    if( !markIface )
      return;
    
    QMutexLocker lock(smart->smartMutex());
  
    KSharedPtr<KTextEditor::Attribute> t( new KTextEditor::Attribute() );
    
    bool isOriginalState = diff->applied() == m_plugin->patch()->isAlreadyApplied();
    
    if(isOriginalState) {
      t->setProperty( QTextFormat::BackgroundBrush, QBrush( QColor( 0, 255, 255, 20 ) ) );
    }else{
      t->setProperty( QTextFormat::BackgroundBrush, QBrush( QColor( 255, 0, 255, 20 ) ) );
    }
    range->setAttribute( t );
    
    range->deleteChildRanges();
    
    KTextEditor::MarkInterface::MarkTypes mark;
    
    if(isOriginalState) {
      mark = KTextEditor::MarkInterface::markType27;
      
      if(isInsertion(diff))
        mark = KTextEditor::MarkInterface::markType25;
      if(isRemoval(diff))
        mark = KTextEditor::MarkInterface::markType26;
    }else{
      mark = KTextEditor::MarkInterface::markType24;
      
      if(isInsertion(diff))
        mark = KTextEditor::MarkInterface::markType22;
      if(isRemoval(diff))
        mark = KTextEditor::MarkInterface::markType23;
    }
    
    markIface->addMark(range->start().line(), mark);
    
    
    Diff2::DifferenceStringList lines;
    if(diff->applied())
      lines = diff->destinationLines();
    else
      lines = diff->sourceLines();
    
    for(int a = 0; a < lines.size(); ++a) {
      Diff2::DifferenceString* line = lines[a];
      int currentPos = 0;
      QString string = line->string();
      
      Diff2::MarkerList markers = line->markerList();

      for(int b = 0; b < markers.size(); ++b) { 
        if(markers[b]->type() == Diff2::Marker::End)
        {
          if(currentPos != 0 || markers[b]->offset() != string.size())
          {
            KTextEditor::SmartRange * r2 = smart->newSmartRange( KTextEditor::Cursor(a + range->start().line(), currentPos), KTextEditor::Cursor(a + range->start().line(), markers[b]->offset()), range );
            
            KSharedPtr<KTextEditor::Attribute> t( new KTextEditor::Attribute() );

            t->setProperty( QTextFormat::BackgroundBrush, QBrush( QColor( 255, 0, 0, 70 ) ) );
            r2->setAttribute( t );
          }
        }
        currentPos = markers[b]->offset();
      }
    }
}

void PatchHighlighter::clear()
{
    KTextEditor::SmartInterface* smart = dynamic_cast<KTextEditor::SmartInterface*>( m_doc->textDocument() );
    if ( !smart )
        return;

    KTextEditor::MarkInterface* markIface = dynamic_cast<KTextEditor::MarkInterface*>( m_doc->textDocument() );
    if( !markIface )
      return;

    QHash< int, KTextEditor::Mark* > marks = markIface->marks();
    foreach(int line, marks.keys()) {
      markIface->removeMark(line, KTextEditor::MarkInterface::markType22);
      markIface->removeMark(line, KTextEditor::MarkInterface::markType23);
      markIface->removeMark(line, KTextEditor::MarkInterface::markType24);
      markIface->removeMark(line, KTextEditor::MarkInterface::markType25);
      markIface->removeMark(line, KTextEditor::MarkInterface::markType26);
      markIface->removeMark(line, KTextEditor::MarkInterface::markType27);
    }
    
    QMutexLocker lock(smart->smartMutex());

    while(!m_ranges.isEmpty())
      delete *m_ranges.begin();

    Q_ASSERT(m_ranges.isEmpty());
    Q_ASSERT(m_differencesForRanges.isEmpty());
}

PatchHighlighter::~PatchHighlighter()
{
    clear();
}

IDocument* PatchHighlighter::doc() {
    return m_doc;
}

void PatchHighlighter::documentDestroyed() {
    m_ranges.clear();
}

void PatchReviewPlugin::removeHighlighting( const KUrl& file ) {
    if ( file.isEmpty() ) {
        ///Remove all highlighting
        qDeleteAll(m_highlighters);
        m_highlighters.clear();
    } else {
        HighlightMap::iterator it = m_highlighters.find( file );
        if ( it != m_highlighters.end() ) {
            delete * it;
            m_highlighters.erase( it );
        }
    }
}

void PatchReviewPlugin::notifyPatchChanged()
{
    kDebug() << "notifying patch change: " << m_patch->file();
    m_updateKompareTimer->start(500);
}

void PatchReviewPlugin::showPatch()
{
    startReview(m_patch, OpenAndRaise);
}

void PatchReviewPlugin::forceUpdate()
{
  m_patch->update();
  
  notifyPatchChanged();
}

void PatchReviewPlugin::updateKompareModel() {
  
    kDebug() << "updating model";
    try {
        m_modelList.reset( 0 );
        delete m_diffSettings;
        removeHighlighting();
        
        emit patchChanged();

        if ( m_patch->file().isEmpty() )
            return;
        
        qRegisterMetaType<const Diff2::DiffModel*>( "const Diff2::DiffModel*" );
        m_diffSettings = new DiffSettings( 0 );
        m_kompareInfo.reset( new Kompare::Info() );
        m_kompareInfo->localDestination=m_patch->file().toLocalFile();
        m_kompareInfo->localSource=m_patch->baseDir().toLocalFile();
        
        m_modelList.reset(new Diff2::KompareModelList( m_diffSettings.data(), new QWidget, this ));
        m_modelList->slotKompareInfo(m_kompareInfo.get());
        
        try {
            if ( !m_modelList->openDirAndDiff() )
                throw "could not open diff " + m_patch->file().prettyUrl() + " on " + m_patch->baseDir().prettyUrl();
         } catch ( const QString & str ) {
            throw;
        } catch ( ... ) {
            throw QString( "lib/libdiff2 crashed, memory may be corrupted. Please restart kdevelop." );
        }

        emit patchChanged();
        
        for(int i=0; i<m_modelList->modelCount(); i++) {
            const Diff2::DiffModel* model=m_modelList->modelAt(i);
            for(int j=0; j<model->differences()->count(); j++) {
                model->differences()->at(j)->apply(m_patch->isAlreadyApplied());
            }
        }

        highlightPatch();

        return;
    } catch ( const QString & str ) {
        KMessageBox::error(0, str, "Kompare Model Update");
    } catch ( const char * str ) {
        KMessageBox::error(0, str, "Kompare Model Update");
    }
    m_modelList.reset( 0 );
    m_kompareInfo.reset( 0 );
    delete m_diffSettings;
    
    emit patchChanged();
}

K_PLUGIN_FACTORY(KDevProblemReporterFactory, registerPlugin<PatchReviewPlugin>(); )
K_EXPORT_PLUGIN(KDevProblemReporterFactory(KAboutData("kdevpatchreview","kdevpatchreview", ki18n("Patch Review"), "0.1", ki18n("Highlights code affected by a patch"), KAboutData::License_GPL)))

class PatchReviewToolViewFactory : public KDevelop::IToolViewFactory
{
public:
    PatchReviewToolViewFactory(PatchReviewPlugin *plugin): m_plugin(plugin) {}

    virtual QWidget* create(QWidget *parent = 0)
    {
        return m_plugin->createToolView(parent);
    }

    virtual Qt::DockWidgetArea defaultPosition()
    {
        return Qt::BottomDockWidgetArea;
    }

    virtual QString id() const
    {
        return "org.kdevelop.PatchReview";
    }

private:
    PatchReviewPlugin *m_plugin;
};

PatchReviewPlugin::~PatchReviewPlugin()
{
    removeHighlighting();
    delete m_patch;
}

void PatchReviewPlugin::registerPatch(IPatchSource::Ptr patch)
{
  if(!m_knownPatches.contains(patch)) {
    m_knownPatches << patch;
    connect(patch, SIGNAL(destroyed(QObject*)), SLOT(clearPatch(QObject*)));
  }
}

void PatchReviewPlugin::clearPatch(QObject* _patch)
{
  kDebug() << "clearing patch" << _patch << "current:" << (QObject*)m_patch;
  IPatchSource::Ptr patch((IPatchSource*)_patch);
  m_knownPatches.removeAll(patch);
  m_knownPatches.removeAll(0);
  
  if(patch == m_patch) {
    kDebug() << "is current patch";
    if(!m_knownPatches.empty())
      setPatch(m_knownPatches.first());
    else
      setPatch(IPatchSource::Ptr(new LocalPatchSource));
  }
}

#if 0
#if HAVE_KOMPARE
void showDiff(const KDevelop::VcsDiff& d)
{
    ICore::self()->uiController()->switchToArea("review", KDevelop::IUiController::ThisWindow);
    foreach(const VcsLocation& l, d.leftTexts().keys())
    {
        KUrl to;
        if(d.rightTexts().contains(l))
        {
            KTemporaryFile temp2;
            temp2.setSuffix("2.patch");
            temp2.setAutoRemove(false);
            temp2.open();
            QTextStream t2(&temp2);
            t2 << d.rightTexts()[l];
            temp2.close();
            to=temp2.fileName();
        }
        else
            to=l.localUrl();
        
        KUrl fakeUrl(to);
        fakeUrl.setScheme("kdevpatch");
        
        IDocumentFactory* docf=ICore::self()->documentController()->factory("text/x-patch");
        IDocument* doc=docf->create(fakeUrl, ICore::self());
        IPatchDocument* pdoc=dynamic_cast<IPatchDocument*>(doc);
        
        Q_ASSERT(pdoc);
        ICore::self()->documentController()->openDocument(doc);
        pdoc->setDiff(d.leftTexts()[l], to);
    }
}
#endif
#endif

void PatchReviewPlugin::cancelReview()
{
  if(m_patch) {
    m_modelList.reset( 0 );
    m_patch->cancelReview();

    emit patchChanged();
    
    delete m_patch;
    
    Sublime::MainWindow* w = dynamic_cast<Sublime::MainWindow*>(ICore::self()->uiController()->activeMainWindow());
    if(w->area()->workingSet().startsWith("review")) {
      w->area()->clearViews();
      ICore::self()->uiController()->switchToArea("code", KDevelop::IUiController::ThisWindow);
    }
  }
}

void PatchReviewPlugin::finishReview(QList< KUrl > selection)
{
  if(m_patch) {
    if(!m_patch->finishReview(selection))
      return;
    m_modelList.reset( 0 );
    
    emit patchChanged();
    
    if(!dynamic_cast<LocalPatchSource*>(m_patch))
      delete m_patch;
    
    Sublime::MainWindow* w = dynamic_cast<Sublime::MainWindow*>(ICore::self()->uiController()->activeMainWindow());
    if(w->area()->workingSet().startsWith("review")) {
      w->area()->clearViews();
      ICore::self()->uiController()->switchToArea("code", KDevelop::IUiController::ThisWindow);
    }
  }
}

void PatchReviewPlugin::startReview(IPatchSource* patch, IPatchReview::ReviewMode mode)
{
  Q_UNUSED(mode);
  setPatch(patch);
  QMetaObject::invokeMethod(this, "updateReview", Qt::QueuedConnection);
}

void PatchReviewPlugin::updateReview()
{
  if(!m_patch)
    return;
  
  m_updateKompareTimer->stop();
  updateKompareModel();
  ICore::self()->uiController()->switchToArea("review", KDevelop::IUiController::ThisWindow);
  Sublime::MainWindow* w = dynamic_cast<Sublime::MainWindow*>(ICore::self()->uiController()->activeMainWindow());
  if(!w->area()->workingSet().startsWith("review"))
    w->area()->setWorkingSet("review");
  
  w->area()->clearViews();
  
  if(!m_modelList.get())
    return;
  
  //Open the diff itself
#ifdef HAVE_KOMPARE
  KUrl fakeUrl(m_patch->file());
  fakeUrl.setScheme("kdevpatch");
  IDocumentFactory* docf=ICore::self()->documentController()->factory("text/x-patch");
  IDocument* doc=docf->create(fakeUrl, ICore::self());
  IPatchDocument* pdoc=dynamic_cast<IPatchDocument*>(doc);
  
  Q_ASSERT(pdoc);
  ICore::self()->documentController()->openDocument(doc);
#else
  ICore::self()->documentController()->openDocument(m_patch->file());
#endif

  if(m_modelList->modelCount() < maximumFilesToOpenDirectly) {
    //Open all relates files
    for(int a = 0; a < m_modelList->modelCount(); ++a) {
      KUrl url(m_modelList->modelAt(a)->source());
       ICore::self()->documentController()->openDocument(url);
       seekHunk(true, url); //Jump to the first changed position
    }
  }
  
  bool b = ICore::self()->uiController()->findToolView(i18n("Patch Review"), m_factory);
  Q_ASSERT(b);
}

void PatchReviewPlugin::setPatch(IPatchSource* patch)
{
  if(m_patch) {
    QObject* objPatch = dynamic_cast<QObject*>(m_patch);
    if(objPatch) {
      disconnect(objPatch, SIGNAL(patchChanged()), this, SLOT(notifyPatchChanged()));
    }
  }
  m_patch = patch;

  if(m_patch) {
    kDebug() << "setting new patch" << patch->name() << "with file" << patch->file();
    registerPatch(patch);
    
    QObject* objPatch = dynamic_cast<QObject*>(m_patch);
    if(objPatch) {
      connect(objPatch, SIGNAL(patchChanged()), this, SLOT(notifyPatchChanged()));
    }
  }
  
  notifyPatchChanged();
}

PatchReviewPlugin::PatchReviewPlugin(QObject *parent, const QVariantList &)
: KDevelop::IPlugin(KDevProblemReporterFactory::componentData(), parent), 
  m_patch(0), m_factory(new PatchReviewToolViewFactory(this))
{
    KDEV_USE_EXTENSION_INTERFACE( KDevelop::IPatchReview )
  
    core()->uiController()->addToolView(i18n("Patch Review"), m_factory);
    setXMLFile("kdevpatchreview.rc");

    connect(ICore::self()->documentController(), SIGNAL(documentClosed(KDevelop::IDocument*)), this, SLOT(documentClosed(KDevelop::IDocument*)));
    connect(ICore::self()->documentController(), SIGNAL(textDocumentCreated(KDevelop::IDocument*)), this, SLOT(textDocumentCreated(KDevelop::IDocument*)));

    m_updateKompareTimer = new QTimer( this );
    m_updateKompareTimer->setSingleShot( true );
    connect( m_updateKompareTimer, SIGNAL( timeout() ), this, SLOT( updateKompareModel() ) );
    
    setPatch(IPatchSource::Ptr(new LocalPatchSource));
}

void PatchReviewPlugin::documentClosed(IDocument* doc)
{
    removeHighlighting(doc->url());
}

void PatchReviewPlugin::textDocumentCreated(IDocument* doc)
{
    addHighlighting( doc->url(), doc );
}

void PatchReviewPlugin::unload()
{
    core()->uiController()->removeToolView(m_factory);

    KDevelop::IPlugin::unload();
}

QWidget* PatchReviewPlugin::createToolView(QWidget* parent)
{
    return new PatchReviewToolView(parent, this);
}

#if 0
void PatchReviewPlugin::determineState() {
    LocalPatchSourcePointer lpatch = m_patch;
    if ( !lpatch ) {
        kDebug() <<"determineState(..) could not lock patch";
    }
    try {
        if ( lpatch->filename.isEmpty() )
            throw "state can only be determined for file-patches";

        KUrl fileUrl = lpatch->filename;

        {
            K3Process proc;                                                                                                        
            ///Try to apply, if it works, the patch is not applied                                                                 
            QString cmd =  "patch --dry-run -s -f -i " + fileUrl.toLocalFile();                                                    
            proc << splitArgs( cmd );                                                                                              
                                                                                                                                   
            kDebug() << "calling " << cmd;                                                                                         
                                                                                                                                   
            if ( !proc.start( K3Process::Block ) )                                                                                 
                throw "could not start process";                                                                                   
                                                                                                                                   
            if ( !proc.normalExit() )                                                                                              
                throw "process did not exit normally";                                                                             
                                                                                                                                   
            kDebug() << "exit-status:" << proc.exitStatus();                                                                       

            if ( proc.exitStatus() == 0 ) {
//                 lpatch->state = LocalPatchSource::NotApplied;
                return;
            }
        }

        {
            ///Try to revert, of it works, the patch is applied
            K3Process proc;
            QString cmd =  "patch --dry-run -s -f -i --reverse " + fileUrl.toLocalFile();
            proc << splitArgs( cmd );

            kDebug() << "calling " << cmd;

            if ( !proc.start( K3Process::Block ) )
                throw "could not start process";

            if ( !proc.normalExit() )
                throw "process did not exit normally";

            kDebug() << "exit-status:" << proc.exitStatus();

            if ( proc.exitStatus() == 0 ) {
//                 lpatch->state = LocalPatchSource::Applied;
                return;
            }
        }
    } catch ( const QString& str ) {
        kWarning() <<"Error:" << str;
    } catch ( const char* str ) {
        kWarning() << "Error:" << str;
    }

//     lpatch->state = LocalPatchSource::Unknown;
}
#endif
#include "patchreview.moc"

// kate: space-indent on; indent-width 2; tab-width 2; replace-tabs on
