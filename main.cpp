#include "cronoacqdlg.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    cronoacqDlg w;
    w.show();
    return a.exec();
}
