#ifndef ACQCONTROL_H
#define ACQCONTROL_H

#include <QObject>
#include <QThread>
#include <QDebug>
#include <QTimer>
#include <QVector>
#include "rootserver.h"

class acqcontrol : public QObject
{
    Q_OBJECT
public:
    explicit acqcontrol(QObject *parent = nullptr);
    ~acqcontrol();

signals:
    void finished();
    void logmessage(QString message);
    void rate(int value);
    void sampleready(QVector<double> x,QVector<double> y);
    void startloop();
    void startserver(acqcontrol* requestor);
    void go4event(bufferEvnt hdr, QVector<int> channels, QVector<quint64> times);
    void nofclients(int n);


public slots:
    void runloop();
    void restart();
    void quit();
    void set_nofsamples(int n){m_nofsamples=n;}
    void set_initpars(init_pars ip){initpars=ip;}
    void set_statuspars(status_pars sp){statuspars=sp;}

private slots:
    void PlotTrigger();
    void RateTrigger();


private:
    bool m_isrunning;
    bool m_restart;
    bool m_samplechanged;
    bool m_timercreated_flag;
    bool m_go4serverstarted_flag;
    int m_count;
    int m_nofsamples;
    init_pars initpars;
    status_pars statuspars;


    int maxfps=60;

    void createtimers();
    void startgo4server();


    QVector<double> x, y; // initialize with entries 0..100


    QTimer *plotTimer;
    QTimer *rateTimer;

    rootserver *server;
    QThread *serverThread;



};

#endif // ACQCONTROL_H
