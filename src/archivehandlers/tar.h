/* This file is part of the KDE libraries
   Copyright (C) 2000-2005 David Faure <faure@kde.org>
   Copyright (C) 2003 Leo Savernik <l.savernik@aon.at>
   Copyright (C) 2013 Pier Luigi Fiorini <pierluigi.fiorini@gmail.com>

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
#ifndef TAR_H
#define TAR_H

#include <karchivehandler.h>

/**
 * A class for reading / writing (optionally compressed) tar archives.
 *
 * TarHandler allows you to read and write tar archives, including those
 * that are compressed using gzip, bzip2 or xz.
 *
 * @author Torben Weis <weis@kde.org>, David Faure <faure@kde.org>
 */
class KARCHIVE_EXPORT TarHandler : public KArchiveHandler
{
public:
    /**
     * Creates an instance that operates on the given filename
     * using the compression filter associated to given mimetype.
     *
     * @param filename is a local path (e.g. "/home/weis/myfile.tgz")
     * @param mimetype "application/x-gzip", "application/x-bzip" or
     * "application/x-xz"
     * Do not use application/x-compressed-tar or similar - you only need to
     * specify the compression layer !  If the mimetype is omitted, it
     * will be determined from the filename.
     */
    explicit TarHandler( const QString &mimeType );

    /**
     * If the tar ball is still opened, then it will be
     * closed automatically by the destructor.
     */
    virtual ~TarHandler();

    /**
     * Special function for setting the "original file name" in the gzip header,
     * when writing a tar.gz file. It appears when using in the "file" command,
     * for instance. Should only be called if the underlying device is a KFilterDev!
     * @param fileName the original file name
     */
    void setOrigFileName( const QByteArray & fileName );

protected:

    /// Reimplemented from KArchive
    virtual bool doWriteSymLink(const QString &name, const QString &target,
                                const QString &user, const QString &group,
                                mode_t perm, time_t atime, time_t mtime, time_t ctime);
    /// Reimplemented from KArchive
    virtual bool doWriteDir( const QString& name, const QString& user, const QString& group,
                             mode_t perm, time_t atime, time_t mtime, time_t ctime );
    /// Reimplemented from KArchive
    virtual bool doPrepareWriting( const QString& name, const QString& user,
                                   const QString& group, qint64 size, mode_t perm,
                                   time_t atime, time_t mtime, time_t ctime );
    /// Reimplemented from KArchive
    virtual bool doFinishWriting( qint64 size );

    /**
     * Opens the archive for reading.
     * Parses the directory listing of the archive
     * and creates the KArchiveDirectory/KArchiveFile entries.
     * @param mode the mode of the file
     */
    virtual bool openArchive( QIODevice::OpenMode mode );
    virtual bool closeArchive();

    virtual bool createDevice( QIODevice::OpenMode mode );

protected:
    virtual void virtual_hook( int id, void* data );
private:
    class TarHandlerPrivate;
    TarHandlerPrivate* const d;
};

#endif
