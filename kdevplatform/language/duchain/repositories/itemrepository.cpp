/*
   Copyright 2008 David Nolden <david.nolden.kdevelop@art-master.de>

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

#include "itemrepository.h"

#include <QDataStream>
#include <QUuid>

#include <kstandarddirs.h>
#include <kcomponentdata.h>
#include <klockfile.h>
#include <kmessagebox.h>
#include <klocale.h>

#include <interfaces/icore.h>

#include "../duchain.h"
#include <interfaces/isession.h>

namespace KDevelop {

//If KDevelop crashed this many times consicutively, clean up the repository
const int crashesBeforeCleanup = 2;

uint staticItemRepositoryVersion() {
  //Increase this to reset incompatible item-repositories
  return 67;
}

AbstractItemRepository::~AbstractItemRepository() {
}

ItemRepositoryRegistry::ItemRepositoryRegistry(QString openPath, KLockFile::Ptr lock) : m_mutex(QMutex::Recursive) {
  if(!openPath.isEmpty())
    open(openPath, false, lock);
}

QMutex& ItemRepositoryRegistry::mutex() {
  return m_mutex;
}

QAtomicInt& ItemRepositoryRegistry::getCustomCounter(const QString& identity, int initialValue) {
  if(!m_customCounters.contains(identity))
    m_customCounters.insert(identity, new QAtomicInt(initialValue));
  return *m_customCounters[identity];
}

bool processExists(int pid) {
  ///@todo Find a cross-platform way of doing this!
  QFileInfo f(QString("/proc/%1").arg(pid));
  return f.exists();
}

QPair<QString, KLockFile::Ptr> allocateRepository() {
  KLockFile::Ptr lock;
  QString repoPath;
  
   KComponentData component("item repositories temp", QByteArray(), KComponentData::SkipMainComponentRegistration);
    QString baseDir = QDir::homePath() + "/.kdevduchain";
    KStandardDirs::makeDir(baseDir);

    Q_ASSERT( ICore::self() );
    Q_ASSERT( ICore::self()->activeSession() );

    baseDir += "/" + ICore::self()->activeSession()->id().toString();

    //Since each instance of kdevelop needs an own directory, iterate until we find a not-yet-used one
    for(int a = 0; a < 100; ++a) {
      QString specificDir = baseDir + QString("/%1").arg(a);
      KStandardDirs::makeDir(specificDir);

       lock = new KLockFile(specificDir + "/lock", component);
       KLockFile::LockResult result = lock->lock(KLockFile::NoBlockFlag | KLockFile::ForceFlag);
       bool useDir = false;
       if(result != KLockFile::LockOK) {
         int pid;
         QString hostname, appname;
         if(lock->getLockInfo(pid, hostname, appname)) {
           if(!processExists(pid)) {
             kDebug() << "The process holding" << specificDir << "with pid" << pid << "does not exists any more. Re-using the directory.";
             QFile::remove(specificDir + "/lock");
             useDir = true;
             if(lock->lock(KLockFile::NoBlockFlag | KLockFile::ForceFlag) != KLockFile::LockOK) {
               kWarning() << "Failed to re-establish the lock in" << specificDir;
               continue;
             }
           }
         }
       }else {
         useDir = true;
       }
       if(useDir) {
          repoPath = specificDir;
          if(result == KLockFile::LockStale) {
            kWarning() << "stale lock detected:" << specificDir + "/lock";
          }
          break;
       }
    }
    
    if(repoPath.isEmpty()) {
      kError() << "could not create a directory for the duchain data";
    }else{
      kDebug() << "picked duchain directory" << repoPath;
    }
    
    return qMakePair(repoPath, lock);
}

///The global item-repository registry that is used by default
static ItemRepositoryRegistry& allocateGlobalItemRepositoryRegistry() {
  QPair<QString, KLockFile::Ptr> repo = allocateRepository();
  
  ///We intentionally leak the registry, to prevent problems in the destruction order, where
  ///the actual repositories might get deleted later than the repository registry.
  static ItemRepositoryRegistry* global = new ItemRepositoryRegistry(repo.first, repo.second);
  return *global;
}

///The global item-repository registry that is used by default
ItemRepositoryRegistry& globalItemRepositoryRegistry() {
  
  static ItemRepositoryRegistry& global(allocateGlobalItemRepositoryRegistry());
  return global;
}

void ItemRepositoryRegistry::registerRepository(AbstractItemRepository* repository, AbstractRepositoryManager* manager) {
  QMutexLocker lock(&m_mutex);
  m_repositories.insert(repository, manager);
  if(!m_path.isEmpty()) {
    if(!repository->open(m_path)) {
      deleteDataDirectory();
      kError() << "failed to open a repository";
      abort();
    }
  }
}

QString ItemRepositoryRegistry::path() const {
  //We cannot lock the mutex here, since this may be called with one of the repositories locked,
  //and that may lead to a deadlock when at the same time a storing is requested
  return m_path;
}

void ItemRepositoryRegistry::lockForWriting() {
  QMutexLocker lock(&m_mutex);
  //Create is_writing
  QFile f(m_path + "/is_writing");
  f.open(QIODevice::WriteOnly);
  f.close();
}

void ItemRepositoryRegistry::unlockForWriting() {
  QMutexLocker lock(&m_mutex);
  //Delete is_writing
  QFile::remove(m_path + "/is_writing");
}

void ItemRepositoryRegistry::unRegisterRepository(AbstractItemRepository* repository) {
  QMutexLocker lock(&m_mutex);
  Q_ASSERT(m_repositories.contains(repository));
  repository->close();
  m_repositories.remove(repository);
}

//Recursive delete, copied from a mailing-list
//Returns true on success
bool removeDirectory(const QDir &aDir)
{
  bool has_err = false;
  if (aDir.exists())//QDir::NoDotAndDotDot
  {
    QFileInfoList entries = aDir.entryInfoList(QDir::NoDotAndDotDot | 
    QDir::Dirs | QDir::Files);
    int count = entries.size();
    for (int idx = 0; ((idx < count) && !has_err); idx++)
    {
      QFileInfo entryInfo = entries[idx];
      QString path = entryInfo.absoluteFilePath();
      if (entryInfo.isDir())
      {
        has_err = !removeDirectory(QDir(path));
      }
      else
      {
        QFile file(path);
        if (!file.remove())
        has_err = true;
      }
    }
    if (!aDir.rmdir(aDir.absolutePath()))
      has_err = true;
  }
  return !has_err;
}

//After calling this, the data-directory may be a new one
void ItemRepositoryRegistry::deleteDataDirectory() {
  QMutexLocker lock(&m_mutex);
  QFileInfo pathInfo(m_path);
  QDir d(m_path);

  //lockForWriting creates a file, that prevents any other KDevelop instance from using the directory as it is.
  //Instead, the other instance will try to delete the directory as well.
  lockForWriting();
  
  // Have to release the lock here, else it will delete the new lock and windows needs all file-handles
  // to be released before deleting a directory that contains these files
  
  m_lock->unlock();
  bool result = removeDirectory(d);
  Q_ASSERT(result);
  Q_ASSERT(m_lock);
  //Just remove the old directory, and allocate a new one. Probably it'll be the same one.
  QPair<QString, KLockFile::Ptr> repo = allocateRepository();
  m_path = repo.first;
  m_lock = repo.second;
}

bool ItemRepositoryRegistry::open(const QString& path, bool clear, KLockFile::Ptr lock) {
  QMutexLocker mlock(&m_mutex);
  if(m_path == path && !clear)
    return true;
  
  m_lock = lock;
  m_path = path;

  bool needRepoCheck = true;
  
  while(needRepoCheck) {

    if(QFile::exists(m_path + "/is_writing")) {
      kWarning() << "repository" << m_path << "was write-locked, it probably is inconsistent";
      clear = true;
    }
  
    QDir pathDir(m_path);
    pathDir.setFilter(QDir::Files);
    
    //When there is only one file in the repository, it's the lock-file, and the repository has just been cleared
    if(pathDir.count() != 1)
    {
      if(!QFile::exists( m_path + QString("/version_%1").arg(staticItemRepositoryVersion()) ))
      {
      kWarning() << "version-hint not found, seems to be an old version";
      clear = true;
      }else if(getenv("CLEAR_DUCHAIN_DIR"))
      {
        kWarning() << "clearing duchain directory because CLEAR_DUCHAIN_DIR is set";
        clear = true;
      }
    }
    
    QFile crashesFile(m_path + QString("/crash_counter"));
    if(crashesFile.open(QIODevice::ReadOnly)) {
      int count;
      QDataStream stream(&crashesFile);
      stream >> count;

      kDebug() << "current count of crashes: " << count;
      
      if(count >= crashesBeforeCleanup)
      {
        ///@todo Ask the user
        kWarning() << "kdevelop crashed" << count << "times in a row with the duchain repository" << m_path << ", clearing it";
        clear = true;
      }else{
        ///Increase the crash-count. It will be reset of kdevelop is shut down cleanly.
        ++count;
        crashesFile.close();
        crashesFile.open(QIODevice::WriteOnly | QIODevice::Truncate);
        QDataStream writeStream(&crashesFile);
        writeStream << count;
      }
    }else{
        ///Increase the crash-count. It will be reset of kdevelop is shut down cleanly.
        crashesFile.open(QIODevice::WriteOnly | QIODevice::Truncate);
        QDataStream writeStream(&crashesFile);
        writeStream << 1;
    }
    
    if(clear) {
        kWarning() << QString("The data-repository at %1 has to be cleared.").arg(m_path);
  //     KMessageBox::information( 0, i18n("The data-repository at %1 has to be cleared. Either the disk format has changed, or KDevelop crashed while writing the repository.", m_path ) );
#ifdef Q_OS_WIN
        /// on Windows a file can't be deleted unless the last file handle gets closed
        /// deleteDataDirectory would enter a never ending loop
        crashesFile.close();
#endif
        deleteDataDirectory();
        clear = false;
        //We need to re-check, because a new data-directory may have been picked
    }else{
        needRepoCheck = false;
    }
  }
  
  foreach(AbstractItemRepository* repository, m_repositories.keys()) {
    if(!repository->open(path)) {
      deleteDataDirectory();
      kError() << "failed to open a repository";
      abort();
    }
  }
  
  QFile f(path + "/Counters");
  if(f.open(QIODevice::ReadOnly)) {
    QDataStream stream(&f);
    
    while(!stream.atEnd()) {
      //Read in all custom counter values
      QString counterName;
      stream >> counterName;
      int counterValue;
      stream >> counterValue;
      if(m_customCounters.contains(counterName))
        *m_customCounters[counterName] = counterValue;
      else
        getCustomCounter(counterName, 0) = counterValue;
    }
  }else{
//     kDebug() << "Could not open counter file";
  }
  
  return true;
}

void ItemRepositoryRegistry::store() {
  QMutexLocker lock(&m_mutex);
  foreach(AbstractItemRepository* repository, m_repositories.keys())
    repository->store();

  QFile versionFile(m_path + QString("/version_%1").arg(staticItemRepositoryVersion()));
  if(versionFile.open(QIODevice::WriteOnly)) {
    versionFile.close();
  }else{
    kWarning() << "Could not open version file for writing";
  }
  
  //Store all custom counter values
  QFile f(m_path + "/Counters");
  if(f.open(QIODevice::WriteOnly)) {
    f.resize(0);
    QDataStream stream(&f);
    for(QMap<QString, QAtomicInt*>::const_iterator it = m_customCounters.constBegin(); it != m_customCounters.constEnd(); ++it) {
      stream << it.key();
      stream << it.value()->fetchAndAddRelaxed(0);
    }
  }else{
    kWarning() << "Could not open counter file for writing";
  }
}

void ItemRepositoryRegistry::printAllStatistics() const {
  QMutexLocker lock(&m_mutex);
  foreach(AbstractItemRepository* repository, m_repositories.keys()) {
    kDebug() << "statistics in" << repository->repositoryName() << ":";
    kDebug() << repository->printStatistics();
  }
}

int ItemRepositoryRegistry::finalCleanup() {
  QMutexLocker lock(&m_mutex);
  int changed = false;
  foreach(AbstractItemRepository* repository, m_repositories.keys()) {
    int added = repository->finalCleanup();
    changed += added;
    kDebug() << "cleaned in" << repository->repositoryName() << ":" << added;
  }
  
  return changed;
}


void ItemRepositoryRegistry::close() {

  QMutexLocker lock(&m_mutex);
    
  foreach(AbstractItemRepository* repository, m_repositories.keys())
    repository->close();
  
  m_path.clear();
}

ItemRepositoryRegistry::~ItemRepositoryRegistry() {
  close();
  QMutexLocker lock(&m_mutex);
  foreach(QAtomicInt* counter, m_customCounters)
    delete counter;
}

void ItemRepositoryRegistry::shutdown() {
  QFile::remove(m_path + QString("/crash_counter"));
  if(m_lock)
    m_lock->unlock();
}

}
