#ifndef GO4CONNECTIONTHREAD_H
#define GO4CONNECTIONTHREAD_H

#include "cronotypes.h"
#include <QObject>
#include <TSocket.h>

class go4connectionThread : public QObject
{
    Q_OBJECT
public:
    explicit go4connectionThread(QObject *parent = nullptr);

    void setSocket(TSocket *newS);

signals:
    void killme();

public slots:
    void senddata(bufferEvnt hdr, QVector<int> channels, QVector<quint64> times);

private:
    TSocket *socket;
    bool m_pipebroken;

};

#endif // GO4CONNECTIONTHREAD_H
