#ifndef THREADINGPUSH_H
#define THREADINGPUSH_H

#include <QObject>
#include <git2.h>
class threadingPush : public QObject
{
    Q_OBJECT
public:
    explicit threadingPush(QObject *parent = nullptr);
void ControlPush(QString branchName,git_repository *repo,int login_tries,bool is_cred_cached);
signals:

};

#endif // THREADINGPUSH_H
