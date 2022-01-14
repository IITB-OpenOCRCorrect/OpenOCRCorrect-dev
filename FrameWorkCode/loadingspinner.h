#ifndef LOADINGSPINNER_H
#define LOADINGSPINNER_H

#include <QDialog>

namespace Ui {
class LoadingSpinner;
}

class LoadingSpinner : public QDialog
{
    Q_OBJECT

public:
    explicit LoadingSpinner(QWidget *parent = nullptr);
    ~LoadingSpinner();

    void SetSave();

private:

    QMovie *mv;
    Ui::LoadingSpinner *ui;
};

#endif // LOADINGSPINNER_H
