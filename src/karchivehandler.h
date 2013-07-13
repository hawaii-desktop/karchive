/* This file is part of the KDE libraries
   Copyright (C) 2000-2005 David Faure <faure@kde.org>
   Copyright (C) 2003 Leo Savernik <l.savernik@aon.at>
   Copyright (C) 2013 Pier Luigi Fiorini <pierluigi.fiorini@gmail.com>

   Moved from ktar.h by Roberto Teixeira <maragato@kde.org>

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
#ifndef KARCHIVEHANDLER_H
#define KARCHIVEHANDLER_H

#include <sys/stat.h>
#include <sys/types.h>

#include <QtCore/QDate>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QHash>

#include <karchive_export.h>

class KArchive;
class KArchiveDirectory;
class KArchiveFile;

class KArchiveHandlerPrivate;

/**
 * KArchiveHandler is a base class for archive handlers.
 * @short base class for all archive handlers
 * @author Pier Luigi Fiorini <pierluigi.fiorini@gmail.com>
 * @author David Faure <faure@kde.org>
 */
class KARCHIVE_EXPORT KArchiveHandler
{
public:
    /**
     * Constructor.
     * @param mimeType is archive MIME Type.
     */
    KArchiveHandler( const QString& mimeType );

    /**
     * Destructor.
     */
    virtual ~KArchiveHandler();

    /**
     * Returns the pointer to the KArchive object that loaded this
     * archive handler.
     */
    KArchive *archive() const;

    /**
     * Opens the archive for reading or writing.
     * Inherited classes might want to reimplement openArchive instead.
     * @param mode may be QIODevice::ReadOnly or QIODevice::WriteOnly
     * @see close
     */
    virtual bool open( QIODevice::OpenMode mode );

    /**
     * Closes the archive.
     * Inherited classes might want to reimplement closeArchive instead.
     *
     * @return true if close succeeded without problems
     * @see open
     */
    virtual bool close();

    /**
     * Checks whether the archive is open.
     * @return true if the archive is opened
     */
    bool isOpen() const;

    /**
     * The name of the archive file, as passed to the KArchive constructor that takes a
     * fileName, or an empty string if you used the QIODevice constructor.
     * @return the name of the file, or QString() if unknown
     */
    QString fileName() const;

    /**
     * Returns whether the device is owned.
     */
    bool isDeviceOwned() const;

    /**
     * Returns the mode in which the archive was opened
     * @return the mode in which the archive was opened (QIODevice::ReadOnly or QIODevice::WriteOnly)
     * @see open()
     */
    QIODevice::OpenMode mode() const;

    /**
     * The underlying device.
     * @return the underlying device.
     */
    QIODevice * device() const;

    /**
     * Retrieves or create the root directory.
     * The default implementation assumes that openArchive() did the parsing,
     * so it creates a dummy rootdir if none was set (write mode, or no '/' in the archive).
     * Reimplement this to provide parsing/listing on demand.
     * @return the root directory
     */
    virtual KArchiveDirectory* rootDir();

    /**
     * Opens an archive for reading or writing.
     * Called by open.
     * @param mode may be QIODevice::ReadOnly or QIODevice::WriteOnly
     */
    virtual bool openArchive( QIODevice::OpenMode mode ) = 0;

    /**
     * Closes the archive.
     * Called by close.
     */
    virtual bool closeArchive() = 0;

    /**
     * Write a directory to the archive.
     * This virtual method must be implemented by subclasses.
     *
     * Depending on the archive type not all metadata might be used.
     *
     * @param name the name of the directory
     * @param user the user that owns the directory
     * @param group the group that owns the directory
     * @param perm permissions of the directory. Use 040755 if you don't have any other information.
     * @param atime time the file was last accessed
     * @param mtime modification time of the file
     * @param ctime time of last status change
     * @see writeDir
     */
    virtual bool doWriteDir( const QString& name, const QString& user, const QString& group,
                             mode_t perm, time_t atime, time_t mtime, time_t ctime ) = 0;

    /**
     * Writes a symbolic link to the archive.
     * This virtual method must be implemented by subclasses.
     *
     * @param name name of symbolic link
     * @param target target of symbolic link
     * @param user the user that owns the directory
     * @param group the group that owns the directory
     * @param perm permissions of the directory
     * @param atime time the file was last accessed
     * @param mtime modification time of the file
     * @param ctime time of last status change
     * @see writeSymLink
     */
    virtual bool doWriteSymLink(const QString &name, const QString &target,
                                const QString &user, const QString &group,
                                mode_t perm, time_t atime, time_t mtime, time_t ctime) = 0;

    /**
     * This virtual method must be implemented by subclasses.
     *
     * Depending on the archive type not all metadata might be used.
     *
     * @param name the name of the file
     * @param user the user that owns the file
     * @param group the group that owns the file
     * @param size the size of the file
     * @param perm permissions of the file. Use 0100644 if you don't have any more specific permissions to set.
     * @param atime time the file was last accessed
     * @param mtime modification time of the file
     * @param ctime time of last status change
     * @see prepareWriting
     */
    virtual bool doPrepareWriting( const QString& name, const QString& user,
                                   const QString& group, qint64 size, mode_t perm,
                                   time_t atime, time_t mtime, time_t ctime ) = 0;

    /**
     * Called after writing the data.
     * This virtual method must be implemented by subclasses.
     *
     * @param size the size of the file
     * @see finishWriting()
     */
    virtual bool doFinishWriting( qint64 size ) = 0;

    /**
     * Write data into the current file - to be called after calling KArchive::prepareWriting()
     */
    virtual bool writeData( const char* data, qint64 size );

protected:
    /**
     * Ensures that @p path exists, create otherwise.
     * This handles e.g. tar files missing directory entries, like mico-2.3.0.tar.gz :)
     * @param path the path of the directory
     * @return the directory with the given @p path
     */
    KArchiveDirectory * findOrCreate( const QString & path );

    /**
     * Can be reimplemented in order to change the creation of the device
     * (when using the fileName constructor). By default this method uses
     * QSaveFile when saving, and a simple QFile on reading.
     * This method is called by open().
     */
    virtual bool createDevice( QIODevice::OpenMode mode );

protected:
    friend class KArchive;

    /**
     * Saves a pointer to the KArchive object that loaded this
     * archive handler.  It will be used later to create the
     * root directory.
     *
     * \sa rootDir()
     */
    void setArchive( KArchive *archive );

    /**
     * Sets the name of the archive file.
     */
    void setFileName( const QString &fileName );

    /**
     * Can be called by derived classes in order to set the underlying device.
     * Note that KArchiveHandler will -not- own the device, it must be deleted by the derived class.
     */
    void setDevice( QIODevice *dev );

    /**
     * Derived classes call setRootDir from openArchive,
     * to set the root directory after parsing an existing archive.
     */
    void setRootDir( KArchiveDirectory *rootDir );

    void abortWriting();

protected:
    virtual void virtual_hook( int id, void* data );
private:
    KArchiveHandlerPrivate* const d;
};

#endif // KARCHIVEHANDLER_H
