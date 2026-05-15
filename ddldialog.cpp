#include "ddldialog.h"
#include "ui_ddldialog.h"

#include <QBrush>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QDebug>

#include <algorithm>

// 本文件负责 DDL 管理窗口的任务校验、列表刷新和 JSON 持久化。
namespace {
// 生成 DDL 数据文件路径；如果 data 目录不存在，则自动创建。
QString dataFilePath()
{
    const QString dataPath = QCoreApplication::applicationDirPath() + "/data";
    QDir dir;
    if (!dir.exists(dataPath)) {
        dir.mkpath(dataPath);
    }
    return dataPath + "/ddl_data.json";
}

// 将距离截止时间的秒数转换为列表中显示的中文剩余时间。
QString remainingText(qint64 seconds)
{
    if (seconds < 0) {
        return QStringLiteral("已截止");
    }
    if (seconds < 60) {
        return QStringLiteral("剩余：不足 1 分钟");
    }

    const qint64 days = seconds / (24 * 3600);
    const qint64 hours = (seconds % (24 * 3600)) / 3600;
    const qint64 minutes = (seconds % 3600) / 60;

    if (days > 0) {
        return QStringLiteral("剩余：%1 天 %2 小时").arg(days).arg(hours);
    }
    if (hours > 0) {
        return QStringLiteral("剩余：%1 小时 %2 分钟").arg(hours).arg(minutes);
    }
    return QStringLiteral("剩余：%1 分钟").arg(minutes);
}
}

// 初始化 DDL 管理窗口，绑定按钮事件，并读取历史任务。
DDLDialog::DDLDialog(QWidget *parent)
    : QDialog(parent)
    , tasks()
    , ui(new Ui::DDLDialog)
    , uiRefreshTimer(new QTimer(this))
{
    ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setWindowTitle(QStringLiteral("DDL 管理"));

    ui->taskEdit->setPlaceholderText(QStringLiteral("请输入任务名称"));
    ui->deadlineEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    ui->deadlineEdit->setCalendarPopup(true);
    ui->deadlineEdit->setMinimumDateTime(QDateTime::currentDateTime());
    ui->deadlineEdit->setDateTime(QDateTime::currentDateTime().addSecs(3600));

    connect(ui->addButton, &QPushButton::clicked, this, &DDLDialog::addTask);
    connect(ui->deleteButton, &QPushButton::clicked, this, &DDLDialog::deleteTask);
    connect(ui->taskEdit, &QLineEdit::returnPressed, this, &DDLDialog::addTask);
    connect(uiRefreshTimer, &QTimer::timeout, this, [this]() {
        if (isVisible()) {
            refreshTaskListUI();
        }
    });

    loadTasksFromFile();
    refreshTaskListUI();
    uiRefreshTimer->start(60000);
}

DDLDialog::~DDLDialog()
{
    delete ui;
}

// 返回任务列表引用，供主窗口定时检查 DDL 状态。
QVector<DDLTask> &DDLDialog::getTasks()
{
    return tasks;
}

// 添加任务：校验输入、处理同名任务、排序并保存到本地文件。
void DDLDialog::addTask()
{
    const QString taskName = ui->taskEdit->text().trimmed();
    if (taskName.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("任务名称不能为空。"));
        return;
    }

    const QDateTime deadline = ui->deadlineEdit->dateTime();
    if (deadline <= QDateTime::currentDateTime()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("截止时间必须晚于当前时间。"));
        return;
    }

    for (const DDLTask &task : tasks) {
        if (task.name.compare(taskName, Qt::CaseInsensitive) == 0) {
            const QString message = QStringLiteral("已经存在同名 DDL：\n\n任务：%1\n截止时间：%2\n\n是否仍然添加？")
                                        .arg(task.name, task.deadline.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
            const auto reply = QMessageBox::question(this,
                                                     QStringLiteral("发现重复任务"),
                                                     message,
                                                     QMessageBox::Yes | QMessageBox::No);
            if (reply == QMessageBox::No) {
                return;
            }
            break;
        }
    }

    DDLTask task;
    task.name = taskName;
    task.deadline = deadline;
    tasks.append(task);

    sortTasksByDeadline();
    saveTasksToFile();
    refreshTaskListUI();
    emit taskAdded(task.name, task.deadline);

    ui->taskEdit->clear();
    ui->deadlineEdit->setMinimumDateTime(QDateTime::currentDateTime());
    ui->deadlineEdit->setDateTime(QDateTime::currentDateTime().addSecs(3600));
}

// 删除当前选中的任务，删除前弹窗确认，避免误操作。
void DDLDialog::deleteTask()
{
    const int row = ui->taskList->currentRow();
    if (row < 0 || row >= tasks.size()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请先在列表中选择要删除的任务。"));
        return;
    }

    const DDLTask task = tasks.at(row);
    const QString message = QStringLiteral("确定要删除这个 DDL 吗？\n\n任务：%1\n截止时间：%2")
                                .arg(task.name, task.deadline.toString(QStringLiteral("yyyy-MM-dd HH:mm")));

    const auto reply = QMessageBox::question(this,
                                             QStringLiteral("确认删除"),
                                             message,
                                             QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        return;
    }

    tasks.removeAt(row);
    saveTasksToFile();
    refreshTaskListUI();
    emit taskDeleted(task.name);
}

// 将任务列表写入 JSON 文件，保证程序重启后 DDL 不丢失。
void DDLDialog::saveTasksToFile()
{
    QJsonArray array;
    for (const DDLTask &task : tasks) {
        QJsonObject object;
        object.insert(QStringLiteral("name"), task.name);
        object.insert(QStringLiteral("deadline"), task.deadline.toString(Qt::ISODate));
        object.insert(QStringLiteral("reminded"), task.reminded);
        object.insert(QStringLiteral("expiredNotified"), task.expiredNotified);
        array.append(object);
    }

    QFile file(dataFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "Failed to save DDL data:" << file.errorString();
        return;
    }

    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
}

// 从 JSON 文件读取历史任务，并过滤空名称或无效时间的记录。
void DDLDialog::loadTasksFromFile()
{
    QFile file(dataFilePath());
    if (!file.exists()) {
        return;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open DDL data:" << file.errorString();
        return;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) {
        qWarning() << "Invalid DDL data file:" << error.errorString();
        return;
    }

    tasks.clear();
    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        DDLTask task;
        task.name = object.value(QStringLiteral("name")).toString().trimmed();
        task.deadline = QDateTime::fromString(object.value(QStringLiteral("deadline")).toString(), Qt::ISODate);
        task.reminded = object.value(QStringLiteral("reminded")).toBool(false);
        task.expiredNotified = object.value(QStringLiteral("expiredNotified")).toBool(false);

        if (!task.name.isEmpty() && task.deadline.isValid()) {
            tasks.append(task);
        }
    }
    sortTasksByDeadline();
}

// 刷新任务列表，并用颜色和粗体区分截止、紧急和普通任务。
void DDLDialog::refreshTaskListUI()
{
    ui->taskList->clear();
    const QDateTime now = QDateTime::currentDateTime();

    for (const DDLTask &task : tasks) {
        const qint64 seconds = now.secsTo(task.deadline);
        const QString text = QStringLiteral("%1 | 截止时间：%2 | %3")
                                 .arg(task.name,
                                      task.deadline.toString(QStringLiteral("yyyy-MM-dd HH:mm")),
                                      remainingText(seconds));

        QListWidgetItem *item = new QListWidgetItem(text);
        if (seconds < 0) {
            item->setForeground(QBrush(QColor(211, 47, 47)));
            QFont font = item->font();
            font.setBold(true);
            item->setFont(font);
            item->setToolTip(QStringLiteral("这个任务已经截止，请尽快处理。"));
        } else if (seconds <= 24 * 3600) {
            item->setForeground(QBrush(QColor(245, 124, 0)));
            QFont font = item->font();
            font.setBold(true);
            item->setFont(font);
            item->setToolTip(QStringLiteral("这个任务将在 24 小时内截止。"));
        } else {
            item->setForeground(QBrush(QColor(68, 68, 68)));
            item->setToolTip(QStringLiteral("任务时间还比较充裕。"));
        }
        ui->taskList->addItem(item);
    }
}

// 按截止时间从早到晚排序，让最近的 DDL 显示在前面。
void DDLDialog::sortTasksByDeadline()
{
    std::sort(tasks.begin(), tasks.end(), [](const DDLTask &a, const DDLTask &b) {
        return a.deadline < b.deadline;
    });
}
