/* This file is part of KDevelop
    Copyright 2007 Kris Wong <kris.p.wong@gmail.com>
    Copyright 2007 Hamish Rodda <rodda@kde.org>
   Copyright 2007-2009 David Nolden <david.nolden.kdevelop@art-master.de>

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

#include "duchainlock.h"


#include <unistd.h>
#include <QtCore/QThread>

///@todo Always prefer exactly that lock that is requested by the thread that has the foreground mutex,
///           to reduce the amount of UI blocking.

//Nanoseconds to sleep when waiting for a lock
const uint uSleepTime = 500;

#include <kdebug.h>
#include <sys/time.h>
#include "duchain.h"
#include <QThreadStorage>

namespace KDevelop
{
class DUChainLockPrivate
{
public:
  DUChainLockPrivate() {
    m_writer = 0;
    m_writerRecursion = 0;
    m_totalReaderRecursion = 0;
  }

  /**
   * Returns true if there is no reader that is not this thread.
   * */
  bool haveOtherReaders() const {
    ///Since m_totalReaderRecursion is the sum of all reader-recursions, it will be same if either there is no reader at all, or if this thread is the only reader.
    return m_totalReaderRecursion != ownReaderRecursion();
  }

  int ownReaderRecursion() const {
    if(m_readerRecursion.hasLocalData())
      return *m_readerRecursion.localData();
    else
      return 0;
  }
  
  void changeOwnReaderRecursion(int difference) {
    if(m_readerRecursion.hasLocalData()) {
      *m_readerRecursion.localData() += difference;
    }else{
      m_readerRecursion.setLocalData(new int(difference));
    }
    Q_ASSERT(*m_readerRecursion.localData() >= 0);
    m_totalReaderRecursion.fetchAndAddOrdered(difference);
  }

  ///Holds the writer that currently has the write-lock, or zero. Is protected by m_writerRecursion.
  volatile Qt::HANDLE m_writer;

  ///How often is the chain write-locked by the writer? This value protects m_writer,
  ///m_writer may only be changed by the thread that successfully increases this value from 0 to 1
  QAtomicInt m_writerRecursion;
  ///How often is the chain read-locked recursively by all readers? Should be sum of all m_readerRecursion values
  QAtomicInt m_totalReaderRecursion;

  QThreadStorage<int*> m_readerRecursion;
};

class DUChainWriteLockerPrivate
{
public:
  DUChainWriteLockerPrivate() : m_locked(false) {
  }
  DUChainLock* m_lock;
  bool m_locked;
  int m_timeout;
};


DUChainLock::DUChainLock()
  : d(new DUChainLockPrivate)
{
}

DUChainLock::~DUChainLock()
{
  delete d;
}

inline uint toMilliSeconds(timeval v) {
  return v.tv_sec * 1000 + v.tv_usec / 1000;
}

bool DUChainLock::lockForRead(unsigned int timeout)
{
  ///Step 1: Increase the own reader-recursion. This will make sure no further write-locks will succeed
  d->changeOwnReaderRecursion(1);
  
  if(d->m_writer == 0 || d->m_writer == QThread::currentThreadId())
  {
    //Successful lock: Either there is no writer, or we hold the write-lock by ourselves
  }else{
    ///Step 2: Start spinning until there is no writer any more

    timeval startTime;
    gettimeofday(&startTime, 0);

    while(d->m_writer)
    {
      timeval currentTime;
      gettimeofday(&currentTime, 0);
      timeval waited;
      timersub(&currentTime, &startTime, &waited);
      
      if(!timeout || toMilliSeconds(waited) < timeout) {
        usleep(uSleepTime);
      } else {
        //Fail!
        d->changeOwnReaderRecursion(-1);
        return false;
      }
    }
  }
  
  return true;
}

bool DUChainLock::lockForRead() {
  bool ret = lockForRead(0);
  Q_ASSERT(currentThreadHasReadLock());
  return ret;
}

void DUChainLock::releaseReadLock()
{
  d->changeOwnReaderRecursion(-1);
}

bool DUChainLock::currentThreadHasReadLock()
{
  return (bool)d->ownReaderRecursion();
}

bool DUChainLock::lockForWrite(uint timeout)
{
  //It is not allowed to acquire a write-lock while holding read-lock

  Q_ASSERT(d->ownReaderRecursion() == 0);

  if(d->m_writer == QThread::currentThreadId())
  {
    //We already hold the write lock, just increase the recursion count and return
    d->m_writerRecursion.fetchAndAddRelaxed(1);
    return true;
  }
  
  timeval startTime;
  gettimeofday(&startTime, 0);

  while(1)
  {
    //Try acquiring the write-lcok
    if(d->m_totalReaderRecursion == 0 && d->m_writerRecursion.testAndSetOrdered(0, 1))
    {
      //Now we can be sure that there is no other writer, as we have increased m_writerRecursion from 0 to 1
      d->m_writer = QThread::currentThreadId();
      if(d->m_totalReaderRecursion == 0)
      {
        //There is still no readers, we have successfully acquired a write-lock
        return true;
      }else{
        //There may be readers.. we have to continue spinning
        d->m_writer = 0;
        d->m_writerRecursion = 0;
      }
    }
    
    timeval currentTime;
    gettimeofday(&currentTime, 0);
    timeval waited;
    timersub(&currentTime, &startTime, &waited);
    
    if(!timeout || toMilliSeconds(waited) < timeout) {
      usleep(uSleepTime);
    } else {
      //Fail!
      return false;
    }
  }
  
  return false;
}

void DUChainLock::releaseWriteLock()
{
  Q_ASSERT(currentThreadHasWriteLock());

  //The order is important here, m_writerRecursion protects m_writer
  
  if(d->m_writerRecursion == 1)
  {
    d->m_writer = 0;
    d->m_writerRecursion = 0;
  }else{
    d->m_writerRecursion.fetchAndAddOrdered(-1);
  }
}

bool DUChainLock::currentThreadHasWriteLock()
{
  return d->m_writer == QThread::currentThreadId();
}


DUChainReadLocker::DUChainReadLocker(DUChainLock* duChainLock, uint timeout) : m_locked(false), m_timeout(timeout)
{
  m_lock = duChainLock;
  if(!m_lock)
    m_lock =  DUChain::lock();
  m_timeout = timeout;
  lock();
}

DUChainReadLocker::~DUChainReadLocker()
{
  unlock();
}

bool DUChainReadLocker::locked() const {
  return m_locked;
}

bool DUChainReadLocker::lock()
{
  if( m_locked )
    return true;
  
  bool l = false;
  if (m_lock) {
    l = m_lock->lockForRead(m_timeout);
    Q_ASSERT(m_timeout || l);
  };

  m_locked = l;
  
  return l;
}

void DUChainReadLocker::unlock()
{
  if (m_locked && m_lock) {
    m_lock->releaseReadLock();
    m_locked = false;
  }
}


DUChainWriteLocker::DUChainWriteLocker(DUChainLock* duChainLock, uint timeout)
  : d(new DUChainWriteLockerPrivate)
{
  d->m_timeout = timeout;
  d->m_lock = duChainLock;
  
  if(!d->m_lock)
    d->m_lock =  DUChain::lock();
  
  lock();
}
DUChainWriteLocker::~DUChainWriteLocker()
{
  unlock();
  delete d;
}

bool DUChainWriteLocker::lock()
{
  if( d->m_locked )
    return true;
  
  bool l = false;
  if (d->m_lock) {
    l = d->m_lock->lockForWrite(d->m_timeout);
    Q_ASSERT(d->m_timeout || l);
  };

  d->m_locked = l;
  
  return l;
}

bool DUChainWriteLocker::locked() const {
  return d->m_locked;
}

void DUChainWriteLocker::unlock()
{
  if (d->m_locked && d->m_lock) {
    d->m_lock->releaseWriteLock();
    d->m_locked = false;
  }
}
}


// kate: space-indent on; indent-width 2; tab-width 4; replace-tabs on; auto-insert-doxygen on
