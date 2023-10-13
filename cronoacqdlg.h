#ifndef CRONOACQDLG_H
#define CRONOACQDLG_H

#include <QMainWindow>
#include <QMetaType>

#include "acqcontrol.h"
#include "qcustomplot.h"
#include "qtpropertymanager.h"
#include "qtvariantproperty.h"



QT_BEGIN_NAMESPACE
namespace Ui { class cronoacqDlg; }
QT_END_NAMESPACE

class cronoacqDlg : public QMainWindow
{
    Q_OBJECT

public:
    cronoacqDlg(QWidget *parent = nullptr);
    ~cronoacqDlg();

public slots:
    void logmessage(QString message);
    void plotsample(QVector<double> x,QVector<double> y);
    void showrate(int counts);
    void nofclients(int n);


signals:
    void sample_n(int n); //request samples from acqcontrol for QCustomPlot
    void statusChanged(status_pars status); //active card/channel changed
    void initparsChanged(init_pars ip);

private slots:
    void on_activeTreeItemChanged(const QModelIndex &index);

    void on_propertyChanged(QtProperty *property, const QVariant &val);

    //void on_propertyActivated(QTreeWidgetItem *item, int column);

    void on_resetgraphButton_clicked();
//    void on_treeWidget_itemClicked(QTreeWidgetItem *item, int column);

    void on_actionstart_acquistion_triggered();

    void on_actionrestart_acquisition_triggered();

    void on_actionstop_acquisition_triggered();

    void on_actionstart_sampling_triggered();

    void on_actionsingle_sampling_triggered();

    void on_actionpause_sampling_triggered();

    void on_actionadv_configuration_triggered();

    void on_actionrecord_data_triggered();

    void on_actionstop_recording_triggered();

private:
    void logtime();
    void initplot();
    void inittree();

    void initpropertygrid();


    Ui::cronoacqDlg *ui;



    QThread *acqCtrlThread;
    acqcontrol *acqCtrl;

    init_pars initpars;
    status_pars statuspars;


    QtVariantPropertyManager *variantManager;
    QtVariantEditorFactory *variantFactory;

    bool m_initialize_settings=false; //set that "true" if you run it on a new computer the first time

};
#endif // CRONOACQDLG_H
