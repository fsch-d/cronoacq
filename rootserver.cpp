#include "rootserver.h"

//root classes
#include "Riostream.h"
#include <TServerSocket.h>
#include <TSocket.h>
#include <QThread>
#include <QDebug>
#include <QCoreApplication>
#include "go4connectionthread.h"
#include "acqcontrol.h"

rootserver::rootserver(QObject *parent)
    : QObject{parent}
{
    m_listen=true;
    nofclients=0;
}

rootserver::~rootserver()
{

}

void rootserver::setListen(bool newListen)
{
    m_listen = newListen;
}

void rootserver::listen(QObject *requestor)
{
TServerSocket *ss = new TServerSocket(6004, kTRUE);

    ss->SetOption(kNoBlock, 1);

QObject::connect(this, &rootserver::updatenofclients,(acqcontrol*) requestor,&acqcontrol::nofclients);

    while(m_listen)
    {
        QCoreApplication::processEvents();
        QThread::sleep(1);
        TSocket* s0;

        s0 = ss->Accept();
        if(reinterpret_cast<intptr_t>(s0)!=-1){
            qInfo() << "conection established" << s0;
            go4connectionThread *go4connection = new go4connectionThread();
            QThread *thread = new QThread;

            //QObject::connect(go4connection,&go4connectionThread::killme,thread,&QThread::quit);
            //QObject::connect(go4connection,&go4connectionThread::killme,thread,&QThread::deleteLater);
            //QObject::connect(this,&QObject::destroyed,thread,&QObject::deleteLater);
            QObject::connect(go4connection,&QObject::destroyed,this,&rootserver::connectionlost);
            QObject::connect(thread, &QThread::finished, go4connection, &QObject::deleteLater);


            QObject::connect((acqcontrol*) requestor,&acqcontrol::go4event,go4connection,&go4connectionThread::senddata);
            go4connection->moveToThread(thread);
            go4connection->setSocket(s0);

            thread->start();
            nofclients++;
            emit updatenofclients(nofclients);
        }
        //else qInfo() << "no conection";



    }
    ss->Close();

    delete ss;

}

void rootserver::connectionlost()
{
    nofclients--;
    emit updatenofclients(nofclients);
}
