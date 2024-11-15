#ifndef ACQCONTROL_H
#define ACQCONTROL_H

#include <QObject>
#include <QThread>
#include <QDebug>
#include <QTimer>
#include <QVector>
#include <QFile>
#include "Ndigo_common_interface.h"
#include "Ndigo_interface.h"
#include "Ndigo250M_interface.h"
#include "cronotypes.h"


#define BUFFER_SIZE_250M (1 << 21)		//    2 MB
#define BUFFER_SIZE_5G (1 << 23)

class acqcontrol : public QObject
{
    Q_OBJECT
public:
    explicit acqcontrol(QObject *parent = nullptr);
    ~acqcontrol();


signals:
    void finished();
    void logmessage(QString message);
    void errormessage(QString message);
    void rate(int value);
    void sampleready(QVector<double> x,QVector<double> y);
    void startloop();
    void go4event(int size, QByteArray buffer);
    void nofclients(int n);
    void closeDataFile();


public slots:
    int initCards();
    void restart();
    void quit();
    void set_nofsamples(int n){m_nofsamples=n;}
    void set_initpars(init_pars ip){initpars=ip;}
    void set_statuspars(status_pars sp){statuspars=sp;}
    void nofClients_changed(int n){nofClients=n;}
    void writeDatatoFile(QFile* dataFile);
    void stopWritingtoFile();

private slots:
    void acqloop();
    void PlotTrigger();
    void RateTrigger();


private:
    int initAcq();
    void ReadAdvConfig();
    void createtimers();

    static qint64 time_CF(short* data_buf, int size);


    int maxfps=60;

    bool m_isrunning;
    bool m_restart;
    bool m_sampleNow;
    bool m_timercreated_flag;
    bool m_writetofile;
    bool m_isinitialized;
    int m_count;
    int m_nofsamples;

    int ndigoCount;
    int ndigo250mCount;
    int nofClients;

    int filesize;

    init_pars initpars;
    status_pars statuspars;


    ndigo_device* ndigos[NR_CARDS];
    ndigo_static_info ndigo_info[NR_CARDS];
    ndigo_param_info ndigo_paramInfo[NR_CARDS];


    ndigo_configuration conf[NR_CARDS - NR_250MCARDS];
    ndigo250m_configuration conf250m[NR_250MCARDS];



    //own event buffer
    evnt_group event;




    QVector<double> x, y;


    QTimer *plotTimer;
    QTimer *rateTimer;
    QFile *sFile;

    //QThread *serverThread;



};

#endif // ACQCONTROL_H
