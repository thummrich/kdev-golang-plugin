/* This file is part of KDevelop
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
#ifndef ISOURCEFORMATTER_H
#define ISOURCEFORMATTER_H

#include <QtGui/QWidget>
#include <QtCore/QStringList>
#include <KDE/KMimeType>

#include "interfacesexport.h"

namespace KDevelop
{

class KDEVPLATFORMINTERFACES_EXPORT SourceFormatterStyle
{
public:
	SourceFormatterStyle() {};
	SourceFormatterStyle( const QString& name ) : m_name(name) {}
	void setContent( const QString& content ) { m_content = content; }
	void setCaption( const QString& caption ) { m_caption = caption; }
	QString content() const { return m_content; }
	QString caption() const { return m_caption; }
	QString name() const { return m_name; }
private:
	QString m_name;
	QString m_caption;
	QString m_content;
};

/**
* @short A widget to edit a style
* A plugin should inherit this widget to create a widget to
* edit a style.
* @author Cédric Pasteur
*/
class KDEVPLATFORMINTERFACES_EXPORT SettingsWidget : public QWidget
{
		Q_OBJECT

	public:
		SettingsWidget(QWidget *parent = 0);
		virtual ~SettingsWidget();

		/** This function is called after the creation of the dialog.
		* it should initialise the widgets with the values corresponding to
		* the predefined style \arg name if it's not empty, or
		* to the string \arg content.
		*/
		virtual void load(const SourceFormatterStyle&) = 0;

		/** \return A string representing the state of the config.
		*/
		virtual QString save() = 0;

	Q_SIGNALS:
		/** Emits this signal when a setting was changed and the preview
		* needs to be updated. \arg text is the text that will be shown in the
		* editor. One might want to show different text
		* according to the different pages shown in the widget.
		* Text should already be formatted.
		*/
		void previewTextChanged(const QString &text);
};

/**
 * @short An interface for a source beautifier
 * An example of a plugin using an external executable to do
 * the formatting can be found in kdevelop/plugins/formatters/indent_plugin.cpp.
 * @author Cédric Pasteur
 */
class KDEVPLATFORMINTERFACES_EXPORT ISourceFormatter
{
	public:
		virtual ~ISourceFormatter();

		enum IndentationType {
			NoChange,
			IndentWithTabs,
			IndentWithSpaces,
			IndentWithSpacesAndConvertTabs
		};

		/** \return The name of the plugin. This should contain only
		* ASCII chars and no spaces. This will be used internally to identify
		* the plugin.
		*/
		virtual QString name() = 0;
		/** \return A caption describing the plugin.
		*/
		virtual QString caption() = 0;
		/** \return A more complete description of the plugin.
		* The string should be written in Rich text. It can eg contain
		* a link to the project homepage.
		*/
		virtual QString description() = 0;

		/** \return The highlighting mode for Kate corresponding to this mime.
		*/
		virtual QString highlightModeForMime(const KMimeType::Ptr &mime) = 0;
		
		/** Formats using the current style.
		 * @param text The text to format
		 * @param leftContext The context at the left side of the text. If it is in another line, it must end with a newline.
		 * @param rightContext The context at the right side of the text. If it is in the next line, it must start with a newline.
		 *
		 * If the source-formatter cannot work correctly with the context, it will just return the input text.
		*/
		virtual QString formatSource(const QString &text, const KMimeType::Ptr &mime, const QString& leftContext = QString(), const QString& rightContext = QString()) = 0;

		/**
		 * Format with the given style, this is mostly for the kcm to format the preview text
		 * Its a bit of a hassle that this needs to be public API, but I can't find a better way
		 * to do this.
		 */
		virtual QString formatSourceWithStyle( SourceFormatterStyle,
											   const QString& text,
											   const KMimeType::Ptr &mime,
											   const QString& leftContext = QString(),
											   const QString& rightContext = QString() ) = 0;

		/** \return A map of predefined styles (a key and a caption for each type)
		*/
		virtual QList<SourceFormatterStyle> predefinedStyles() = 0;

		/** \return The widget to edit a style.
		*/
		virtual SettingsWidget* editStyleWidget(const KMimeType::Ptr &mime) = 0;

		/** \return The text used in the config dialog to preview the current style.
		*/
		virtual QString previewText(const KMimeType::Ptr &mime) = 0;

		/** \return The indentation type of the currently selected style.
		*/
		virtual IndentationType indentationType() = 0;
		/** \return The number of spaces used for indentation if IndentWithSpaces is used,
		* or the number of spaces per tab if IndentWithTabs is selected.
		*/
		virtual int indentationLength() = 0;

		/** \return A string representing the map. Values are written in the form
		* key=value and separated with ','.
		*/
		static QString optionMapToString(const QMap<QString, QVariant> &map);
		/** \return A map corresponding to the string, that was created with
		* \ref optionMapToString.
		*/
		static QMap<QString, QVariant> stringToOptionMap(const QString &option);

		/** \return A message to display when an executable needed by a
		* plugin is missing. This should be returned as description
		* if a needed executable is not found.
		*/
		static QString missingExecutableMessage(const QString &name);
};

}

Q_DECLARE_INTERFACE(KDevelop::ISourceFormatter, "org.kdevelop.ISourceFormatter")

#endif // ISOURCEFORMATTER_H
// kate: indent-mode cstyle; space-indent off; tab-width 4;
