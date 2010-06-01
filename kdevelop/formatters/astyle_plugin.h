/* This file is part of KDevelop
*  Copyright (C) 2008 Cédric Pasteur <cedric.pasteur@free.fr>
Copyright (C) 2001 Matthias Hölzer-Klüpfel <mhk@caldera.de>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.

 */

#ifndef ASTYLEPLUGIN_H
#define ASTYLEPLUGIN_H

#include <interfaces/iplugin.h>
#include <interfaces/isourceformatter.h>

class AStyleFormatter;

class AStylePlugin : public KDevelop::IPlugin, public KDevelop::ISourceFormatter
{
    Q_OBJECT
    Q_INTERFACES(KDevelop::ISourceFormatter)

    public:
        explicit AStylePlugin(QObject *parent, const QVariantList & = QVariantList());
        ~AStylePlugin();

        virtual QString name();
        virtual QString caption();
        virtual QString description();

        virtual QString highlightModeForMime(const KMimeType::Ptr &mime);

        /** Formats using the current style.
        */
        virtual QString formatSource(const QString &text, const KMimeType::Ptr &mime, const QString& leftContext, const QString& rightContext);

        /** \return A map of predefined styles (a key and a caption for each type)
        */
        virtual QList<KDevelop::SourceFormatterStyle> predefinedStyles();

        /** \return The widget to edit a style.
        */
        virtual KDevelop::SettingsWidget* editStyleWidget(const KMimeType::Ptr &mime);

        virtual QString formatSourceWithStyle(KDevelop::SourceFormatterStyle, const QString& text,
                                              const KMimeType::Ptr &mime,
                                              const QString& leftContext = QString(),
                                              const QString& rightContext = QString() );
        
        /** \return The text used in the config dialog to preview the current style.
        */
        virtual QString previewText(const KMimeType::Ptr &mime);
        
        /** \return The indentation type of the currently selected style.
        */
        virtual IndentationType indentationType();
        /** \return The number of spaces used for indentation if IndentWithSpaces is used,
        * or the number of spaces per tab if IndentWithTabs is selected.
        */
        virtual int indentationLength();

        static QString formattingSample();
        static QString indentingSample();

    private:
        AStyleFormatter *m_formatter;
        KDevelop::SourceFormatterStyle currentStyle;
};

#endif // ASTYLEPLUGIN_H
