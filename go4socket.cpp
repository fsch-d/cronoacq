#include "go4socket.h"
#include <QTcpSocket>
#include <QThread>
#include <QtEndian>
#include "cronotypes.h"

go4Socket::go4Socket(QObject *parent)
    : QObject{parent}
{
    qInfo() <<  QThread::currentThread() << "Socket created...";
}

void go4Socket::createSocket(int socketDescriptor)
{
    qInfo() <<  QThread::currentThread() << "starting socket thread";

    socket= new QTcpSocket();

    if(!socket->setSocketDescriptor(socketDescriptor))
    {
        emit error(socket->error());
        return;
    }

    connect(socket,&QTcpSocket::readyRead,this,&go4Socket::readyRead,Qt::DirectConnection);
    connect(socket,&QTcpSocket::disconnected,this,&go4Socket::disconnected, Qt::QueuedConnection);
    //connect((go4Server*)server,&go4Server::go4event,this,&go4Socket::sendData);


    qDebug() << socketDescriptor << " Client Connected";
}

go4Socket::~go4Socket()
{

}

void go4Socket::readyRead()
{
    QByteArray Data = socket->readAll();

    qDebug() << socketDescriptor << " Data in: " << Data;

    socket->write(Data);
}

void go4Socket::disconnected()
{
    qDebug() << socketDescriptor << "Client Disconnected";



    socket->deleteLater();
    emit clientDisconnected();

    this->deleteLater();

}

void go4Socket::sendData(int size, QByteArray buffer)
{
    if((size+1)*sizeof(bufferEvnt) != (quint64)buffer.size())
    {
        qInfo() << "buffer size mismatch... data wasn't sent.";
    }
    else if(size>0)
    {
        QByteArray temp;
        QDataStream data(&temp,QIODevice::ReadWrite);
        data << (qint64)buffer.size();
        if(socket->state() == QAbstractSocket::ConnectedState)
        {
            socket->write(temp,8);
            socket->write(buffer,buffer.size());
        }
    }

    //data.setByteOrder(QDataStream::LittleEndian);


  //  qInfo() << temp.toHex() << buffer.size() << size << buffer.size()/sizeof(bufferEvnt);

 //   if(size>0)
 //   {
//        socket->write(temp,8);
//    }
/*
    //qInfo() << QThread::currentThread() << hdr.channel << hdr.time;
    QByteArray header;



    header.append(hdr.channel);
    header.append(hdr.time);

    //qInfo() << header[1];
    //qInfo() << header << header.toHex('\x00') << header;


    quint32 n = 66051;
    quint64 m = 289644378304612875;
    //bool ok;

    char data2[2048];

    for(int i=0; i<2048; i++)
    {
        data2[i]=i;
    }


    QByteArray temp;
    temp.clear();

    temp.setRawData(data2,768);

//    QDataStream data(&temp, QIODevice::ReadWrite);

//    data.setByteOrder(QDataStream::BigEndian);

//    data << n << m;


//    m = 868365761026069268;

    //data << m;

//    temp[0]=0x0A;
//    temp[1]=0x0B;
//    temp[2]=0x0C;
//    temp[3]=0x0D;
//    temp[4]=0x0E;


    socket->write(temp, 256);


//    socket->write(header);
//    socket->write(buffer);
    socket->flush();
    //emit dataRcvd();
*/
}
