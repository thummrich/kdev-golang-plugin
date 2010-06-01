/* This file is part of KDevelop
Copyright 2009 Andreas Pakulat <apaku@gmx.de>
Copyright (C) 2008 Cédric Pasteur <cedric.pasteur@free.fr>

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

#include "sourceformattercontroller.h"

#include <QVariant>
#include <QStringList>
#include <QRegExp>
#include <KPluginInfo>
#include <KDebug>

#include <interfaces/icore.h>
#include <interfaces/iplugincontroller.h>
#include <interfaces/ilanguagecontroller.h>
#include <language/interfaces/ilanguagesupport.h>
#include <interfaces/isourceformatter.h>
#include "core.h"
#include <ktexteditor/view.h>
#include <project/projectmodel.h>
#include <kio/netaccess.h>
#include <kmessagebox.h>
#include <qfile.h>
#include <interfaces/context.h>
#include <interfaces/contextmenuextension.h>
#include <kactioncollection.h>
#include <kaction.h>
#include <interfaces/idocument.h>
#include <interfaces/idocumentcontroller.h>
#include <ktexteditor/document.h>
#include "plugincontroller.h"
#include <interfaces/isession.h>

namespace KDevelop
{

const QString SourceFormatterController::kateModeLineConfigKey = "ModelinesEnabled";
const QString SourceFormatterController::styleCaptionKey = "Caption";
const QString SourceFormatterController::styleContentKey = "Content";
const QString SourceFormatterController::supportedMimeTypesKey = "X-KDevelop-SupportedMimeTypes";

SourceFormatterController::SourceFormatterController(QObject *parent)
		: ISourceFormatterController(parent)
{
	setObjectName("SourceFormatterController");
	setComponentData(KComponentData("kdevsourceformatter"));
	setXMLFile("kdevsourceformatter.rc");

	if (Core::self()->setupFlags() & Core::NoUi) return;

	m_formatTextAction = actionCollection()->addAction("edit_reformat_source");
	m_formatTextAction->setText(i18n("&Reformat Source"));
	m_formatTextAction->setToolTip(i18n("Reformat source using AStyle"));
	m_formatTextAction->setWhatsThis(i18n("<b>Reformat source</b><p>Source reformatting "
	        "functionality using <b>astyle</b> library.</p>"));
	connect(m_formatTextAction, SIGNAL(triggered()), this, SLOT(beautifySource()));

	m_formatFilesAction = actionCollection()->addAction("tools_astyle");
	m_formatFilesAction->setText(i18n("Format files"));
	m_formatFilesAction->setToolTip(i18n("Format file(s) using the current theme"));
	m_formatFilesAction->setWhatsThis(i18n("<b>Format files</b><p>Formatting functionality using <b>astyle</b> library.</p>"));
	connect(m_formatFilesAction, SIGNAL(triggered()), this, SLOT(formatFiles()));

	m_formatTextAction->setEnabled(false);
	m_formatFilesAction->setEnabled(true);

	connect(Core::self()->documentController(), SIGNAL(documentActivated(KDevelop::IDocument*)),
	        this, SLOT(activeDocumentChanged(KDevelop::IDocument*)));

	activeDocumentChanged(Core::self()->documentController()->activeDocument());
}

void SourceFormatterController::initialize()
{
}

SourceFormatterController::~SourceFormatterController()
{
}

ISourceFormatter* SourceFormatterController::formatterForUrl(const KUrl &url)
{
	KMimeType::Ptr mime = KMimeType::findByUrl(url);
	return formatterForMimeType(mime);
}
KConfigGroup SourceFormatterController::configuration()
{
	return Core::self()->activeSession()->config()->group( "SourceFormatter" );
}

static ISourceFormatter* findFirstFormatterForMimeType( const KMimeType::Ptr& mime )
{
	static QHash<QString, ISourceFormatter*> knownFormatters;
	if (knownFormatters.contains(mime->name()))
		return knownFormatters[mime->name()];
	
	foreach( IPlugin* p, Core::self()->pluginController()->allPluginsForExtension( "org.kdevelop.ISourceFormatter" ) ) {
		KPluginInfo info = Core::self()->pluginController()->pluginInfo( p );
		if( info.property( SourceFormatterController::supportedMimeTypesKey ).toStringList().contains( mime->name() ) ) {
			ISourceFormatter *formatter = p->extension<ISourceFormatter>();
			knownFormatters[mime->name()] = formatter;
			return formatter;
		}
	}
	knownFormatters[mime->name()] = 0;
	return 0;
}

ISourceFormatter* SourceFormatterController::formatterForMimeType(const KMimeType::Ptr &mime)
{
	if( !isMimeTypeSupported( mime ) ) {
		return 0;
	}
	QString formatter = configuration().readEntry( mime->name(), "" );

	if( formatter.isEmpty() )
	{
		return findFirstFormatterForMimeType( mime );
	}

	QStringList formatterinfo = formatter.split( "||", QString::SkipEmptyParts );

	if( formatterinfo.size() != 2 ) {
		kDebug() << "Broken formatting entry for mime:" << mime << "current value:" << formatter;
		return 0;
	}

	return Core::self()->pluginControllerInternal()->extensionForPlugin<ISourceFormatter>( "org.kdevelop.ISourceFormatter", formatterinfo.at(0) );
}

bool SourceFormatterController::isMimeTypeSupported(const KMimeType::Ptr &mime)
{
	if( findFirstFormatterForMimeType( mime ) ) {
		return true;
	}
	return false;
}

QString SourceFormatterController::indentationMode(const KMimeType::Ptr &mime)
{
	if (mime->is("text/x-c++src") || mime->is("text/x-chdr") ||
	        mime->is("text/x-c++hdr") || mime->is("text/x-csrc") ||
	        mime->is("text/x-java") || mime->is("text/x-csharp"))
		return "cstyle";
	return "none";
}

QString SourceFormatterController::addModelineForCurrentLang(QString input, const KMimeType::Ptr& mime)
{
	if( !isMimeTypeSupported(mime) )
	{
		return input;
	}
	if( !configuration().readEntry( SourceFormatterController::kateModeLineConfigKey, false ) )
		return input;

	QString output;
	QTextStream os(&output, QIODevice::WriteOnly);
	QTextStream is(&input, QIODevice::ReadOnly);

	ISourceFormatter* fmt = formatterForMimeType( mime );
	Q_ASSERT(fmt);

    QString modeline("// kate: ");
	QString length = QString::number(fmt->indentationLength());
	// add indentation style
	modeline.append("indent-mode ").append(indentationMode(mime).append("; "));

	ISourceFormatter::IndentationType type = fmt->indentationType();
	if (type == ISourceFormatter::IndentWithTabs) {
		modeline.append("replace-tabs off; ");
		modeline.append("tab-width ").append(length).append("; ");
	} else {
		modeline.append("space-indent on; ");
		modeline.append("indent-width ").append(length).append("; ");
		if (type == ISourceFormatter::IndentWithSpacesAndConvertTabs)
			modeline.append("replace-tabs on; ");
	}

	kDebug() << "created modeline: " << modeline << endl;

	bool modelinefound = false;
	QRegExp kateModeline("^\\s*//\\s*kate:(.*)$");
	QRegExp knownOptions("\\s*(indent-width|space-indent|tab-width|indent-mode)");
	while (!is.atEnd()) {
		QString line = is.readLine();
		// replace only the options we care about
		if (kateModeline.indexIn(line) >= 0) { // match
			kDebug() << "Found a kate modeline: " << line << endl;
			modelinefound = true;
			QString options = kateModeline.cap(1);
			QStringList optionList = options.split(';', QString::SkipEmptyParts);

			os <<  modeline;
			foreach(const QString &s, optionList) {
				if (knownOptions.indexIn(s) < 0) { // unknown option, add it
					os << s << ";";
					kDebug() << "Found unknown option: " << s << endl;
				}
			}
			os << endl;
		} else
			os << line << endl;
	}

	if (!modelinefound)
		os << modeline << endl;
	return output;
}

void SourceFormatterController::cleanup()
{
}


void SourceFormatterController::activeDocumentChanged(IDocument* doc)
{
	bool enabled = false;

	if (doc) {
		KMimeType::Ptr mime = KMimeType::findByUrl(doc->url());
		if (isMimeTypeSupported(mime))
			enabled = true;
	}

	m_formatTextAction->setEnabled(enabled);
}

void SourceFormatterController::beautifySource()
{
	KDevelop::IDocumentController *docController = KDevelop::ICore::self()->documentController();
	KDevelop::IDocument *doc = docController->activeDocument();
	if (!doc)
		return;
	// load the appropriate formatter
	KMimeType::Ptr mime = KMimeType::findByUrl(doc->url());
	ISourceFormatter *formatter = formatterForMimeType(mime);
        if( !formatter ) {
            kDebug() << "no formatter available for" << mime;
            return;
        }

	bool has_selection = false;
	KTextEditor::View *view = doc->textDocument()->views().first();
	if (view && view->selection())
		has_selection = true;

	if (has_selection) {
		QString original = view->selectionText();

		QString output = formatter->formatSource(view->selectionText(), mime,
												  view->document()->text(KTextEditor::Range(KTextEditor::Cursor(0,0),view->selectionRange().start())),
												  view->document()->text(KTextEditor::Range(view->selectionRange().end(), view->document()->documentRange().end())));

		//remove the final newline character, unless it should be there
		if (!original.endsWith('\n')  && output.endsWith('\n'))
			output.resize(output.length() - 1);
		//there was a selection, so only change the part of the text related to it
		doc->textDocument()->replaceText(view->selectionRange(), output);
	} else {
		formatDocument(doc, formatter, mime);
        }
}

void SourceFormatterController::formatDocument(KDevelop::IDocument *doc, ISourceFormatter *formatter, const KMimeType::Ptr &mime)
{
	KTextEditor::Document *textDoc = doc->textDocument();

	KTextEditor::Cursor cursor = doc->cursorPosition();
	QString text = formatter->formatSource(textDoc->text(), mime);
	text = addModelineForCurrentLang(text, mime);
	textDoc->setText(text);
	doc->setCursorPosition(cursor);
}

void SourceFormatterController::formatFiles()
{
	if (m_prjItems.isEmpty())
		return;

	//get a list of all files in this folder recursively
	QList<KDevelop::ProjectFolderItem*> folders;
	foreach(KDevelop::ProjectBaseItem *item, m_prjItems) {
		if (!item)
			continue;
		if (item->folder())
			folders.append(item->folder());
		else if (item->file())
			m_urls.append(item->file()->url());
		else if (item->target()) {
			foreach(KDevelop::ProjectFileItem *f, item->fileList())
			m_urls.append(f->url());
		}
	}

	while (!folders.isEmpty()) {
		KDevelop::ProjectFolderItem *item = folders.takeFirst();
		foreach(KDevelop::ProjectFolderItem *f, item->folderList())
		folders.append(f);
		foreach(KDevelop::ProjectTargetItem *f, item->targetList()) {
			foreach(KDevelop::ProjectFileItem *child, f->fileList())
			m_urls.append(child->url());
		}
		foreach(KDevelop::ProjectFileItem *f, item->fileList())
		m_urls.append(f->url());
	}

	formatFiles(m_urls);
}

void SourceFormatterController::formatFiles(KUrl::List &list)
{
	//! \todo IStatus
	for (int fileCount = 0; fileCount < list.size(); fileCount++) {
		// check mimetype
		KMimeType::Ptr mime = KMimeType::findByUrl(list[fileCount]);
		kDebug() << "Checking file " << list[fileCount].pathOrUrl() << " of mime type " << mime->name() << endl;
		ISourceFormatter *formatter = formatterForMimeType(mime);
		if (!formatter) // unsupported mime type
			continue;

		// if the file is opened in the editor, format the text in the editor without saving it
		KDevelop::IDocumentController *docController = KDevelop::ICore::self()->documentController();
		KDevelop::IDocument *doc = docController->documentForUrl(list[fileCount]);
		if (doc) {
			kDebug() << "Processing file " << list[fileCount].pathOrUrl() << "opened in editor" << endl;
			formatDocument(doc, formatter, mime);
			return;
		}

		kDebug() << "Processing file " << list[fileCount].pathOrUrl() << endl;
		QString tmpFile, output;
		if (KIO::NetAccess::download(list[fileCount], tmpFile, 0)) {
			QFile file(tmpFile);
			// read file
			if (file.open(QFile::ReadOnly)) {
				QTextStream is(&file);
				output = formatter->formatSource(is.readAll(), mime);
				file.close();
			} else
				KMessageBox::error(0, i18n("Unable to read %1", list[fileCount].prettyUrl()));

			//write new content
			if (file.open(QFile::WriteOnly | QIODevice::Truncate)) {
				QTextStream os(&file);
				os << addModelineForCurrentLang(output, mime);
				file.close();
			} else
				KMessageBox::error(0, i18n("Unable to write to %1", list[fileCount].prettyUrl()));

			if (!KIO::NetAccess::upload(tmpFile, list[fileCount], 0))
				KMessageBox::error(0, KIO::NetAccess::lastErrorString());

			KIO::NetAccess::removeTempFile(tmpFile);
		} else
			KMessageBox::error(0, KIO::NetAccess::lastErrorString());
	}
}

KDevelop::ContextMenuExtension SourceFormatterController::contextMenuExtension(KDevelop::Context* context)
{
	KDevelop::ContextMenuExtension ext;
	m_urls.clear();
	m_prjItems.clear();

	if (context->hasType(KDevelop::Context::EditorContext))
	{
		if(m_formatTextAction->isEnabled())
			ext.addAction(KDevelop::ContextMenuExtension::EditGroup, m_formatTextAction);
	} else if (context->hasType(KDevelop::Context::FileContext)) {
		KDevelop::FileContext* filectx = dynamic_cast<KDevelop::FileContext*>(context);
		m_urls = filectx->urls();
		ext.addAction(KDevelop::ContextMenuExtension::EditGroup, m_formatFilesAction);
	} else if (context->hasType(KDevelop::Context::CodeContext)) {
	} else if (context->hasType(KDevelop::Context::ProjectItemContext)) {
		KDevelop::ProjectItemContext* prjctx = dynamic_cast<KDevelop::ProjectItemContext*>(context);
		m_prjItems = prjctx->items();
		if ( !m_prjItems.isEmpty() ) {
			ext.addAction(KDevelop::ContextMenuExtension::ExtensionGroup, m_formatFilesAction);
		}
	}
	return ext;
}

SourceFormatterStyle SourceFormatterController::styleForMimeType( const KMimeType::Ptr& mime )
{
	QStringList formatter = configuration().readEntry( mime->name(), "" ).split( "||", QString::SkipEmptyParts );
	if( formatter.count() == 2 )
	{
		SourceFormatterStyle s( formatter.at( 0 ) );
		KConfigGroup fmtgrp = configuration().group( formatter.at(0) );
		if( fmtgrp.hasGroup( formatter.at(1) ) ) {
			KConfigGroup stylegrp = fmtgrp.group( formatter.at(1) );
			s.setCaption( stylegrp.readEntry( styleCaptionKey, "" ) );
			s.setContent( stylegrp.readEntry( styleContentKey, "" ) );
		}
		return s;
	}
	return SourceFormatterStyle();
}

/*
 Code copied from source formatter plugin, unused currently but shouldn't be just thrown away
QString SourceFormatterPlugin::replaceSpacesWithTab(const QString &input, ISourceFormatter *formatter)
{
	QString output(input);
	int wsCount = formatter->indentationLength();
	ISourceFormatter::IndentationType type = formatter->indentationType();

	if (type == ISourceFormatter::IndentWithTabs) {
		// tabs and wsCount spaces to be a tab
		QString replace;
		for (int i = 0; i < wsCount;i++)
			replace += ' ';

		output = output.replace(replace, QChar('\t'));
// 		input = input.remove(' ');
	} else if (type == ISourceFormatter::IndentWithSpacesAndConvertTabs) {
		//convert tabs to spaces
		QString replace;
		for (int i = 0;i < wsCount;i++)
			replace += ' ';

		output = output.replace(QChar('\t'), replace);
	}
	return output;
}

QString SourceFormatterPlugin::addIndentation(QString input, const QString indentWith)
{
	QString output;
	QTextStream os(&output, QIODevice::WriteOnly);
	QTextStream is(&input, QIODevice::ReadOnly);

	while (!is.atEnd())
		os << indentWith << is.readLine() << endl;
	return output;
}
*/

}

#include "sourceformattercontroller.moc"

// kate: indent-mode cstyle; space-indent off; tab-width 4;

