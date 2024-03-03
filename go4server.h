#ifndef GO4SERVER_H
#define GO4SERVER_H

#include <QTcpServer>
#include "acqcontrol.h"


class go4Server : public QTcpServer
{
    Q_OBJECT
public:
    explicit go4Server(QObject *parent = nullptr, acqcontrol *acqCtrl = nullptr);

signals:
    void nofClients_changed(int n);
    void logmessage(QString message);
    void errormessage(QString message);
    void go4event(bufferEvnt hdr, QByteArray buffer);

public slots:
    void decrement_nofClients();
//    void newdata();

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private:

    int nofClients;
    acqcontrol *acqCtrl;
    bufferEvnt hdr;
    QByteArray buffer;
};

#endif // GO4SERVER_H
