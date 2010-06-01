/*
   Copyright 2008 Hamish Rodda <rodda@kde.org>

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

#ifndef KDEVELOP_OVERRIDESPAGE_H
#define KDEVELOP_OVERRIDESPAGE_H

#include <QtGui/QWizardPage>

#include "language/duchain/declaration.h"

class QTreeWidget;
class QTreeWidgetItem;

namespace KDevelop {

class ClassGenerator;

class KDEVPLATFORMLANGUAGE_EXPORT OverridesPage : public QWizardPage
{
    Q_OBJECT

public:
    OverridesPage(ClassGenerator* generator, QWizard* parent);
    virtual ~OverridesPage();

    QTreeWidget* overrideTree() const;

    QWidget* extraFunctionsContainer() const;

    virtual void initializePage();
    virtual void cleanupPage();
    virtual bool validatePage();
    /**
     * Default implementation populates the tree with all virtual functions in the base classes.
     * Calls @c addPotentialOverride() on each function, where more filtering can be applied.
     * @param baseList Declarations of implemented base classes.
     */
    virtual void populateOverrideTree(const QList<DeclarationPointer> & baseList);
    /**
     * Add @p childDeclaration as potential override. Don't call @c KDevelop::OverridesPage::addPotentialOverride()
     * in overloaded class to filter a declaration.
     * @p classItem The parent class from which @p childDeclaration stems from. Should be used as parent for the override item.
     * @p childDeclaration The overridable function.
     */
    virtual void addPotentialOverride(QTreeWidgetItem* classItem, DeclarationPointer childDeclaration);

protected:
    ClassGenerator* generator() const;

public Q_SLOTS:
    virtual void selectAll();
    virtual void deselectAll();

private:
    class OverridesPagePrivate* const d;
};

}

#endif // KDEVELOP_OVERRIDESPAGE_H
