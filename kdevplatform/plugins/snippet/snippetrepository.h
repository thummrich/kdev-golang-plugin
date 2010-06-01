/***************************************************************************
 *   Copyright 2007 Robert Gruber <rgruber@users.sourceforge.net>          *
 *   Copyright 2010 Milian Wolff <mail@milianw.de>                         *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef __SNIPPETREPOSITORY_H__
#define __SNIPPETREPOSITORY_H__

#include <QObject>
#include <QStandardItem>
#include <QStringList>

class Snippet;

/**
 * Each object of this type represents a repository of snippets. Each repository
 * has a name and will be saved to an XML file that includes all items of this repository.
 *
 * To access the snippets in this repo, iterate over it's children and dynamic_cast as required.
 * To add a snippet, @p appendRow() it.
 * To access the name of the repository, use @p text() and @p setText().
 *
 * NOTE: Unchecked repositores are considered "disabled" in the sense that their snippets
 *       won't show up during code completion.
 *
 * @author Robert Gruber <rgruber@users.sourceforge.net>
 * @author Milian Wolff <mail@milianw.de>
 */
class SnippetRepository : public QObject, public QStandardItem
{
    Q_OBJECT

public:
    /**
     * Creates a new SnippetRepository. When @p file exists it will be parsed (XML).
     *
     * @param file Location of the snippet's repository file.
     */
    SnippetRepository(const QString& file);
    ~SnippetRepository();

    /**
     * Creates a snippet repository for the given name and adds it to the SnippetStore.
     */
    static SnippetRepository* createRepoFromName(const QString& name);

    /**
     * The license for the snippets contained in this repository.
     */
    QString license() const;
    /**
     * Sets the license for the snippets contained in this repository.
     */
    void setLicense(const QString& license);

    /**
     * The author(s) of the snippets contained in this repository.
     */
    QString authors() const;
    /**
     * Sets the author(s) of the snippets contained in this repository.
     */
    void setAuthors(const QString& authors);

    /**
     * The valid filetypes for the snippets contained in this repository.
     * Empty list means no restriction on the modes.
     * @see KTextEditor::Document::mode()
     */
    QStringList fileTypes() const;
    /**
     * Sets the valid filetypes for the snippets contained in this repository.
     * An empty list, or any list which contains an element "*" is treated as
     * a no-restriction filter.
     */
    void setFileTypes(const QStringList& filetypes);

    /**
     * The path to this repository's file.
     */
    const QString& file() const;

    /**
     * The namespace associated with this repository.
     * Used in CodeCompletion for filtering.
     */
    QString completionNamespace() const;
    /**
     * Sets the code completion namespace for this repository.
     */
    void setCompletionNamespace(const QString& completionNamespace);

    /**
     * Remove this repository from the disk. Also deletes the item and all its children.
     */
    void remove();

    /**
     * Save this repository to disk.
     */
    void save();

    virtual QVariant data(int role = Qt::UserRole + 1) const;
    virtual void setData(const QVariant& value, int role = Qt::UserRole + 1);

private Q_SLOTS:
    /// parses the XML file and load the containing snippets.
    void slotParseFile();

private:
    /// path to the repository file
    QString m_file;
    /// license of the snippets in this repo
    QString m_license;
    /// author(s) of the snippets in this repo
    QString m_authors;
    /// valid filetypes for the snippets in this repo
    QStringList m_filetypes;
    /// filtering namespace for code completion
    QString m_namespace;
};

#endif

