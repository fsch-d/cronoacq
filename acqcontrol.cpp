#include "acqcontrol.h"
#include <QTimer>
#include <QThread>
#include <QRandomGenerator>
#include <QtMath>
#include <QCoreApplication>
#include "rootserver.h"



acqcontrol::acqcontrol(QObject *parent)
    : QObject{parent}
{
    m_isrunning=false;
    m_count=0;
    m_nofsamples=0;
    m_timercreated_flag=false;
    m_go4serverstarted_flag=false;

    //create and start timers for signals to ui

//    timerPlot->stop();

    QObject::connect(this,&acqcontrol::startloop,this,&acqcontrol::runloop,Qt::QueuedConnection);





   qInfo() << this << "acqCtrl created ...";

}

acqcontrol::~acqcontrol()
{
    qInfo() << this << "acqcontrol destructor ...";


    emit finished();
}

void acqcontrol::createtimers()
{
    if(m_timercreated_flag) return;
    rateTimer = new QTimer(this);
    QObject::connect(rateTimer,&QTimer::timeout,this,&acqcontrol::RateTrigger);
    QObject::connect(this,&acqcontrol::finished, rateTimer, &QTimer::stop, Qt::DirectConnection);
    QObject::connect(this, &acqcontrol::destroyed, rateTimer , &QTimer::deleteLater);
    rateTimer->start(1000);

    plotTimer = new QTimer(this);
    QObject::connect(plotTimer,&QTimer::timeout,this,&acqcontrol::PlotTrigger);
    QObject::connect(this,&acqcontrol::finished, plotTimer, &QTimer::stop, Qt::DirectConnection);
    QObject::connect(this, &acqcontrol::destroyed, plotTimer , &QTimer::deleteLater);
    plotTimer->start(1000/maxfps);

    m_timercreated_flag=true;


    qInfo() << this << "timers created";

}

void acqcontrol::startgo4server()
{
    if (m_go4serverstarted_flag) return;

    //create TServer
    server = new rootserver();
    serverThread = new QThread();
    server->moveToThread(serverThread);

    QObject::connect(this,&acqcontrol::destroyed, serverThread, &QThread::quit);
    QObject::connect(serverThread, &QThread::finished, server, &QObject::deleteLater);
    QObject::connect(this, &acqcontrol::startserver, server, &rootserver::listen);
    QObject::connect(server, &rootserver::logmessage, this, &acqcontrol::logmessage);


    serverThread->start();

    m_go4serverstarted_flag=true;


    emit startserver(this);

    qInfo() << this << "root server for go4 started";

}

void acqcontrol::runloop()
{
    m_isrunning=true;
//    qInfo() << initpars.main.range_start;
    //int j=0;
    emit logmessage("ACQ loop runs!");
    if(!m_timercreated_flag) createtimers();
    if(!m_go4serverstarted_flag) startgo4server();



    while(m_isrunning)
    {

        QThread::usleep(20);
        //qInfo() << this << j++;


        bufferEvnt hdr;
        QVector<int> channels;
        QVector<quint64> times;
        hdr.channel=20;//packet count
        hdr.time=10;//packet timestamp


        for(int i=0; i<hdr.channel; i++)
        {
            int channel=(int)(15*QRandomGenerator::global()->generateDouble());
            quint64 time=1e5-2e5*(int)(15*QRandomGenerator::global()->generateDouble());
            channels.append(channel);
            times.append(time);
        }

        emit go4event(hdr, channels, times);


        int n=50+(int)(30*QRandomGenerator::global()->generateDouble());
        if(m_nofsamples!=0){
            x.fill(0,n);
            y.fill(0,n);
            for (int i=0; i < n; i++) {
                // data with sin + random component
                x[i] = i+10; // x goes from -1 to 1
                y[i] = (-70 * qExp(-qPow(i+QRandomGenerator::global()->generateDouble()-25,2)/qPow(5,2))-15 + (20 * QRandomGenerator::global()->generateDouble()));

            }
        }
        m_count++;
        QCoreApplication::processEvents();

    }


    if(m_restart){
        emit startloop();
    }

    m_count=0;
    m_restart=false;
    emit logmessage("ACQ loop stopped.");
}

void acqcontrol::restart()
{
    qInfo() << this << "restart button pressed";
    m_isrunning=false;
    m_restart=true;
}

void acqcontrol::quit()
{
    qInfo() << this << "stop button pressed";
    m_isrunning=false;
    m_restart=false;
}

void acqcontrol::PlotTrigger()
{
    if(m_isrunning && m_nofsamples!=0)
    {
        emit sampleready(x,y);
        if(m_nofsamples>0) m_nofsamples--;
        //qInfo() << this << "test";
    }
}

void acqcontrol::RateTrigger()
{
    //qInfo() << this << m_count;

    if(m_isrunning)
    {
        emit rate(m_count);
        m_count=0;
    }
    //timerRate->start(1000);
}
