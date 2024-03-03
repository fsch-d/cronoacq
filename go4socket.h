#ifndef GO4SOCKET_H
#define GO4SOCKET_H

#include <QThread>
#include <QTcpSocket>

class go4Socket : public QObject
{
    Q_OBJECT
public:
    explicit go4Socket(QObject *parent = nullptr);
    ~go4Socket();

signals:
    void error(QTcpSocket::SocketError socketerror);
    void clientDisconnected();
    void dataRcvd();

public slots:
    void readyRead();
    void disconnected();
    void sendData(int size, QByteArray bffr);
    void createSocket(int socketDescriptor);

//private slots:
//    void write();


private:
    QTcpSocket *socket;
    int socketDescriptor;
};

#endif // GO4SOCKET_H
