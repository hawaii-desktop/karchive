/* This file is part of the KDE libraries
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
#ifndef KARCHIVEHANDLERPLUGIN_H
#define KARCHIVEHANDLERPLUGIN_H

#include <QtCore/qplugin.h>

#include <karchive_export.h>

class KArchiveHandler;

struct KARCHIVE_EXPORT KArchiveHandlerFactoryInterface {
    virtual ~KArchiveHandlerFactoryInterface() {}

    virtual KArchiveHandler *create( const QString &mimeType ) = 0;
    virtual QStringList mimeTypes() const = 0;
};

#define KArchiveHandlerFactoryInterface_iid "org.kde.KArchiveHandlerFactoryInterface"

Q_DECLARE_INTERFACE(KArchiveHandlerFactoryInterface, KArchiveHandlerFactoryInterface_iid)

/**
 * KArchiveHandlerPlugin is a base class for archive handlers plugins.
 * @short base class for all archive handlers plugins
 * @author Pier Luigi Fiorini <pierluigi.fiorini@gmail.com>
 */
class KARCHIVE_EXPORT KArchiveHandlerPlugin : public QObject, public KArchiveHandlerFactoryInterface
{
    Q_OBJECT
    Q_INTERFACES(KArchiveHandlerFactoryInterface)
public:
    /**
     * Constructor.
     */
    KArchiveHandlerPlugin( QObject *parent = 0 );

    /**
     * Destructor.
     */
    virtual ~KArchiveHandlerPlugin();

    /**
     * Returns the list of supported MIME Types.
     *
     * @see create
     */
    virtual QStringList mimeTypes() const = 0;

    /**
     * Creates and returns a KArchiveHandler object for the given MIME Type
     * If a plugin cannot create an archive handler, it should return 0 instead.
     *
     * @param mimeType the MIME Type.
     *
     * @see keys
     */
    KArchiveHandler *create( const QString &mimeType ) = 0;
};

#endif // KARCHIVEHANDLERPLUGIN_H
