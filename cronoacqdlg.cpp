#include "cronoacqdlg.h"
#include "ui_cronoacqdlg.h"
#include <QDateTime>
#include <QDebug>
#include <QThread>
#include <QMetaType>
#include <QDataStream>
#include <QtNetwork>

#include <QFile>
#include "qtvariantproperty.h"
#include "qttreepropertybrowser.h"

#include "qcustomplot.h"
#include "treemodel.h"

#include "go4server.h"


cronoacqDlg::cronoacqDlg(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::cronoacqDlg)
{
    ui->setupUi(this);

    qRegisterMetaType<init_pars>();


    //Load settings from QSettings
    QSettings settings("Fischerlab", "cronoACQ4.1");
    restoreState(settings.value("windowState").toByteArray());
    initpars = settings.value("initpars").value<init_pars>();



    //some ui stuff
    setCentralWidget(ui->TracedockWidget);
    ui->actionpause_sampling->setEnabled(false);
    ui->actionrestart_acquisition->setEnabled(false);
    ui->actionstop_acquisition->setEnabled(false);
    ui->actionstop_recording->setEnabled(false);
    ui->lcdRate->setAutoFillBackground(true);
    ui->statusbar->addWidget(ui->lcdRate);
    ui->statusbar->addWidget(ui->nofclients);
    if(!initpars.main.advconf) ui->actionadv_configuration->setEnabled(false);


    auto palette = ui->lcdRate->palette();
    palette.setColor(palette.Window, Qt::red);
    palette.setColor(palette.WindowText, Qt::black);
    ui->lcdRate->setPalette(palette);
    ui->lcdRate->setSegmentStyle(QLCDNumber::Flat );

    ui->customPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    ui->customPlot->axisRect()->setRangeZoomAxes(nullptr,ui->customPlot->yAxis);


    //getting propertygrid
    variantManager = new QtVariantPropertyManager();
    variantFactory = new QtVariantEditorFactory();
    initpropertygrid();

    //QObject::connect(ui->variantEditor, &QTreeWidget::itemActivated, this, &cronoacqDlg::on_propertyActivated);


    initplot();
    inittree();
    nofClients_changed(0);


    //getting the threads up and running
    QThread::currentThread()->setObjectName("cronoacqdlg thread");

    acqCtrl = new acqcontrol();
    acqCtrl->set_initpars(initpars);
    acqCtrlThread = new QThread();

    acqCtrl->moveToThread(acqCtrlThread);
    acqCtrl->setObjectName("acqctrlThread");
    acqCtrlThread->start();

    server = new go4Server(nullptr, acqCtrl);


    connect(acqCtrlThread, &QThread::finished, acqCtrl, &QObject::deleteLater);

    //Connect signals and slots, e.g., ui to readout thread
    connect(this,&cronoacqDlg::initparsChanged,acqCtrl,&acqcontrol::set_initpars);
    connect(this,&cronoacqDlg::statusChanged,acqCtrl,&acqcontrol::set_statuspars);
    connect(acqCtrl,&acqcontrol::logmessage,this,&cronoacqDlg::logmessage);
    connect(acqCtrl,&acqcontrol::errormessage,this,&cronoacqDlg::errormessage);
    connect(ui->actionstart_acquistion,&QAction::triggered,acqCtrl,&acqcontrol::startloop);
    connect(ui->actionstop_acquisition,&QAction::triggered,acqCtrl,&acqcontrol::quit,Qt::DirectConnection);
    connect(ui->actionrestart_acquisition,&QAction::triggered,acqCtrl,&acqcontrol::restart,Qt::DirectConnection);
    connect(acqCtrl,&acqcontrol::sampleready,this,&cronoacqDlg::plotsample);
    connect(this,&cronoacqDlg::sample_n,acqCtrl,&acqcontrol::set_nofsamples);
    connect(acqCtrl,&acqcontrol::rate,this,&cronoacqDlg::showrate);
    //connect(acqCtrl,&acqcontrol::nofclients,this,&cronoacqDlg::nofclients);
    connect(server,&go4Server::nofClients_changed,this,&cronoacqDlg::nofClients_changed);
    connect(server,&go4Server::nofClients_changed,acqCtrl,&acqcontrol::nofClients_changed, Qt::DirectConnection);
    connect(server,&go4Server::logmessage,this,&cronoacqDlg::logmessage);
    connect(server,&go4Server::errormessage,this,&cronoacqDlg::errormessage);


    connect(this,&cronoacqDlg::writeDatatoFile,acqCtrl,&acqcontrol::writeDatatoFile);
    connect(ui->actionstop_recording,&QAction::triggered,acqCtrl,&acqcontrol::stopWritingtoFile);
    connect(acqCtrl,&acqcontrol::closeDataFile,this,&cronoacqDlg::closeFile);

    //////////////////////////////////////////////////////////////////
    /// \brief get the tcpip server started for go4
    ///
    QString ipAddress;
    QList<QHostAddress> ipAddressesList = QNetworkInterface::allAddresses();
    // use the first non-localhost IPv4 address
    for (int i = 0; i < ipAddressesList.size(); ++i) {
        if (ipAddressesList.at(i) != QHostAddress::LocalHost &&
            ipAddressesList.at(i).toIPv4Address()) {
            ipAddress = ipAddressesList.at(i).toString();
            break;
        }
    }
    if(!server->listen(QHostAddress::Any,6005))
    {
        QMessageBox::critical(this, tr("cronoacq go4 server"),
                              tr("Unable to start the server: %1.")
                                  .arg(server->errorString()));
        close();
        return;
    }
    logmessage(tr("Server started at %1:%2/tcp").arg(ipAddress).arg(server->serverPort()));

    connect(this,&cronoacqDlg::initCards,acqCtrl,&acqcontrol::initCards);

    emit initCards();

    //qInfo() << this << "All starts here!";


    //



}

cronoacqDlg::~cronoacqDlg()
{

    QSettings settings("Fischerlab", "cronoACQ4.1");
    QVariant v = QVariant::fromValue(initpars);

    settings.setValue("initpars",v);

    if(m_initialize_settings)
    {
        settings.setValue("geometry", saveGeometry());
        settings.setValue("windowState", saveState());
    }



    delete variantManager;
    delete variantFactory;

    delete ui;
}

void cronoacqDlg::logmessage(QString message)
{
    logtime();
    ui->logBrowser->moveCursor(QTextCursor::End);
    ui->logBrowser->insertHtml(message);
}

void cronoacqDlg::errormessage(QString message)
{
    logtime();
    ui->logBrowser->moveCursor(QTextCursor::End);
    message.prepend("<font color=\"Red\">");
    message.append("</font>");
    ui->logBrowser->insertHtml(message);
}

void cronoacqDlg::plotsample(QVector<double> x, QVector<double> y)
{
    ui->customPlot->graph(0)->setData(x, y);
    ui->customPlot->replot();
}

void cronoacqDlg::showrate(int counts)
{
    //qInfo() << this << counts;

    ui->lcdRate->display(counts);
}

void cronoacqDlg::nofClients_changed(int n)
{
    auto noc = QStringLiteral("no of clients: %1").arg(n);
    ui->nofclients->setText(noc);
}

void cronoacqDlg::on_propertyChanged(QtProperty *property, const QVariant &val)
{
    //main properties
    if(property->propertyName()==QString("source card")) initpars.main.sourcecard=val.toInt();
    if(property->propertyName()==QString("source channel")) initpars.main.sourcechan=val.toInt();
    if(property->propertyName()==QString("range start")) initpars.main.range_start=val.toInt();
    if(property->propertyName()==QString("range stop")) initpars.main.range_stop=val.toInt();

    if(property->propertyName()==QString("advanced configuration"))
    {
        initpars.main.advconf=val.toBool();
        if(val.toBool())
        {
            property->subProperties().constFirst()->setEnabled(true);
            ui->actionadv_configuration->setEnabled(true);
        }
        else
        {
            property->subProperties().constFirst()->setEnabled(false);
            ui->actionadv_configuration->setEnabled(false);
        }
    }
    if(property->propertyName()==QString("configuration file")) initpars.main.advfilename=val.toString();


    //card properties
    if(!(statuspars.active_card < 0 || statuspars.active_card >= NR_CARDS))
    {
        if(property->propertyName()==QString("precursor")) initpars.card[statuspars.active_card].cardPars.precursor=val.toInt();
        if(property->propertyName()==QString("length")) initpars.card[statuspars.active_card].cardPars.length=val.toInt();
        if(property->propertyName()==QString("retrigger")) initpars.card[statuspars.active_card].cardPars.retrigger=val.toBool();

        //channel properties
        if(!(statuspars.active_chan < 0 || statuspars.active_chan >= 5))
        {
            if(property->propertyName()==QString("edge")) initpars.card[statuspars.active_card].chan[statuspars.active_chan].edge=val.toBool();
            if(property->propertyName()==QString("rising")) initpars.card[statuspars.active_card].chan[statuspars.active_chan].rising=val.toBool();
            if(property->propertyName()==QString("threshhold")){
                initpars.card[statuspars.active_card].chan[statuspars.active_chan].thresh=val.toInt();
                QVector<double> x({-1000000,1000000}), y({(double)initpars.card[statuspars.active_card].chan[statuspars.active_chan].thresh ,(double)initpars.card[statuspars.active_card].chan[statuspars.active_chan].thresh});
                ui->customPlot->graph(1)->setData(x, y);
                ui->customPlot->replot();
            }
        }
    }

    emit initparsChanged(initpars);

    //qInfo() << "item " << property->propertyName() << "changed to" << val;
}

void cronoacqDlg::on_activeTreeItemChanged(const QModelIndex &index)
{
    statuspars.active_chan=-1;
    statuspars.active_card=-1;
    ui->customPlot->graph(0)->setVisible(false);
    ui->customPlot->graph(1)->setVisible(false);
    if(index.data(Qt::DisplayRole).toString()==QString("card 0")) statuspars.active_card=0;
    else if(index.data(Qt::DisplayRole).toString()==QString("card 1")) statuspars.active_card=1;
    else if(index.data(Qt::DisplayRole).toString()==QString("card 2")) statuspars.active_card=2;
    else
    {
        if(index.parent().data(Qt::DisplayRole).toString()==QString("card 0")) statuspars.active_card=0;
        else if(index.parent().data(Qt::DisplayRole).toString()==QString("card 1")) statuspars.active_card=1;
        else if(index.parent().data(Qt::DisplayRole).toString()==QString("card 2")) statuspars.active_card=2;
        if(index.data(Qt::DisplayRole).toString()==QString("A")) statuspars.active_chan=0;
        else if(index.data(Qt::DisplayRole).toString()==QString("B")) statuspars.active_chan=1;
        else if(index.data(Qt::DisplayRole).toString()==QString("C")) statuspars.active_chan=2;
        else if(index.data(Qt::DisplayRole).toString()==QString("D")) statuspars.active_chan=3;
        else if(index.data(Qt::DisplayRole).toString()==QString("TDC")) statuspars.active_chan=4;
    }


    if(statuspars.active_chan!=-1)
    {
        QVector<double> x({-1000000,1000000}), y({(double)initpars.card[statuspars.active_card].chan[statuspars.active_chan].thresh ,(double)initpars.card[statuspars.active_card].chan[statuspars.active_chan].thresh});
        ui->customPlot->graph(1)->setData(x, y);
        ui->customPlot->graph(0)->setVisible(true);
        ui->customPlot->graph(1)->setVisible(true);
    }

    ui->customPlot->replot();

    initpropertygrid();

    //qInfo() << "item " << index.data(Qt::DisplayRole) << "   " << index.parent().data(Qt::DisplayRole) << "clicked";

    emit statusChanged(statuspars);
}


void cronoacqDlg::logtime()
{
    QDateTime date = QDateTime::currentDateTime();
    QString formattedTime = date.toString("[hh:mm:ss] ");

    ui->logBrowser->setTextColor( QColor( "blue" ) );
    ui->logBrowser->append(formattedTime);
}

void cronoacqDlg::initplot()
{
    ui->customPlot->clearGraphs();
    ui->customPlot->addGraph();


    ui->customPlot->addGraph();
    QPen ThPen;
    ThPen.setStyle(Qt::DotLine);
    ThPen.setColor(Qt::blue);
    ui->customPlot->graph(1)->setPen(ThPen);
    ui->customPlot->graph(1)->setVisible(false);

    ui->customPlot->xAxis2->setVisible(true);
    ui->customPlot->xAxis2->setTickLabels(false);
    ui->customPlot->yAxis2->setVisible(true);
    ui->customPlot->yAxis2->setTickLabels(false);
    connect(ui->customPlot->xAxis, SIGNAL(rangeChanged(QCPRange)), ui->customPlot->xAxis2, SLOT(setRange(QCPRange)));
    connect(ui->customPlot->yAxis, SIGNAL(rangeChanged(QCPRange)), ui->customPlot->yAxis2, SLOT(setRange(QCPRange)));

    // give the axes some labels:   
    ui->customPlot->xAxis->setLabel("time (ns)");
    ui->customPlot->yAxis->setLabel("mV");
    // set axes ranges, so we see all data:
    ui->customPlot->xAxis->setRange(0, 100);
    ui->customPlot->yAxis->setRange(-100, 100);

    if(statuspars.active_chan!=-1)
    {
        QVector<double> x({-1000000,1000000}), y({(double)initpars.card[statuspars.active_card].chan[statuspars.active_chan].thresh ,(double)initpars.card[statuspars.active_card].chan[statuspars.active_chan].thresh});
        ui->customPlot->graph(1)->setData(x, y);
        ui->customPlot->graph(0)->setVisible(true);
        ui->customPlot->graph(1)->setVisible(true);
    }

    ui->customPlot->replot();
}

void cronoacqDlg::inittree()
{
    const QStringList headers({tr("Objects")});


    QByteArray lines;
    if(m_initialize_settings){//set 1 if tree.txt was changed
        QFile file("../cronoacq/tree.txt");
        file.open(QIODevice::ReadOnly);
        lines =file.readAll();
        file.close();
        QSettings settings("Fischerlab", "cronoACQ4.1");
        QVariant v;
        v=lines;
        settings.setValue("treestructure", v);

    }
    else
    {
        QSettings settings("Fischerlab", "cronoACQ4.1");
        lines=settings.value("treestructure").toByteArray();
    }

    TreeModel *model = new TreeModel(headers, lines, this);

    ui->treeView->setModel(model);
    for (int column = 0; column < model->columnCount(); ++column)
        ui->treeView->resizeColumnToContents(column);

    QObject::connect(ui->treeView, &QAbstractItemView::clicked, this, &cronoacqDlg::on_activeTreeItemChanged);


}



void cronoacqDlg::initpropertygrid()
{
    //int i = 0;

    QObject::disconnect(variantManager, &QtVariantPropertyManager::valueChanged, this, &cronoacqDlg::on_propertyChanged);


    ui->variantEditor->clear();
    ui->variantEditor->setResizeMode(QtTreePropertyBrowser::Interactive);

    int n=-1;
    if(statuspars.active_card==-1 && statuspars.active_chan==-1) n=0;
    if(statuspars.active_card>-1 && statuspars.active_card<NR_CARDS && statuspars.active_chan==-1) n=1;
    if(statuspars.active_card>-1 && statuspars.active_card<NR_CARDS && statuspars.active_chan>-1 && statuspars.active_chan<5) n=2;


    switch(n){
    case 0:
    {
        QtProperty *topItem = variantManager->addProperty(QtVariantPropertyManager::groupTypeId(), QLatin1String("main settings"));
        QtVariantProperty *item = variantManager->addProperty(QtVariantPropertyManager::enumTypeId(), QLatin1String("source card"));
        QStringList enumNames;
        enumNames << "card 0" << "card 1" << "card 2";
        item->setAttribute(QLatin1String("enumNames"), enumNames);
        item->setValue(initpars.main.sourcecard);
        topItem->addSubProperty(item);


        item = variantManager->addProperty(QtVariantPropertyManager::enumTypeId(), QLatin1String("source channel"));
        enumNames.clear();
        enumNames << "A" << "B" << "C" << "D" << "TDC";
        item->setAttribute(QLatin1String("enumNames"), enumNames);
        item->setValue(initpars.main.sourcechan);
        topItem->addSubProperty(item);

        item = variantManager->addProperty(QMetaType::Int, QLatin1String("range start"));
        item->setValue(initpars.main.range_start);
        item->setAttribute(QLatin1String("minimum"),-100000000);
        item->setAttribute(QLatin1String("maximum"), 100000000);
        item->setAttribute(QLatin1String("singleStep"), 1);
        item->setEnabled(true);
        topItem->addSubProperty(item);

        item = variantManager->addProperty(QMetaType::Int, QLatin1String("range stop"));
        item->setValue(initpars.main.range_stop);
        item->setAttribute(QLatin1String("minimum"),-100000000);
        item->setAttribute(QLatin1String("maximum"), 100000000);
        item->setAttribute(QLatin1String("singleStep"), 1);
        item->setEnabled(true);
        topItem->addSubProperty(item);
        item = variantManager->addProperty(QMetaType::Bool, QLatin1String("advanced configuration"));
        item->setValue(initpars.main.advconf);

        QtVariantProperty *subItem = variantManager->addProperty(QMetaType::QString, QLatin1String("configuration file"));
        subItem->setValue(initpars.main.advfilename);
        if(!initpars.main.advconf) subItem->setEnabled(false);
        item->addSubProperty(subItem);
        topItem->addSubProperty(item);

        ui->variantEditor->setFactoryForManager(variantManager, variantFactory);
        ui->variantEditor->addProperty(topItem);

        QObject::connect(variantManager, &QtVariantPropertyManager::valueChanged, this, &cronoacqDlg::on_propertyChanged);

        break;
    }
    case 1:
    {
        QtProperty *topItem = variantManager->addProperty(QtVariantPropertyManager::groupTypeId(), QLatin1String("card settings"));
        QtVariantProperty *item = variantManager->addProperty(QMetaType::Int, QLatin1String("precursor"));
        item->setValue(initpars.card[statuspars.active_card].cardPars.precursor);
        topItem->addSubProperty(item);
        item = variantManager->addProperty(QMetaType::Int, QLatin1String("length"));
        item->setValue(initpars.card[statuspars.active_card].cardPars.length);
        topItem->addSubProperty(item);
        item = variantManager->addProperty(QMetaType::Bool, QLatin1String("retrigger"));
        item->setValue(initpars.card[statuspars.active_card].cardPars.retrigger);
        topItem->addSubProperty(item);

        ui->variantEditor->setFactoryForManager(variantManager, variantFactory);
        ui->variantEditor->addProperty(topItem);

        QObject::connect(variantManager, &QtVariantPropertyManager::valueChanged, this, &cronoacqDlg::on_propertyChanged);

        break;
    }
    case 2:
    {
        QtProperty *topItem = variantManager->addProperty(QtVariantPropertyManager::groupTypeId(), QLatin1String("channel settings"));
        QtVariantProperty *item = variantManager->addProperty(QMetaType::Bool, QLatin1String("edge"));
        item->setValue(initpars.card[statuspars.active_card].chan[statuspars.active_chan].edge);
        topItem->addSubProperty(item);
        item = variantManager->addProperty(QMetaType::Bool, QLatin1String("rising"));
        item->setValue(initpars.card[statuspars.active_card].chan[statuspars.active_chan].rising);
        topItem->addSubProperty(item);
        item = variantManager->addProperty(QMetaType::Int, QLatin1String("threshhold"));
        item->setValue(initpars.card[statuspars.active_card].chan[statuspars.active_chan].thresh);
        topItem->addSubProperty(item);

        ui->variantEditor->setFactoryForManager(variantManager, variantFactory);
        ui->variantEditor->addProperty(topItem);

        QObject::connect(variantManager, &QtVariantPropertyManager::valueChanged, this, &cronoacqDlg::on_propertyChanged);

        break;
    }

    default:
        ;
    }

    //QtTreePropertyBrowser *variantEditor = new QtTreePropertyBrowser();
//    for(int i=0; i<3; i++) ui->variantEditor->addProperty(card[i]);
//    ui->variantEditor->addProperty(topItem);
    ui->variantEditor->setPropertiesWithoutValueMarked(true);
    ui->variantEditor->setRootIsDecorated(false);


}


void cronoacqDlg::on_resetgraphButton_clicked()
{
    initplot();
}



void cronoacqDlg::on_actionstart_acquistion_triggered()
{
    ui->actionrestart_acquisition->setEnabled(true);
    ui->actionstop_acquisition->setEnabled(true);
    ui->actionstart_acquistion->setEnabled(false);

    // get the palette
    auto palette = ui->lcdRate->palette();
    palette.setColor(palette.Window, Qt::green);
    palette.setColor(palette.WindowText, Qt::black);
    ui->lcdRate->setPalette(palette);

}


void cronoacqDlg::on_actionrestart_acquisition_triggered()
{
    ui->actionrestart_acquisition->setEnabled(true);
    ui->actionstop_acquisition->setEnabled(true);
    ui->actionstart_acquistion->setEnabled(false);

    logmessage("Restart button clicked.");


}


void cronoacqDlg::on_actionstop_acquisition_triggered()
{
    ui->actionrestart_acquisition->setEnabled(false);
    ui->actionstop_acquisition->setEnabled(false);
    ui->actionstart_acquistion->setEnabled(true);

    auto palette = ui->lcdRate->palette();
    palette.setColor(palette.Window, Qt::red);
    palette.setColor(palette.WindowText, Qt::black);
    ui->lcdRate->setPalette(palette);
    int n=0;
    ui->lcdRate->display(n);
}


void cronoacqDlg::on_actionstart_sampling_triggered()
{
    ui->actionpause_sampling->setEnabled(true);
    ui->actionstart_sampling->setEnabled(false);
    //    ui->actionsingle_sampling->setEnabled(true);
    emit sample_n(-1);
}


void cronoacqDlg::on_actionsingle_sampling_triggered()
{
    ui->actionpause_sampling->setEnabled(false);
    ui->actionstart_sampling->setEnabled(true);
    //    ui->actionsingle_sampling->setEnabled(true);
    emit sample_n(1);
}


void cronoacqDlg::on_actionpause_sampling_triggered()
{
    ui->actionpause_sampling->setEnabled(false);
    ui->actionstart_sampling->setEnabled(true);
//    ui->actionsingle_sampling->setEnabled(true);
    emit sample_n(0);

}


void cronoacqDlg::on_actionadv_configuration_triggered()
{
    QString filter = "text files (*.txt) ;; all files (*.*)";
    initpars.main.advfilename = QFileDialog::getOpenFileName(this,"open advanced configuration file", QDir::currentPath(), filter);
    initpropertygrid();
    emit initparsChanged(initpars);
}


void cronoacqDlg::on_actionrecord_data_triggered()
{
    QString filter = "data files (*.daqc) ;; all files (*.*)";
    QString path;
    path=QFileInfo(statuspars.sFileName).absolutePath();
    if(!QDir(path).exists()) path=QDir::homePath();
    QString filename = QFileDialog::getSaveFileName(this,"save data to file", path, filter);

    if(!filename.endsWith(".daqc",Qt::CaseInsensitive)) filename.append(".daqc");

    sFile = new QFile(filename);
    if(!sFile->open(QIODevice::WriteOnly))
    {
        logmessage("Couldn't open file ...");
        return;
    }

    statuspars.sFileName = filename;
    statuspars.bFileOpen=true;
    ui->actionrecord_data->setEnabled(false);
    ui->actionstop_recording->setEnabled(true);
    emit statusChanged(statuspars);
    emit writeDatatoFile(sFile);
    logmessage(tr("file %1 opened").arg(filename));
}


void cronoacqDlg::closeFile()
{
    statuspars.bFileOpen=false;
    ui->actionrecord_data->setEnabled(true);
    ui->actionstop_recording->setEnabled(false);
    sFile->close();
    delete sFile;
    logmessage(tr("file %1 closed").arg(statuspars.sFileName));
    statuspars.sFileName="";
    emit statusChanged(statuspars);
}


void cronoacqDlg::on_maintrigger_stateChanged(int arg1)
{
    statuspars.m_bTrigOnly=arg1;
    emit statusChanged(statuspars);
}

