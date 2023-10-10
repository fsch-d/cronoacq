#ifndef CRONOACQDLG_H
#define CRONOACQDLG_H

#include <QMainWindow>
#include "acqcontrol.h"
#include "qcustomplot.h"
#include "src/qtpropertymanager.h"
#include "src/qtvariantproperty.h"


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
    void propertyChanged(QtProperty *property, const QVariant &val);


signals:
    void sample_n(int n); //request samples from acqcontrol for QCustomPlot

private slots:
    void on_resetgraphButton_clicked();
//    void on_treeWidget_itemClicked(QTreeWidgetItem *item, int column);

    void on_actionstart_acquistion_triggered();

    void on_actionrestart_acquisition_triggered();

    void on_actionstop_acquisition_triggered();

    void on_actionstart_sampling_triggered();

    void on_actionsingle_sampling_triggered();

    void on_actionpause_sampling_triggered();

private:
    void logtime();
    void initplot();
//    void inittree();

    void initpropertygrid();


    Ui::cronoacqDlg *ui;



    QThread *acqCtrlThread;
    acqcontrol *acqCtrl;

    init_pars initpars;
    QtVariantPropertyManager *variantManager;
    QtVariantEditorFactory *variantFactory;

};
#endif // CRONOACQDLG_H
