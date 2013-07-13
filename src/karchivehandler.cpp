/* This file is part of the KDE libraries
   Copyright (C) 2000-2005 David Faure <faure@kde.org>
   Copyright (C) 2003 Leo Savernik <l.savernik@aon.at>
   Copyright (C) 2013 Pier Luigi Fiorini <pierluigi.fiorini@gmail.com>

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

#include <qsavefile.h>

#include <QtCore/QFile>

#include "karchive.h"
#include "karchivehandler.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>

class KArchiveHandlerPrivate
{
public:
    KArchiveHandlerPrivate()
        : archive( 0 )
        , rootDir( 0 )
        , saveFile( 0 )
        , dev( 0 )
        , fileName()
        , mode( QIODevice::NotOpen )
        , deviceOwned( false )
    {}
    ~KArchiveHandlerPrivate()
    {
        delete saveFile;
        delete rootDir;
    }

    void abortWriting();

    KArchive *archive;
    KArchiveDirectory *rootDir;
    QSaveFile *saveFile;
    QIODevice *dev;
    QString fileName;
    QIODevice::OpenMode mode;
    bool deviceOwned;
};

void KArchiveHandlerPrivate::abortWriting()
{
    if ( saveFile ) {
        saveFile->cancelWriting();
        delete saveFile;
        saveFile = 0;
        dev = 0;
    }
}

////////////////////////////////////////////////////////////////////////
//////////////////////// KArchiveHandler ///////////////////////////////
////////////////////////////////////////////////////////////////////////

KArchiveHandler::KArchiveHandler( const QString &mimeType )
	: d(new KArchiveHandlerPrivate)
{
}

KArchiveHandler::~KArchiveHandler()
{
}

KArchive *KArchiveHandler::archive() const
{
    return d->archive;
}

void KArchiveHandler::setArchive( KArchive *archive )
{
    d->archive = archive;
}

bool KArchiveHandler::open( QIODevice::OpenMode mode )
{
    Q_ASSERT( mode != QIODevice::NotOpen );

    if ( isOpen() )
        close();

    if ( !d->fileName.isEmpty() )
    {
        Q_ASSERT( !d->dev );
        if ( !createDevice( mode ) )
            return false;
    }

    Q_ASSERT( d->dev );

    if ( !d->dev->isOpen() && !d->dev->open( mode ) )
        return false;

    d->mode = mode;

    Q_ASSERT( !d->rootDir );
    d->rootDir = 0;

    return openArchive( mode );
}

bool KArchiveHandler::close()
{
    if ( !isOpen() )
        return false; // already closed (return false or true? arguable...)

    // moved by holger to allow kzip to write the zip central dir
    // to the file in closeArchive()
    // DF: added d->dev so that we skip closeArchive if saving aborted.
    bool closeSucceeded = true;
    if ( d->dev ) {
        closeSucceeded = closeArchive();
        if ( d->mode == QIODevice::WriteOnly && !closeSucceeded )
            d->abortWriting();
    }

    if (d->dev && d->dev != d->saveFile) {
        d->dev->close();
    }

    // if d->saveFile is not null then it is equal to d->dev.
    if (d->saveFile) {
        closeSucceeded = d->saveFile->commit();
        delete d->saveFile;
        d->saveFile = 0;
    } if (d->deviceOwned) {
        delete d->dev; // we created it ourselves in open()
    }

    delete d->rootDir;
    d->rootDir = 0;
    d->mode = QIODevice::NotOpen;
    d->dev = 0;
    return closeSucceeded;
}

bool KArchiveHandler::isOpen() const
{
    return d->mode != QIODevice::NotOpen;
}

QString KArchiveHandler::fileName() const
{
    return d->fileName;
}

void KArchiveHandler::setFileName( const QString &fileName )
{
    if (!fileName.isEmpty() && d->fileName != fileName)
        d->fileName = fileName;
}

bool KArchiveHandler::isDeviceOwned() const
{
    return d->deviceOwned;
}

QIODevice::OpenMode KArchiveHandler::mode() const
{
    return d->mode;
}

QIODevice * KArchiveHandler::device() const
{
    return d->dev;
}

void KArchiveHandler::setDevice( QIODevice * dev )
{
    if ( d->deviceOwned )
        delete d->dev;
    d->dev = dev;
    d->deviceOwned = false;
}

KArchiveDirectory * KArchiveHandler::rootDir()
{
    if ( !d->rootDir )
    {
        //qDebug() << "Making root dir ";
        struct passwd* pw =  getpwuid( getuid() );
        struct group* grp = getgrgid( getgid() );
        QString username = pw ? QFile::decodeName(pw->pw_name) : QString::number( getuid() );
        QString groupname = grp ? QFile::decodeName(grp->gr_name) : QString::number( getgid() );

        d->rootDir = new KArchiveDirectory( d->archive, QLatin1String("/"), (int)(0777 + S_IFDIR), 0, username, groupname, QString() );
    }
    return d->rootDir;
}

void KArchiveHandler::setRootDir( KArchiveDirectory *rootDir )
{
    Q_ASSERT( !d->rootDir ); // Call setRootDir only once during parsing please ;)
    d->rootDir = rootDir;
}

bool KArchiveHandler::writeData( const char* data, qint64 size )
{
    bool ok = device()->write( data, size ) == size;
    if ( !ok )
        d->abortWriting();
    return ok;
}

KArchiveDirectory * KArchiveHandler::findOrCreate( const QString & path )
{
    //qDebug() << path;
    if ( path.isEmpty() || path == QLatin1String("/") || path == QLatin1String(".") ) // root dir => found
    {
        //qDebug() << "returning rootdir";
        return rootDir();
    }
    // Important note : for tar files containing absolute paths
    // (i.e. beginning with "/"), this means the leading "/" will
    // be removed (no KDirectory for it), which is exactly the way
    // the "tar" program works (though it displays a warning about it)
    // See also KArchiveDirectory::entry().

    // Already created ? => found
    const KArchiveEntry* ent = rootDir()->entry( path );
    if ( ent )
    {
        if ( ent->isDirectory() )
            //qDebug() << "found it";
            return (KArchiveDirectory *) ent;
        else {
            //qWarning() << "Found" << path << "but it's not a directory";
        }
    }

    // Otherwise go up and try again
    int pos = path.lastIndexOf( QLatin1Char('/') );
    KArchiveDirectory * parent;
    QString dirname;
    if ( pos == -1 ) // no more slash => create in root dir
    {
        parent =  rootDir();
        dirname = path;
    }
    else
    {
        QString left = path.left( pos );
        dirname = path.mid( pos + 1 );
        parent = findOrCreate( left ); // recursive call... until we find an existing dir.
    }

    //qDebug() << "found parent " << parent->name() << " adding " << dirname << " to ensure " << path;
    // Found -> add the missing piece
    KArchiveDirectory * e = new KArchiveDirectory( d->archive, dirname, d->rootDir->permissions(),
                                                   d->rootDir->date(), d->rootDir->user(),
                                                   d->rootDir->group(), QString() );
    parent->addEntry( e );
    return e; // now a directory to <path> exists
}

bool KArchiveHandler::createDevice( QIODevice::OpenMode mode )
{
    switch( mode ) {
    case QIODevice::WriteOnly:
        if ( !d->fileName.isEmpty() ) {
            // The use of QSaveFile can't be done in the ctor (no mode known yet)
            //qDebug() << "Writing to a file using QSaveFile";
            d->saveFile = new QSaveFile(d->fileName);
            if ( !d->saveFile->open(QIODevice::WriteOnly) ) {
                //qWarning() << "QSaveFile creation for " << d->fileName << " failed, " << d->saveFile->errorString();
                delete d->saveFile;
                d->saveFile = 0;
                return false;
            }
            d->dev = d->saveFile;
            Q_ASSERT( d->dev );
        }
        break;
    case QIODevice::ReadOnly:
    case QIODevice::ReadWrite:
        // ReadWrite mode still uses QFile for now; we'd need to copy to the tempfile, in fact.
        if ( !d->fileName.isEmpty() ) {
            d->dev = new QFile( d->fileName );
            d->deviceOwned = true;
        }
        break; // continued below
    default:
        //qWarning() << "Unsupported mode " << d->mode;
        return false;
    }
    return true;
}

void KArchiveHandler::abortWriting()
{
    d->abortWriting();
}

void KArchiveHandler::virtual_hook( int, void* )
{ /*BASE::virtual_hook( id, data )*/; }
