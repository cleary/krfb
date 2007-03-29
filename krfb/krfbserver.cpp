//
// C++ Implementation: krfbserver
//
// Description:
//
//
// Author: Alessandro Praduroux <pradu@pradu.it>, (C) 2007
//
// Copyright: See COPYING file that comes with this distribution
//
//
#include "krfbserver.h"
#include "krfbserver.moc"

//#include "rfbcontroller.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <KConfig>
#include <KGlobal>

const int DEFAULT_TCP_PORT = 5900;

KrfbServer::KrfbServer()
    : QObject(0), _controller(0) //new RFBController(0))
{
    QTimer::singleShot(0, this, SLOT(startListening()));

    // TESTING!!!
    QTimer::singleShot(100000, this, SLOT(disconnectAndQuit()));
}

void KrfbServer::startListening() {

    KSharedConfigPtr conf = KGlobal::config();
    KConfigGroup tcpConfig(conf, "TCP");

    int port = tcpConfig.readEntry("port",DEFAULT_TCP_PORT);

    _server = new QTcpServer(this);
    connect(_server,SIGNAL(newConnection()),SLOT(newConnection()));

    if (!_server->listen(QHostAddress::Any, port)) {
        kDebug() << "server listen error" << endl;
        deleteLater();
        return;
    }
}


KrfbServer::~KrfbServer()
{
    //delete _controller;
}

void KrfbServer::newConnection()
{
    int fdNum = 0;
    QTcpSocket *conn = _server->nextPendingConnection();
    QString peer = conn->peerAddress().toString();
    emit sessionEstablished(peer);
    connect (conn, SIGNAL(disconnected()), SIGNAL(sessionFinished()));

    fdNum = conn->socketDescriptor();
    conn->close();
    //_controller->startServer(fdNum);
}

void KrfbServer::enableDesktopControl(bool enable)
{
    // TODO
}

void KrfbServer::disconnectAndQuit()
{
    // TODO: cleanup of existing connections
    _server->close();
    emit quitApp();
}


