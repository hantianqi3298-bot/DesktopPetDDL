#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QLabel>
#include <QMouseEvent>
#include <QMovie>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <QTimer>
#include <QWidget>

#include "ddldialog.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

// 桌宠主窗口：集成动画显示、鼠标交互、DDL 提醒、番茄钟和简单养成状态。
class MainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void onLongPressTimeout();
    void onPetFrameChanged();
    void toggleHumanForm();

private:
    enum class PomodoroMode { Idle, Focus, Break };
    enum class PetMotionMode { Resting, Walking, Sleeping, Falling, Hanging };

    Ui::MainWindow *ui;

    QMovie *currentMovie;
    QString currentGifPath;
    QPixmap currentPetPixmap;

    DDLDialog *ddlDialog;
    QTimer *ddlCheckTimer;

    QTimer *longPressTimer;
    bool isPressing;

    bool isHumanForm;
    bool hasUnacknowledgedDDL;
    QString unacknowledgedDDLState;

    QLabel *bubbleLabel;
    QTimer *bubbleTimer;
    QTimer *interactionTimer;

    QTimer *pomodoroTimer;
    PomodoroMode pomodoroMode;
    int pomodoroSecondsRemaining;
    bool isPomodoroPaused;

    QTimer *motionTimer;
    QTimer *statusTimer;
    QTimer *randomActionTimer;

    QPoint dragPosition;
    QPoint dragStartGlobal;
    QPoint lastMouseGlobal;
    QPointF petVelocity;

    bool isDragging;
    bool hasDragged;
    bool autoWalkEnabled;
    int walkingDirection;

    qint64 lastOperationTimeMs;
    PetMotionMode motionMode;

    int mood;
    int hunger;
    int energy;

    void startFocusPomodoro();
    void startBreakPomodoro();
    void pausePomodoro();
    void resumePomodoro();
    void stopPomodoro();
    void updatePomodoro();
    QString formatSeconds(int seconds) const;
    QString pomodoroStatusText() const;

    void updatePetMotion();
    void updateMotionMode(bool onGround);
    void updatePetStatus();
    void triggerRandomAction();
    void handleClickInteraction();
    void feedPet();
    void playWithPet();
    void showPetStatus();
    void showBubble(const QString &text, int msec = 3500);
    void updateBubblePosition();
    void markUserOperation();
    QString motionModeText() const;
    void addMood(int delta);
    void addHunger(int delta);
    void addEnergy(int delta);
    void placePetAtBottomRight();

    void checkDDLTasks();
    void changePetState(const QString &gifPath);
    void restorePetState();
};

#endif
