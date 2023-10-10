#ifndef ROOTSERVER_H
#define ROOTSERVER_H

#include <QObject>
#include <QThread>
#include "cronotypes.h"

class rootserver : public QObject
{
    Q_OBJECT
public:
    explicit rootserver(QObject *parent = nullptr);
    ~rootserver();

    void setListen(bool newListen);

public slots:
    void listen(QObject *requestor);
    void connectionlost();

signals:
    void sendconnectflag();
    void updatenofclients(int n);

private:
    bool m_listen;
    int nofclients;

};

#endif // ROOTSERVER_H
