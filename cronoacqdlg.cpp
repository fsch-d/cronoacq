#include "cronoacqdlg.h"
#include "ui_cronoacqdlg.h"
#include <ctime>
#include <QDebug>
#include <QThread>

#include <QDate>
#include <QLocale>
#include <QLineEdit>
#include "src/qtpropertymanager.h"
#include "src/qtvariantproperty.h"
#include "src/qttreepropertybrowser.h"

#include "qcustomplot.h"


cronoacqDlg::cronoacqDlg(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::cronoacqDlg)
{
    ui->setupUi(this);



    QSettings settings("Fischerlab", "cronoACQ");
 //   restoreGeometry(settings.value("geometry").toByteArray());
    restoreState(settings.value("windowState").toByteArray());


    setCentralWidget(ui->TracedockWidget);
//    splitDockWidget(ui->TracedockWidget,ui->propertydockWidget, Qt::Horizontal);




    //some ui stuff



    ui->actionpause_sampling->setEnabled(false);
    ui->actionrestart_acquisition->setEnabled(false);
    ui->actionstop_acquisition->setEnabled(false);
//    ui->actionstart_acquistion->setEnabled(true);


    ui->lcdRate->setAutoFillBackground(true);

    ui->statusbar->addWidget(ui->lcdRate);
    ui->statusbar->addWidget(ui->nofclients);


    auto palette = ui->lcdRate->palette();
    palette.setColor(palette.Window, Qt::red);
    palette.setColor(palette.WindowText, Qt::black);
    ui->lcdRate->setPalette(palette);

    ui->lcdRate->setSegmentStyle(QLCDNumber::Flat );

    ui->customPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    ui->customPlot->axisRect()->setRangeZoomAxes(nullptr,ui->customPlot->yAxis);



    variantManager = new QtVariantPropertyManager();
    variantFactory = new QtVariantEditorFactory();
    initpropertygrid();

    QObject::connect(variantManager, &QtVariantPropertyManager::valueChanged, this, &cronoacqDlg::propertyChanged);



    initplot();
 //   inittree();
    nofclients(0);


    //getting the threads up and running
    QThread::currentThread()->setObjectName("cronoacqdlg thread");

    acqCtrl = new acqcontrol();
    acqCtrlThread = new QThread();

    acqCtrl->moveToThread(acqCtrlThread);
    acqCtrl->setObjectName("acqctrlThread");
    acqCtrlThread->start();


    connect(acqCtrlThread, &QThread::finished, acqCtrl, &QObject::deleteLater);

    //Connect ui to readout thread
    QObject::connect(acqCtrl,&acqcontrol::logmessage,this,&cronoacqDlg::logmessage);
    QObject::connect(ui->actionstart_acquistion,&QAction::triggered,acqCtrl,&acqcontrol::runloop);
    QObject::connect(ui->actionstop_acquisition,&QAction::triggered,acqCtrl,&acqcontrol::quit,Qt::DirectConnection);
    QObject::connect(ui->actionrestart_acquisition,&QAction::triggered,acqCtrl,&acqcontrol::restart,Qt::DirectConnection);
    QObject::connect(acqCtrl,&acqcontrol::sampleready,this,&cronoacqDlg::plotsample);
    QObject::connect(this,&cronoacqDlg::sample_n,acqCtrl,&acqcontrol::set_nofsamples);
    QObject::connect(acqCtrl,&acqcontrol::rate,this,&cronoacqDlg::showrate);
    QObject::connect(acqCtrl,&acqcontrol::nofclients,this,&cronoacqDlg::nofclients);

    qInfo() << this << "All starts here!";


    //



}

cronoacqDlg::~cronoacqDlg()
{

    if(0)
    {
        QSettings settings("Fischerlab", "cronoACQ");
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

void cronoacqDlg::nofclients(int n)
{
    auto noc = QStringLiteral("no of clients: %1").arg(n);
    ui->nofclients->setText(noc);
}

void cronoacqDlg::propertyChanged(QtProperty *property, const QVariant &val)
{
    qInfo() << "item " << property->propertyName() << "changed to" << val;
}


void cronoacqDlg::logtime()
{
    time_t rawtime;
    struct tm * timeinfo;
    char buffer [14];
    time (&rawtime);
    timeinfo = localtime (&rawtime);

    strftime (buffer,14,"[%H:%M:%S]  ",timeinfo);

    ui->logBrowser->setTextColor( QColor( "blue" ) );
    ui->logBrowser->append(buffer);
}

void cronoacqDlg::initplot()
{
    ui->customPlot->clearGraphs();
    ui->customPlot->addGraph();
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
    ui->customPlot->replot();
}

/*void cronoacqDlg::inittree()
{
    ui->treeWidget->setColumnCount(1);
    ui->treeWidget->setHeaderLabel("controls");
    QTreeWidgetItem *main = new QTreeWidgetItem(ui->treeWidget, QStringList(QString("main")));
    QTreeWidgetItem *card0 = new QTreeWidgetItem(ui->treeWidget, QStringList(QString("card 0")));
    QTreeWidgetItem *card1 = new QTreeWidgetItem(ui->treeWidget, QStringList(QString("card 1")));
    QTreeWidgetItem *card2 = new QTreeWidgetItem(ui->treeWidget, QStringList(QString("card 2")));

    QList<QTreeWidgetItem *> items;
    items.append(main);
    items.append(card0);
    items.append(card1);
    items.append(card2);

    QTreeWidgetItem *channel[3][5];
    QList<QTreeWidgetItem *> channels[3];
    for(int i=0; i<3; i++)
    {
        for(int j=0; j<5; j++) channel[i][j]= new QTreeWidgetItem();
        channel[i][0]->setText(0,"channel A");
        channel[i][1]->setText(0,"channel B");
        channel[i][2]->setText(0,"channel C");
        channel[i][3]->setText(0,"channel D");
        channel[i][4]->setText(0,"TDC");
        for(int j=0; j<5; j++) channels[i].append(channel[i][j]);
    }

    card0->addChildren(channels[0]);
    card1->addChildren(channels[1]);
    card2->addChildren(channels[2]);

    ui->treeWidget->addTopLevelItems(items);
}*/


void cronoacqDlg::initpropertygrid()
{
    int i = 0;
    ui->variantEditor->setResizeMode(QtTreePropertyBrowser::Interactive);

    QtProperty *mainItems = variantManager->addProperty(QtVariantPropertyManager::groupTypeId(), QLatin1String("main settings"));

    QtVariantProperty *item = variantManager->addProperty(QtVariantPropertyManager::enumTypeId(), QLatin1String("source card"));
    QStringList enumNames;
    enumNames << "card 0" << "card 1" << "card 2";
    item->setAttribute(QLatin1String("enumNames"), enumNames);
    item->setValue(0);
    mainItems->addSubProperty(item);


    item = variantManager->addProperty(QtVariantPropertyManager::enumTypeId(), QLatin1String("source channel"));
    enumNames.clear();
    enumNames << "A" << "B" << "C" << "D" << "TDC";
    item->setAttribute(QLatin1String("enumNames"), enumNames);
    item->setValue(0);
    mainItems->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Int, QLatin1String("range start"));
    item->setValue(-50000000);
    item->setAttribute(QLatin1String("minimum"),-100000000);
    item->setAttribute(QLatin1String("maximum"), 100000000);
    item->setAttribute(QLatin1String("singleStep"), 1);
    item->setEnabled(true);
    mainItems->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Int, QLatin1String("range stop"));
    item->setValue(1000000);
    item->setAttribute(QLatin1String("minimum"),-100000000);
    item->setAttribute(QLatin1String("maximum"), 100000000);
    item->setAttribute(QLatin1String("singleStep"), 1);
    item->setEnabled(true);
    mainItems->addSubProperty(item);

    //item = variantManager->addProperty(QtVariantPropertyManager::QtVariantPropertyManager::groupTypeId(), QLatin1String("advanced configuration"));

 //   QtVariantProperty *subItem = variantManager->addProperty(QVariant::Bool, QLatin1String("enabled"));
    item = variantManager->addProperty(QVariant::Bool, QLatin1String("advanced configuration"));
    item->setValue(true);

    QtVariantProperty *subItem = variantManager->addProperty(QVariant::String, QLatin1String("configuration file"));
    item->addSubProperty(subItem);
    mainItems->addSubProperty(item);


    QtProperty *card[3];
    card[0] = variantManager->addProperty(QtVariantPropertyManager::groupTypeId(), QLatin1String("card 0"));
    card[1] = variantManager->addProperty(QtVariantPropertyManager::groupTypeId(), QLatin1String("card 1"));
    card[2] = variantManager->addProperty(QtVariantPropertyManager::groupTypeId(), QLatin1String("card 2"));


    for(int i=0; i<3; i++)
    {
        QtVariantProperty *channel[5];

        channel[0] = variantManager->addProperty(QVariant::String, QLatin1String("A"));
        channel[1] = variantManager->addProperty(QVariant::String, QLatin1String("B"));
        channel[2] = variantManager->addProperty(QVariant::String, QLatin1String("C"));
        channel[3] = variantManager->addProperty(QVariant::String, QLatin1String("D"));
        channel[4] = variantManager->addProperty(QVariant::String, QLatin1String("TDC"));

        //item->setValue(1000000);
        for(int j=0; j<5; j++){

            subItem = variantManager->addProperty(QVariant::Bool, QLatin1String("edge"));
            subItem->setValue(true);
            channel[j]->addSubProperty(subItem);
            subItem = variantManager->addProperty(QVariant::Bool, QLatin1String("rising"));
            subItem->setValue(false);
            channel[j]->addSubProperty(subItem);
            subItem = variantManager->addProperty(QVariant::Int, QLatin1String("threshhold"));
            subItem->setValue(-10);
            channel[j]->addSubProperty(subItem);

            card[i]->addSubProperty(channel[j]);
        }


    }

/*

    QtProperty *topItem = variantManager->addProperty(QtVariantPropertyManager::groupTypeId(), QLatin1String("the other stuff"));


    item = variantManager->addProperty(QVariant::Int, QString::number(i++) + QLatin1String(" Int Property"));
    item->setValue(20);
    item->setAttribute(QLatin1String("minimum"), 0);
    item->setAttribute(QLatin1String("maximum"), 100);
    item->setAttribute(QLatin1String("singleStep"), 10);
    item->setEnabled(true);
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Int, QString::number(i++) + QLatin1String(" Int Property (ReadOnly)"));
    item->setValue(20);
    item->setAttribute(QLatin1String("minimum"), 0);
    item->setAttribute(QLatin1String("maximum"), 100);
    item->setAttribute(QLatin1String("singleStep"), 10);
    item->setAttribute(QLatin1String("readOnly"), true);
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Double, QString::number(i++) + QLatin1String(" Double Property"));
    item->setValue(1.2345);
    item->setAttribute(QLatin1String("singleStep"), 0.1);
    item->setAttribute(QLatin1String("decimals"), 3);
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Double, QString::number(i++) + QLatin1String(" Double Property (ReadOnly)"));
    item->setValue(1.23456);
    item->setAttribute(QLatin1String("singleStep"), 0.1);
    item->setAttribute(QLatin1String("decimals"), 5);
    item->setAttribute(QLatin1String("readOnly"), true);
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::String, QString::number(i++) + QLatin1String(" String Property"));
    item->setValue("Value");
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::String, QString::number(i++) + QLatin1String(" String Property (Password)"));
    item->setAttribute(QLatin1String("echoMode"), QLineEdit::Password);
    item->setValue("Password");
    topItem->addSubProperty(item);

    // Readonly String Property
    item = variantManager->addProperty(QVariant::String, QString::number(i++) + QLatin1String(" String Property (ReadOnly)"));
    item->setAttribute(QLatin1String("readOnly"), true);
    item->setValue("readonly text");
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Date, QString::number(i++) + QLatin1String(" Date Property"));
    item->setValue(QDate::currentDate().addDays(2));
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Time, QString::number(i++) + QLatin1String(" Time Property"));
    item->setValue(QTime::currentTime());
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::DateTime, QString::number(i++) + QLatin1String(" DateTime Property"));
    item->setValue(QDateTime::currentDateTime());
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::KeySequence, QString::number(i++) + QLatin1String(" KeySequence Property"));
    item->setValue(QKeySequence(Qt::ControlModifier | Qt::Key_Q));
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Char, QString::number(i++) + QLatin1String(" Char Property"));
    item->setValue(QChar(386));
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Locale, QString::number(i++) + QLatin1String(" Locale Property"));
    item->setValue(QLocale(QLocale::Polish, QLocale::Poland));
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Point, QString::number(i++) + QLatin1String(" Point Property"));
    item->setValue(QPoint(10, 10));
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::PointF, QString::number(i++) + QLatin1String(" PointF Property"));
    item->setValue(QPointF(1.2345, -1.23451));
    item->setAttribute(QLatin1String("decimals"), 3);
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Size, QString::number(i++) + QLatin1String(" Size Property"));
    item->setValue(QSize(20, 20));
    item->setAttribute(QLatin1String("minimum"), QSize(10, 10));
    item->setAttribute(QLatin1String("maximum"), QSize(30, 30));
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::SizeF, QString::number(i++) + QLatin1String(" SizeF Property"));
    item->setValue(QSizeF(1.2345, 1.2345));
    item->setAttribute(QLatin1String("decimals"), 3);
    item->setAttribute(QLatin1String("minimum"), QSizeF(0.12, 0.34));
    item->setAttribute(QLatin1String("maximum"), QSizeF(20.56, 20.78));
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Rect, QString::number(i++) + QLatin1String(" Rect Property"));
    item->setValue(QRect(10, 10, 20, 20));
    topItem->addSubProperty(item);
    item->setAttribute(QLatin1String("constraint"), QRect(0, 0, 50, 50));

    item = variantManager->addProperty(QVariant::RectF, QString::number(i++) + QLatin1String(" RectF Property"));
    item->setValue(QRectF(1.2345, 1.2345, 1.2345, 1.2345));
    topItem->addSubProperty(item);
    item->setAttribute(QLatin1String("constraint"), QRectF(0, 0, 50, 50));
    item->setAttribute(QLatin1String("decimals"), 3);

/*    item = variantManager->addProperty(QtVariantPropertyManager::enumTypeId(),
                                       QString::number(i++) + QLatin1String(" Enum Property"));
    QStringList enumNames;
    enumNames << "Enum0" << "Enum1" << "Enum2";
    item->setAttribute(QLatin1String("enumNames"), enumNames);
    item->setValue(1);
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QtVariantPropertyManager::flagTypeId(),
                                       QString::number(i++) + QLatin1String(" Flag Property"));
    QStringList flagNames;
    flagNames << "Flag0" << "Flag1" << "Flag2";
    item->setAttribute(QLatin1String("flagNames"), flagNames);
    item->setValue(5);
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::SizePolicy, QString::number(i++) + QLatin1String(" SizePolicy Property"));
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Font, QString::number(i++) + QLatin1String(" Font Property"));
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Cursor, QString::number(i++) + QLatin1String(" Cursor Property"));
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Color, QString::number(i++) + QLatin1String(" Color Property"));
    topItem->addSubProperty(item);

/*    QtProperty *topItem = variantManager->addProperty(QtVariantPropertyManager::groupTypeId(),
                                                      QString::number(i++) + QLatin1String(" Group Property"));

    QtVariantProperty *item = variantManager->addProperty(QVariant::Bool, QString::number(i++) + QLatin1String(" Bool Property"));
    item->setValue(true);
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Int, QString::number(i++) + QLatin1String(" Int Property"));
    item->setValue(20);
    item->setAttribute(QLatin1String("minimum"), 0);
    item->setAttribute(QLatin1String("maximum"), 100);
    item->setAttribute(QLatin1String("singleStep"), 10);
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Int, QString::number(i++) + QLatin1String(" Int Property (ReadOnly)"));
    item->setValue(20);
    item->setAttribute(QLatin1String("minimum"), 0);
    item->setAttribute(QLatin1String("maximum"), 100);
    item->setAttribute(QLatin1String("singleStep"), 10);
    item->setAttribute(QLatin1String("readOnly"), true);
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Double, QString::number(i++) + QLatin1String(" Double Property"));
    item->setValue(1.2345);
    item->setAttribute(QLatin1String("singleStep"), 0.1);
    item->setAttribute(QLatin1String("decimals"), 3);
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Double, QString::number(i++) + QLatin1String(" Double Property (ReadOnly)"));
    item->setValue(1.23456);
    item->setAttribute(QLatin1String("singleStep"), 0.1);
    item->setAttribute(QLatin1String("decimals"), 5);
    item->setAttribute(QLatin1String("readOnly"), true);
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::String, QString::number(i++) + QLatin1String(" String Property"));
    item->setValue("Value");
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::String, QString::number(i++) + QLatin1String(" String Property (Password)"));
    item->setAttribute(QLatin1String("echoMode"), QLineEdit::Password);
    item->setValue("Password");
    topItem->addSubProperty(item);

    // Readonly String Property
    item = variantManager->addProperty(QVariant::String, QString::number(i++) + QLatin1String(" String Property (ReadOnly)"));
    item->setAttribute(QLatin1String("readOnly"), true);
    item->setValue("readonly text");
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Date, QString::number(i++) + QLatin1String(" Date Property"));
    item->setValue(QDate::currentDate().addDays(2));
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Time, QString::number(i++) + QLatin1String(" Time Property"));
    item->setValue(QTime::currentTime());
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::DateTime, QString::number(i++) + QLatin1String(" DateTime Property"));
    item->setValue(QDateTime::currentDateTime());
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::KeySequence, QString::number(i++) + QLatin1String(" KeySequence Property"));
    item->setValue(QKeySequence(Qt::ControlModifier | Qt::Key_Q));
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Char, QString::number(i++) + QLatin1String(" Char Property"));
    item->setValue(QChar(386));
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Locale, QString::number(i++) + QLatin1String(" Locale Property"));
    item->setValue(QLocale(QLocale::Polish, QLocale::Poland));
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Point, QString::number(i++) + QLatin1String(" Point Property"));
    item->setValue(QPoint(10, 10));
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::PointF, QString::number(i++) + QLatin1String(" PointF Property"));
    item->setValue(QPointF(1.2345, -1.23451));
    item->setAttribute(QLatin1String("decimals"), 3);
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Size, QString::number(i++) + QLatin1String(" Size Property"));
    item->setValue(QSize(20, 20));
    item->setAttribute(QLatin1String("minimum"), QSize(10, 10));
    item->setAttribute(QLatin1String("maximum"), QSize(30, 30));
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::SizeF, QString::number(i++) + QLatin1String(" SizeF Property"));
    item->setValue(QSizeF(1.2345, 1.2345));
    item->setAttribute(QLatin1String("decimals"), 3);
    item->setAttribute(QLatin1String("minimum"), QSizeF(0.12, 0.34));
    item->setAttribute(QLatin1String("maximum"), QSizeF(20.56, 20.78));
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Rect, QString::number(i++) + QLatin1String(" Rect Property"));
    item->setValue(QRect(10, 10, 20, 20));
    topItem->addSubProperty(item);
    item->setAttribute(QLatin1String("constraint"), QRect(0, 0, 50, 50));

    item = variantManager->addProperty(QVariant::RectF, QString::number(i++) + QLatin1String(" RectF Property"));
    item->setValue(QRectF(1.2345, 1.2345, 1.2345, 1.2345));
    topItem->addSubProperty(item);
    item->setAttribute(QLatin1String("constraint"), QRectF(0, 0, 50, 50));
    item->setAttribute(QLatin1String("decimals"), 3);

    item = variantManager->addProperty(QtVariantPropertyManager::enumTypeId(),
                                       QString::number(i++) + QLatin1String(" Enum Property"));
    QStringList enumNames;
    enumNames << "Enum0" << "Enum1" << "Enum2";
    item->setAttribute(QLatin1String("enumNames"), enumNames);
    item->setValue(1);
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QtVariantPropertyManager::flagTypeId(),
                                       QString::number(i++) + QLatin1String(" Flag Property"));
    QStringList flagNames;
    flagNames << "Flag0" << "Flag1" << "Flag2";
    item->setAttribute(QLatin1String("flagNames"), flagNames);
    item->setValue(5);
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::SizePolicy, QString::number(i++) + QLatin1String(" SizePolicy Property"));
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Font, QString::number(i++) + QLatin1String(" Font Property"));
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Cursor, QString::number(i++) + QLatin1String(" Cursor Property"));
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Color, QString::number(i++) + QLatin1String(" Color Property"));
    topItem->addSubProperty(item);

*/
    //QtTreePropertyBrowser *variantEditor = new QtTreePropertyBrowser();
    ui->variantEditor->setFactoryForManager(variantManager, variantFactory);
    ui->variantEditor->addProperty(mainItems);
    for(int i=0; i<3; i++) ui->variantEditor->addProperty(card[i]);
//    ui->variantEditor->addProperty(topItem);
    ui->variantEditor->setPropertiesWithoutValueMarked(true);
    ui->variantEditor->setRootIsDecorated(false);


}


void cronoacqDlg::on_resetgraphButton_clicked()
{
    initplot();
}


/*void cronoacqDlg::on_treeWidget_itemClicked(QTreeWidgetItem *item, int column)
{
    if(item->parent()) qInfo() << item->text(0) << "  " << item->parent()->text(column);
    else qInfo() << item->text(column);
}*/


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

