#include "go4connectionthread.h"
#include "cronotypes.h"

#include <QDebug>
#include <QThread>

go4connectionThread::go4connectionThread(QObject *parent)
    : QObject{parent}
{
    qInfo() << this << "client connected";
    m_pipebroken=false;
}

void go4connectionThread::senddata(bufferEvnt hdr, QVector<int> channels, QVector<quint64> times)
{
    if(m_pipebroken) return;
    int Flag_end = 0;
    int connectflag = socket->SendRaw(&Flag_end, 4);
    if(connectflag<0){
        qInfo() << this << "socket broken";
        m_pipebroken=true;
        //emit killme();
        QThread::currentThread()->quit();
        QThread::currentThread()->deleteLater();
        qInfo() << this << "delete signal sent";
        return;
    }

    bufferEvnt go4event[256];

    for(int i=0; i<hdr.channel; i++)
    {
        go4event[i].channel=channels[i];
        go4event[i].time=times[i];
    }

    socket->SendRaw(&hdr,sizeof(bufferEvnt));
    socket->SendRaw(&go4event, sizeof(bufferEvnt)*hdr.channel);

}

void go4connectionThread::setSocket(TSocket *newS)
{
    socket = newS;
}
