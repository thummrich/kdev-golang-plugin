/***************************************************************************
    begin                : Sun Aug 8 1999
    copyright            : (C) 1999 by John Birch
    email                : jbb@kdevelop.org
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef KDEV_VARIABLEWIDGET_H
#define KDEV_VARIABLEWIDGET_H

#include <QTreeView>

#include <KComboBox>

#include "../util/treeview.h"
#include "../debuggerexport.h"
#include "variablecollection.h"

class KMenu;
class KLineEdit;
class KHistoryComboBox;

namespace KDevelop
{

class DebugController;

class VariableTree;
class AbstractVariableItem;

class KDEVPLATFORMDEBUGGER_EXPORT VariableWidget : public QWidget
{
    Q_OBJECT

public:
    VariableWidget( DebugController *controller, QWidget *parent=0 );

Q_SIGNALS:
    void requestRaise();
    void addWatchVariable(const QString& indent);
    void evaluateExpression(const QString& indent);

public Q_SLOTS:
    void slotAddWatch(const QString &ident);

protected:
    virtual void showEvent(QShowEvent* e);
    virtual void hideEvent(QHideEvent* e);

private:
    VariableTree *varTree_;
    KLineEdit *watchVarEntry_;
    KHistoryComboBox *watchVarEditor_;
    VariablesRoot *variablesRoot_;
};

/***************************************************************************/
/***************************************************************************/
/***************************************************************************/

class VariableTree : public KDevelop::AsyncTreeView
{
    Q_OBJECT
public:
    VariableTree(DebugController *controller, VariableWidget *parent);
    virtual ~VariableTree();

    VariableCollection* collection() const;

private:
    void setupActions();
    virtual void contextMenuEvent(QContextMenuEvent* event);

private slots:
    void deleteWatch();

#if 0
Q_SIGNALS:
    void toggleWatchpoint(const QString &varName);

protected:
    virtual void contextMenuEvent(QContextMenuEvent* event);
    virtual void keyPressEvent(QKeyEvent* e);
    virtual void showEvent(QShowEvent* event);

private: // helper functions
    void handleAddressComputed(const GDBMI::ResultRecord& r);

    void updateCurrentFrame();

    void copyToClipboard(AbstractVariableItem* item);

    KMenu* activePopup_;
    QAction* toggleWatch_;
#endif
private:
    QAction *m_watchDelete;
};

}

#endif
