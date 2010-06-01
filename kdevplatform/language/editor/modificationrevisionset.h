/*
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

#ifndef MODIFICATIONREVISIONSET_H
#define MODIFICATIONREVISIONSET_H

#include <sys/types.h>
#include <language/util/basicsetrepository.h>
#include "modificationrevision.h"

namespace KDevelop {

class KDEVPLATFORMLANGUAGE_EXPORT ModificationRevisionSet
{
  public:
    ModificationRevisionSet(uint index = 0);
    
    static void clearCache();
    
    void clear();
    
    uint index() const {
      return m_index;
    }
    
    ///Returns the count of file dependencies in this set
    uint size() const;
    
    void addModificationRevision(const IndexedString& url, const ModificationRevision& revision);

    ///Returns true if the modification-revision was contained before.
    bool removeModificationRevision(const IndexedString& url, const ModificationRevision& revision);
    
//     const QMap<KDevelop::IndexedString, KDevelop::ModificationRevision> allModificationTimes() const;
    
    bool needsUpdate() const;
    
    QString toString() const;
    
    bool operator!=(const ModificationRevisionSet& rhs) const {
      return m_index != rhs.m_index;
    }

    bool operator==(const ModificationRevisionSet& rhs) const {
      return m_index == rhs.m_index;
    }
    
    ModificationRevisionSet& operator+=(const ModificationRevisionSet& rhs);
    ModificationRevisionSet& operator-=(const ModificationRevisionSet& rhs);
    
  private:
    uint m_index;
};
}

#endif // MODIFICATIONREVISIONSET_H
