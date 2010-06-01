/* Copyright 2008 Aleix Pol <aleixpol@gmail.com>
 * Copyright 2009 Ramón Zarazúa <killerfox512+kde@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef APPLYCHANGESWIDGET_H
#define APPLYCHANGESWIDGET_H

#include <KDE/KDialog>
#include "../languageexport.h"

namespace KTextEditor { class Document; class Range;}
class KUrl;
class QModelIndex;

namespace KDevelop
{
class IndexedString;
class ApplyChangesWidgetPrivate;

class KDEVPLATFORMLANGUAGE_EXPORT ApplyChangesWidget : public KDialog
{
    Q_OBJECT
    public:
        ApplyChangesWidget(QWidget* parent=0);
        ~ApplyChangesWidget();
        
        void setInformation(const QString& info);

        KTextEditor::Document* document() const;
        
        ///@param modified may be an artifial code representation (@ref KDevelop::InsertArtificialCodeRepresentation)
        void addDocuments(const IndexedString & original);
        
        ///This will save all the modified files into their originals
        bool applyAllChanges();
        
        ///Update the comparison view fo @p index, in case changes have been done directly to the opened document.
        ///@param index the index to update, -1 for current index
        void updateDiffView(int index = -1);
    
    public Q_SLOTS:
        ///Called when a change between comparison and edition is requested
        void switchEditView();
        ///Called to signal a change to the currently viewed index
        void indexChanged(int);
        
    private Q_SLOTS:
        void change (KTextEditor::Document *document, const KTextEditor::Range &oldRange,
                const KTextEditor::Range &newRange);
        void insertion (KTextEditor::Document *document, const KTextEditor::Range &range);
        void removal (KTextEditor::Document *document, const KTextEditor::Range &range);
        void jump( const QModelIndex &);
        
    private:
        ApplyChangesWidgetPrivate * d;
};

}

#endif
