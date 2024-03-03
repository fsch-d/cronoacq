#include "go4server.h"
#include "go4socket.h"
#include <QDebug>
#include <QThread>
#include "acqcontrol.h"

go4Server::go4Server(QObject *parent, acqcontrol *acqCtrl)
    : QTcpServer{parent}, acqCtrl(acqCtrl)
{
    qInfo() << QThread::currentThread() << "server constructor is actually executed";
    nofClients=0;
}

void go4Server::decrement_nofClients()
{
    nofClients--;
    emit nofClients_changed(nofClients);
    emit logmessage("client disconnected.");
}

void go4Server::incomingConnection(qintptr socketDescriptor)
{
    qDebug() <<  QThread::currentThread() << socketDescriptor << " Connecting...";
    go4Socket *go4socket = new go4Socket();
    QThread *thread = new QThread();

    go4socket->moveToThread(thread);
    connect(go4socket, &go4Socket::clientDisconnected, this, &go4Server::decrement_nofClients, Qt::DirectConnection);
    connect(go4socket,&go4Socket::clientDisconnected,thread,&QThread::quit);
    connect(thread,&QThread::finished,go4socket,&go4Socket::deleteLater);
    connect(thread,&QThread::finished,thread,&QThread::deleteLater);
    //connect(thread, &go4Socket::finished,thread, &go4Socket::deleteLater);

    thread->start();

    QMetaObject::invokeMethod(go4socket, "createSocket", Q_ARG(int, socketDescriptor));
    connect(acqCtrl,&acqcontrol::go4event,go4socket,&go4Socket::sendData,Qt::QueuedConnection);



    nofClients++;
    emit nofClients_changed(nofClients);
    emit logmessage(tr("client connected ..."));
}
