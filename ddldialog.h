#ifndef DDLDIALOG_H
#define DDLDIALOG_H

#include <QDateTime>
#include <QDialog>
#include <QString>
#include <QTimer>
#include <QVector>

namespace Ui {
class DDLDialog;
}

// 单个 DDL 任务的数据结构，记录任务内容、截止时间和提醒状态。
struct DDLTask
{
    QString name;
    QDateTime deadline;
    bool reminded = false;
    bool expiredNotified = false;
};

// DDL 管理窗口：负责添加、删除、显示和持久化保存 DDL 任务。
class DDLDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DDLDialog(QWidget *parent = nullptr);
    ~DDLDialog();

    QVector<DDLTask> &getTasks();
    void saveTasksToFile();
    void refreshTaskListUI();

signals:
    void taskAdded(const QString &taskName, const QDateTime &deadline);
    void taskDeleted(const QString &taskName);

private slots:
    void addTask();
    void deleteTask();

private:
    QVector<DDLTask> tasks;
    Ui::DDLDialog *ui;
    QTimer *uiRefreshTimer;

    void loadTasksFromFile();
    void sortTasksByDeadline();
};

#endif
