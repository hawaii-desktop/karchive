/* This file is part of the KDE libraries
   Copyright (C) 2000-2005 David Faure <faure@kde.org>
   Copyright (C) 2003 Leo Savernik <l.savernik@aon.at>

   Moved from ktar.cpp by Roberto Teixeira <maragato@kde.org>

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

#include "karchive.h"
#include "karchivehandler.h"
#include "karchivehandlerplugin.h"
#include "klimitediodevice_p.h"

#include <qplatformdefs.h> // QT_STATBUF, QT_LSTAT

#include <qsavefile.h>

#include <QStack>
#include <QtCore/QCoreApplication>
#include <QtCore/QMap>
#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QMimeDatabase>
#include <QtCore/QMimeType>
#include <QtCore/QPluginLoader>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef Q_OS_UNIX
#include <limits.h>  // PATH_MAX
#endif

class KArchivePrivate
{
public:
    KArchivePrivate()
        : handler( 0 )
    {}
    ~KArchivePrivate()
    {
        delete handler;
    }

    KArchiveHandler *handler;
};

////////////////////////////////////////////////////////////////////////
/////////////////////////// KArchive ///////////////////////////////////
////////////////////////////////////////////////////////////////////////

KArchive::KArchive( const QString& fileName )
	: d(new KArchivePrivate)
{
    Q_ASSERT( !fileName.isEmpty() );

    // Detect the MIME Type
    QMimeDatabase db;
    QMimeType mime;
    if (QFile::exists(fileName)) {
        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly)) {
            mime= db.mimeTypeForData(&file);
            file.close();
        }
    }

    // Unable to determine MIME Type from contents so get it from file name
    if (!mime.isValid())
        mime = db.mimeTypeForFile(fileName, QMimeDatabase::MatchExtension);

    // If we still can't determine it, raise a fatal error
    if (!mime.isValid())
        qFatal("Could not determine the MIME Type for %s, cannot continue!",
               qPrintable(fileName));

    // Find the appropriate plugin
    KArchiveHandler *handler = loadPlugin(mime.name());

    // We cannot continue if no archive handler have been found
    if (!handler)
        qFatal("No archive handler have been found for %s, cannot continue!",
               qPrintable(mime.name()));

    d->handler = handler;
    d->handler->setArchive(this);
    d->handler->setFileName(fileName);
}

KArchive::KArchive( QIODevice * dev )
	: d(new KArchivePrivate)
{
    // Detect the MIME Type
    QMimeDatabase db;
    QMimeType mime = db.mimeTypeForData(dev);
    if (!mime.isValid())
        qFatal("Could not determine the MIME Type for device, cannot continue!");

    // Find the appropriate plugin
    KArchiveHandler *handler = loadPlugin(mime.name());

    // We cannot continue if no archive handler have been found
    if (!handler)
        qFatal("No archive handler have been found for %s, cannot continue!",
               qPrintable(mime.name()));

    d->handler = handler;
    d->handler->setArchive(this);
    d->handler->setDevice(dev);
}

KArchive::~KArchive()
{
    if ( isOpen() )
        close();

    delete d;
}

bool KArchive::open( QIODevice::OpenMode mode )
{
    Q_ASSERT( mode != QIODevice::NotOpen );
    return d->handler->open( mode );
}

bool KArchive::close()
{
    return d->handler->close();
}

const KArchiveDirectory* KArchive::directory() const
{
    return d->handler->rootDir();
}

bool KArchive::addLocalFile( const QString& fileName, const QString& destName )
{
    QFileInfo fileInfo( fileName );
    if ( !fileInfo.isFile() && !fileInfo.isSymLink() )
    {
        //qWarning() << fileName << "doesn't exist or is not a regular file.";
        return false;
    }

    QT_STATBUF fi;
    if (QT_LSTAT(QFile::encodeName(fileName).constData(), &fi) == -1) {
        /*qWarning() << "stat'ing" << fileName
        	<< "failed:" << strerror(errno);*/
        return false;
    }

    if (fileInfo.isSymLink()) {
        QString symLinkTarget;
        // Do NOT use fileInfo.readLink() for unix symlinks!
        // It returns the -full- path to the target, while we want the target string "as is".
#if defined(Q_OS_UNIX) && !defined(Q_OS_OS2EMX)
        const QByteArray encodedFileName = QFile::encodeName(fileName);
        QByteArray s;
#if defined(PATH_MAX)
        s.resize(PATH_MAX+1);
#else
        int path_max = pathconf(encodedFileName.data(), _PC_PATH_MAX);
        if (path_max <= 0) {
            path_max = 4096;
        }
        s.resize(path_max);
#endif
        int len = readlink(encodedFileName.data(), s.data(), s.size() - 1);
        if ( len >= 0 ) {
            s[len] = '\0';
            symLinkTarget = QFile::decodeName(s);
        }
#endif
        if (symLinkTarget.isEmpty()) // Mac or Windows
            symLinkTarget = fileInfo.symLinkTarget();
        return writeSymLink(destName, symLinkTarget, fileInfo.owner(),
                            fileInfo.group(), fi.st_mode, fi.st_atime, fi.st_mtime,
                            fi.st_ctime);
    }/*end if*/

    qint64 size = fileInfo.size();

    // the file must be opened before prepareWriting is called, otherwise
    // if the opening fails, no content will follow the already written
    // header and the tar file is effectively f*cked up
    QFile file( fileName );
    if ( !file.open( QIODevice::ReadOnly ) )
    {
        //qWarning() << "couldn't open file " << fileName;
        return false;
    }

    if ( !prepareWriting( destName, fileInfo.owner(), fileInfo.group(), size,
    		fi.st_mode, fi.st_atime, fi.st_mtime, fi.st_ctime ) )
    {
        //qWarning() << " prepareWriting" << destName << "failed";
        return false;
    }

    // Read and write data in chunks to minimize memory usage
    QByteArray array;
    array.resize( int( qMin( qint64( 1024 * 1024 ), size ) ) );
    qint64 n;
    qint64 total = 0;
    while ( ( n = file.read( array.data(), array.size() ) ) > 0 )
    {
        if ( !writeData( array.data(), n ) )
        {
            //qWarning() << "writeData failed";
            return false;
        }
        total += n;
    }
    Q_ASSERT( total == size );

    if ( !finishWriting( size ) )
    {
        //qWarning() << "finishWriting failed";
        return false;
    }
    return true;
}

bool KArchive::addLocalDirectory( const QString& path, const QString& destName )
{
    QDir dir( path );
    if ( !dir.exists() )
        return false;
    dir.setFilter(dir.filter() | QDir::Hidden);
    const QStringList files = dir.entryList();
    for ( QStringList::ConstIterator it = files.begin(); it != files.end(); ++it )
    {
        if ( *it != QLatin1String(".") && *it != QLatin1String("..") )
        {
            QString fileName = path + QLatin1Char('/') + *it;
//            qDebug() << "storing " << fileName;
            QString dest = destName.isEmpty() ? *it : (destName + QLatin1Char('/') + *it);
            QFileInfo fileInfo( fileName );

            if ( fileInfo.isFile() || fileInfo.isSymLink() )
                addLocalFile( fileName, dest );
            else if ( fileInfo.isDir() )
                addLocalDirectory( fileName, dest );
            // We omit sockets
        }
    }
    return true;
}

bool KArchive::writeFile( const QString& name, const QString& user,
                          const QString& group, const char* data, qint64 size,
                          mode_t perm, time_t atime, time_t mtime, time_t ctime )
{
    if ( !prepareWriting( name, user, group, size, perm, atime, mtime, ctime ) )
    {
        //qWarning() << "prepareWriting failed";
        return false;
    }

    // Write data
    // Note: if data is 0L, don't call write, it would terminate the KFilterDev
    if ( data && size && !writeData( data, size ) )
    {
        //qWarning() << "writeData failed";
        return false;
    }

    if ( !finishWriting( size ) )
    {
        //qWarning() << "finishWriting failed";
        return false;
    }
    return true;
}

bool KArchive::writeData( const char* data, qint64 size )
{
    bool ok = device()->write( data, size ) == size;
    if ( !ok )
        d->handler->abortWriting();
    return ok;
}

// The writeDir -> doWriteDir pattern allows to avoid propagating the default
// values into all virtual methods of subclasses, and it allows more extensibility:
// if a new argument is needed, we can add a writeDir overload which stores the
// additional argument in the d pointer, and doWriteDir reimplementations can fetch
// it from there.

bool KArchive::writeDir( const QString& name, const QString& user, const QString& group,
                         mode_t perm, time_t atime,
                         time_t mtime, time_t ctime )
{
    return d->handler->doWriteDir( name, user, group, perm | 040000, atime, mtime, ctime );
}

bool KArchive::writeSymLink(const QString &name, const QString &target,
                            const QString &user, const QString &group,
                            mode_t perm, time_t atime,
                            time_t mtime, time_t ctime )
{
    return d->handler->doWriteSymLink( name, target, user, group, perm, atime, mtime, ctime );
}


bool KArchive::prepareWriting( const QString& name, const QString& user,
                               const QString& group, qint64 size,
                               mode_t perm, time_t atime,
                               time_t mtime, time_t ctime )
{
    bool ok = d->handler->doPrepareWriting( name, user, group, size, perm, atime, mtime, ctime );
    if ( !ok )
        d->handler->abortWriting();
    return ok;
}

bool KArchive::finishWriting( qint64 size )
{
    return d->handler->doFinishWriting( size );
}

KArchiveDirectory * KArchive::rootDir()
{
    return d->handler->rootDir();
}

void KArchive::setDevice( QIODevice * dev )
{
    d->handler->setDevice( dev );
}

void KArchive::setRootDir( KArchiveDirectory *rootDir )
{
    d->handler->setRootDir( rootDir );
}

QIODevice::OpenMode KArchive::mode() const
{
    return d->handler->mode();
}

QIODevice * KArchive::device() const
{
    return d->handler->device();
}

bool KArchive::isOpen() const
{
    return d->handler->isOpen();
}

QString KArchive::fileName() const
{
    return d->handler->fileName();
}

KArchiveHandler *KArchive::loadPlugin( const QString &mimeType )
{
    KArchiveHandler *handler = 0;

    const QStringList libPaths = QCoreApplication::libraryPaths();
    const QString pathSuffix = QLatin1String("/karchivehandlers/");
    foreach (const QString &libPath, libPaths) {
        QDir dir(libPath + pathSuffix);
        if (!dir.exists())
            continue;

        foreach (const QString &fileName, dir.entryList(QDir::Files)) {
            QPluginLoader loader(fileName);
            KArchiveHandlerPlugin *plugin =
                qobject_cast<KArchiveHandlerPlugin *>(loader.instance());
            if (plugin) {
                handler = plugin->create(mimeType);
                if (handler)
                    break;
            }
        }
    }

    return handler;
}

////////////////////////////////////////////////////////////////////////
/////////////////////// KArchiveEntry //////////////////////////////////
////////////////////////////////////////////////////////////////////////

class KArchiveEntryPrivate
{
public:
    KArchiveEntryPrivate( KArchive* _archive, const QString& _name, int _access,
                          int _date, const QString& _user, const QString& _group,
                          const QString& _symlink) :
        name(_name),
        date(_date),
        access(_access),
        user(_user),
        group(_group),
        symlink(_symlink),
        archive(_archive)
    {}
    QString name;
    int date;
    mode_t access;
    QString user;
    QString group;
    QString symlink;
    KArchive* archive;
};

KArchiveEntry::KArchiveEntry( KArchive* t, const QString& name, int access, int date,
                      const QString& user, const QString& group, const
                      QString& symlink) :
    d(new KArchiveEntryPrivate(t,name,access,date,user,group,symlink))
{
}

KArchiveEntry::~KArchiveEntry()
{
    delete d;
}

QDateTime KArchiveEntry::datetime() const
{
  QDateTime datetimeobj;
  datetimeobj.setTime_t( d->date );
  return datetimeobj;
}

int KArchiveEntry::date() const
{
    return d->date;
}

QString KArchiveEntry::name() const
{
    return d->name;
}

mode_t KArchiveEntry::permissions() const
{
    return d->access;
}

QString KArchiveEntry::user() const
{
    return d->user;
}

QString KArchiveEntry::group() const
{
    return d->group;
}

QString KArchiveEntry::symLinkTarget() const
{
    return d->symlink;
}

bool KArchiveEntry::isFile() const
{
    return false;
}

bool KArchiveEntry::isDirectory() const
{
    return false;
}

KArchive* KArchiveEntry::archive() const
{
    return d->archive;
}

////////////////////////////////////////////////////////////////////////
/////////////////////// KArchiveFile ///////////////////////////////////
////////////////////////////////////////////////////////////////////////

class KArchiveFilePrivate
{
public:
    KArchiveFilePrivate( qint64 _pos, qint64 _size ) :
        pos(_pos),
        size(_size)
    {}
    qint64 pos;
    qint64 size;
};

KArchiveFile::KArchiveFile( KArchive* t, const QString& name, int access, int date,
                            const QString& user, const QString& group,
                            const QString & symlink,
                            qint64 pos, qint64 size )
  : KArchiveEntry( t, name, access, date, user, group, symlink ),
    d( new KArchiveFilePrivate(pos, size) )
{
}

KArchiveFile::~KArchiveFile()
{
    delete d;
}

qint64 KArchiveFile::position() const
{
  return d->pos;
}

qint64 KArchiveFile::size() const
{
  return d->size;
}

void KArchiveFile::setSize( qint64 s )
{
    d->size = s;
}

QByteArray KArchiveFile::data() const
{
  bool ok = archive()->device()->seek( d->pos );
  if (!ok) {
      //qWarning() << "Failed to sync to" << d->pos << "to read" << name();
  }

  // Read content
  QByteArray arr;
  if ( d->size )
  {
    arr = archive()->device()->read( d->size );
    Q_ASSERT( arr.size() == d->size );
  }
  return arr;
}

QIODevice * KArchiveFile::createDevice() const
{
  return new KLimitedIODevice( archive()->device(), d->pos, d->size );
}

bool KArchiveFile::isFile() const
{
    return true;
}

void KArchiveFile::copyTo(const QString& dest) const
{
  QFile f( dest + QLatin1Char('/')  + name() );
  if ( f.open( QIODevice::ReadWrite | QIODevice::Truncate ) )
  {
      QIODevice* inputDev = createDevice();

      // Read and write data in chunks to minimize memory usage
      const qint64 chunkSize = 1024 * 1024;
      qint64 remainingSize = d->size;
      QByteArray array;
      array.resize( int( qMin( chunkSize, remainingSize ) ) );

      while ( remainingSize > 0 ) {
          const qint64 currentChunkSize = qMin( chunkSize, remainingSize );
          const qint64 n = inputDev->read( array.data(), currentChunkSize );
          Q_ASSERT( n == currentChunkSize );
          f.write( array.data(), currentChunkSize );
          remainingSize -= currentChunkSize;
      }
      f.close();

      delete inputDev;
  }
}

////////////////////////////////////////////////////////////////////////
//////////////////////// KArchiveDirectory /////////////////////////////////
////////////////////////////////////////////////////////////////////////

class KArchiveDirectoryPrivate
{
public:
    ~KArchiveDirectoryPrivate()
    {
        qDeleteAll(entries);
    }
    QHash<QString, KArchiveEntry *> entries;
};

KArchiveDirectory::KArchiveDirectory( KArchive* t, const QString& name, int access,
                              int date,
                              const QString& user, const QString& group,
                              const QString &symlink)
  : KArchiveEntry( t, name, access, date, user, group, symlink ),
    d( new KArchiveDirectoryPrivate )
{
}

KArchiveDirectory::~KArchiveDirectory()
{
  delete d;
}

QStringList KArchiveDirectory::entries() const
{
  return d->entries.keys();
}

const KArchiveEntry* KArchiveDirectory::entry( const QString& _name ) const
{
    QString name = QDir::cleanPath(_name);
    int pos = name.indexOf( QLatin1Char('/') );
  if ( pos == 0 ) // ouch absolute path (see also KArchive::findOrCreate)
  {
    if (name.length()>1)
    {
      name = name.mid( 1 ); // remove leading slash
      pos = name.indexOf( QLatin1Char('/') ); // look again
    }
    else // "/"
      return this;
  }
  // trailing slash ? -> remove
  if ( pos != -1 && pos == name.length()-1 )
  {
    name = name.left( pos );
    pos = name.indexOf( QLatin1Char('/') ); // look again
  }
  if ( pos != -1 )
  {
    const QString left = name.left(pos);
    const QString right = name.mid(pos + 1);

    //qDebug() << "left=" << left << "right=" << right;

    const KArchiveEntry* e = d->entries.value( left );
    if ( !e || !e->isDirectory() )
      return 0;
    return static_cast<const KArchiveDirectory*>(e)->entry( right );
  }

  return d->entries.value( name );
}

void KArchiveDirectory::addEntry( KArchiveEntry* entry )
{
  if( entry->name().isEmpty() )
    return;

  if( d->entries.value( entry->name() ) ) {
      /*qWarning() << "directory " << name()
                  << "has entry" << entry->name() << "already";*/
      return;
  }
  d->entries.insert( entry->name(), entry );
}

bool KArchiveDirectory::isDirectory() const
{
    return true;
}

static bool sortByPosition( const KArchiveFile* file1, const KArchiveFile* file2 ) {
    return file1->position() < file2->position();
}

void KArchiveDirectory::copyTo(const QString& dest, bool recursiveCopy ) const
{
  QDir root;

  QList<const KArchiveFile*> fileList;
  QMap<qint64, QString> fileToDir;

  // placeholders for iterated items
  QStack<const KArchiveDirectory *> dirStack;
  QStack<QString> dirNameStack;

  dirStack.push( this );     // init stack at current directory
  dirNameStack.push( dest ); // ... with given path
  do {
    const KArchiveDirectory* curDir = dirStack.pop();
    const QString curDirName = dirNameStack.pop();
    root.mkdir(curDirName);

    const QStringList dirEntries = curDir->entries();
    for ( QStringList::const_iterator it = dirEntries.begin(); it != dirEntries.end(); ++it ) {
      const KArchiveEntry* curEntry = curDir->entry(*it);
      if (!curEntry->symLinkTarget().isEmpty()) {
          const QString linkName = curDirName+QLatin1Char('/')+curEntry->name();
          // To create a valid link on Windows, linkName must have a .lnk file extension.
#ifdef Q_OS_WIN
          if (!linkName.endsWith(".lnk")) {
              linkName += ".lnk";
          }
#endif
          QFile symLinkTarget(curEntry->symLinkTarget());
          if (!symLinkTarget.link(linkName)) {
              //qDebug() << "symlink(" << curEntry->symLinkTarget() << ',' << linkName << ") failed:" << strerror(errno);
          }
      } else {
          if ( curEntry->isFile() ) {
              const KArchiveFile* curFile = dynamic_cast<const KArchiveFile*>( curEntry );
              if (curFile) {
                  fileList.append( curFile );
                  fileToDir.insert( curFile->position(), curDirName );
              }
          }

          if ( curEntry->isDirectory() && recursiveCopy ) {
              const KArchiveDirectory *ad = dynamic_cast<const KArchiveDirectory*>( curEntry );
              if (ad) {
                  dirStack.push( ad );
                  dirNameStack.push( curDirName + QLatin1Char('/') + curEntry->name() );
              }
          }
      }
    }
  } while (!dirStack.isEmpty());

  qSort( fileList.begin(), fileList.end(), sortByPosition );  // sort on d->pos, so we have a linear access

  for ( QList<const KArchiveFile*>::const_iterator it = fileList.constBegin(), end = fileList.constEnd() ;
        it != end ; ++it ) {
      const KArchiveFile* f = *it;
      qint64 pos = f->position();
      f->copyTo( fileToDir[pos] );
  }
}

void KArchive::virtual_hook( int, void* )
{ /*BASE::virtual_hook( id, data )*/; }

void KArchiveEntry::virtual_hook( int, void* )
{ /*BASE::virtual_hook( id, data );*/ }

void KArchiveFile::virtual_hook( int id, void* data )
{ KArchiveEntry::virtual_hook( id, data ); }

void KArchiveDirectory::virtual_hook( int id, void* data )
{ KArchiveEntry::virtual_hook( id, data ); }
