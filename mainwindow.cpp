#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QBitmap>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QFile>
#include <QGuiApplication>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QRandomGenerator>
#include <QScreen>

// 本文件负责桌宠主窗口：动画绘制、鼠标交互、DDL 提醒、番茄钟和养成状态。
#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

namespace {
//鼠标全局坐标获取方式。
QPoint globalMousePos(QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->globalPosition().toPoint();
#else
    return event->globalPos();
#endif
}

// Windows 平台下移除系统边框、圆角和阴影，使桌宠更像真正贴在桌面上。
void removeSystemWindowBorder(QWidget *widget)
{
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    if (!hwnd) {
        return;
    }

    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_BORDER | WS_DLGFRAME | WS_SYSMENU);
    style |= WS_POPUP;
    SetWindowLongPtr(hwnd, GWL_STYLE, style);

    LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    exStyle &= ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE);
    exStyle |= WS_EX_TOOLWINDOW;
    SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle);

#ifndef DWMWA_NCRENDERING_POLICY
#define DWMWA_NCRENDERING_POLICY 2
#endif
#ifndef DWMNCRP_DISABLED
#define DWMNCRP_DISABLED 1
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_DONOTROUND
#define DWMWCP_DONOTROUND 1
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFE
#endif

    const int ncRenderingPolicy = DWMNCRP_DISABLED;
    DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &ncRenderingPolicy, sizeof(ncRenderingPolicy));

    const int cornerPreference = DWMWCP_DONOTROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPreference, sizeof(cornerPreference));

    const COLORREF borderColor = DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));

    MARGINS margins = {0, 0, 0, 0};
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    SetWindowPos(hwnd,
                 nullptr,
                 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
#else
    Q_UNUSED(widget);
#endif
}
}

// 构造函数：初始化透明窗口、动画、DDL 窗口、气泡和所有定时器。
MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::MainWindow)
    , currentMovie(new QMovie(this))
    , ddlDialog(new DDLDialog(nullptr))
    , ddlCheckTimer(new QTimer(this))
    , longPressTimer(new QTimer(this))
    , isPressing(false)
    , isHumanForm(false)
    , hasUnacknowledgedDDL(false)
    , bubbleLabel(new QLabel(nullptr))
    , bubbleTimer(new QTimer(this))
    , interactionTimer(new QTimer(this))
    , pomodoroTimer(new QTimer(this))
    , pomodoroMode(PomodoroMode::Idle)
    , pomodoroSecondsRemaining(0)
    , isPomodoroPaused(false)
    , motionTimer(new QTimer(this))
    , statusTimer(new QTimer(this))
    , randomActionTimer(new QTimer(this))
    , petVelocity(0, 0)
    , isDragging(false)
    , hasDragged(false)
    , autoWalkEnabled(true)
    , walkingDirection(1)
    , lastOperationTimeMs(QDateTime::currentMSecsSinceEpoch())
    , motionMode(PetMotionMode::Resting)
    , mood(100)
    , hunger(100)
    , energy(100)
{
    // 基础窗口属性：无边框、置顶、工具窗口和无系统阴影。
    setWindowFlags(Qt::FramelessWindowHint
                   | Qt::WindowStaysOnTopHint
                   | Qt::Tool
                   | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);

    ui->setupUi(this);

    setAttribute(Qt::WA_StyledBackground, false);
    QPalette transparentPalette = palette();
    transparentPalette.setColor(QPalette::Window, Qt::transparent);
    setPalette(transparentPalette);
    setStyleSheet(QStringLiteral("QWidget#MainWindow { background: transparent; border: 0; }"));

    resize(160, 160);
    setFixedSize(160, 160);
    winId();
    removeSystemWindowBorder(this);
    QTimer::singleShot(0, this, [this]() { removeSystemWindowBorder(this); });

    // QMovie 播放 GIF，frameChanged 信号驱动窗口重绘。
    connect(currentMovie, &QMovie::frameChanged, this, &MainWindow::onPetFrameChanged);
    changePetState(QStringLiteral(":/assets/idle.gif"));
    QTimer::singleShot(0, this, &MainWindow::placePetAtBottomRight);

    // DDLDialog 独立于桌宠主窗口，避免继承透明窗口样式。
    ddlDialog->setAttribute(Qt::WA_DeleteOnClose, false);
    connect(ddlDialog, &DDLDialog::taskAdded, this, [this](const QString &taskName, const QDateTime &) {
        markUserOperation();
        addMood(2);
        changePetState(QStringLiteral(":/assets/happy.gif"));
        showBubble(QStringLiteral("DDL 已记下：\n%1").arg(taskName), 3500);
        interactionTimer->start(2500);
    });
    connect(ddlDialog, &DDLDialog::taskDeleted, this, [this](const QString &taskName) {
        markUserOperation();
        showBubble(QStringLiteral("已删除 DDL：\n%1").arg(taskName), 3000);
        restorePetState();
    });

    // 多个定时器分别负责 DDL 检查、番茄钟、运动、状态和随机行为。
    connect(ddlCheckTimer, &QTimer::timeout, this, &MainWindow::checkDDLTasks);
    ddlCheckTimer->start(10000);

    connect(pomodoroTimer, &QTimer::timeout, this, &MainWindow::updatePomodoro);

    connect(motionTimer, &QTimer::timeout, this, &MainWindow::updatePetMotion);
    motionTimer->start(30);

    connect(statusTimer, &QTimer::timeout, this, &MainWindow::updatePetStatus);
    statusTimer->start(60000);

    connect(randomActionTimer, &QTimer::timeout, this, &MainWindow::triggerRandomAction);
    randomActionTimer->start(12000);

    interactionTimer->setSingleShot(true);
    connect(interactionTimer, &QTimer::timeout, this, &MainWindow::restorePetState);

    longPressTimer->setSingleShot(true);
    connect(longPressTimer, &QTimer::timeout, this, &MainWindow::onLongPressTimeout);

    bubbleLabel->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput);
    bubbleLabel->setAttribute(Qt::WA_TranslucentBackground, true);
    bubbleLabel->setAlignment(Qt::AlignCenter);
    bubbleLabel->setStyleSheet(QStringLiteral(
        "QLabel {"
        "background-color: rgba(255, 255, 255, 235);"
        "border: 2px solid #ff9fb8;"
        "border-radius: 12px;"
        "padding: 8px;"
        "color: #333333;"
        "font-family: 'Microsoft YaHei', 'SimHei';"
        "font-size: 14px;"
        "font-weight: 600;"
        "}"));

    bubbleTimer->setSingleShot(true);
    connect(bubbleTimer, &QTimer::timeout, bubbleLabel, &QLabel::hide);
}

// 析构函数：释放独立创建的气泡窗口和 DDL 窗口。
MainWindow::~MainWindow()
{
    if (bubbleLabel) {
        bubbleLabel->hide();
        delete bubbleLabel;
        bubbleLabel = nullptr;
    }
    if (ddlDialog) {
        ddlDialog->close();
        delete ddlDialog;
        ddlDialog = nullptr;
    }
    delete ui;
}

// 切换桌宠 GIF 状态；人形态下会优先寻找带 1 前缀的资源。
void MainWindow::changePetState(const QString &gifPath)
{
    QString actualPath = gifPath;
    const int slash = actualPath.lastIndexOf('/');
    if (slash >= 0) {
        const QString fileName = actualPath.mid(slash + 1);
        if (isHumanForm && !fileName.startsWith(QLatin1Char('1'))) {
            actualPath.insert(slash + 1, QLatin1Char('1'));
        } else if (!isHumanForm && fileName.startsWith(QLatin1Char('1'))) {
            actualPath.remove(slash + 1, 1);
        }
    }

    if (!QFile::exists(actualPath)) {
        actualPath = gifPath;
    }
    if (!QFile::exists(actualPath)) {
        return;
    }
    if (currentGifPath == actualPath && currentMovie->state() == QMovie::Running) {
        return;
    }

    currentMovie->stop();
    currentMovie->setFileName(actualPath);
    currentGifPath = actualPath;
    currentMovie->start();
}

// 临时动画结束后，根据 DDL、番茄钟和养成状态恢复合适的动画。
void MainWindow::restorePetState()
{
    if (hasUnacknowledgedDDL) {
        changePetState(unacknowledgedDDLState);
    } else if (pomodoroMode == PomodoroMode::Focus) {
        changePetState(QStringLiteral(":/assets/study.gif"));
    } else if (pomodoroMode == PomodoroMode::Break) {
        changePetState(QStringLiteral(":/assets/happy.gif"));
    } else if (mood < 25 || hunger < 20) {
        changePetState(QStringLiteral(":/assets/warn.gif"));
    } else {
        changePetState(QStringLiteral(":/assets/idle.gif"));
    }
}

// 鼠标按下：记录拖拽起点并启动长按检测。
void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    markUserOperation();
    dragPosition = globalMousePos(event) - frameGeometry().topLeft();
    dragStartGlobal = globalMousePos(event);
    lastMouseGlobal = dragStartGlobal;
    petVelocity = QPointF(0, 0);
    isPressing = true;
    hasDragged = false;
    isDragging = false;
    longPressTimer->start(300);
    event->accept();
}

// 鼠标移动：更新桌宠位置，并记录释放时的初速度。
void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton)) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const QPoint currentGlobal = globalMousePos(event);
    const QPoint delta = currentGlobal - lastMouseGlobal;
    petVelocity = QPointF(delta.x() * 0.8, delta.y() * 0.8);

    if ((currentGlobal - dragStartGlobal).manhattanLength() > 8) {
        hasDragged = true;
        if (longPressTimer->isActive()) {
            longPressTimer->stop();
        }
        if (!isDragging) {
            isDragging = true;
            motionMode = PetMotionMode::Resting;
            changePetState(QStringLiteral(":/assets/lift.gif"));
        }
    }

    if (isDragging || hasDragged) {
        move(currentGlobal - dragPosition);
    }
    lastMouseGlobal = currentGlobal;
    event->accept();
}

// 鼠标释放：区分点击、拖拽释放、边缘吸附和下落。
void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    isPressing = false;
    if (longPressTimer->isActive()) {
        longPressTimer->stop();
        if (!hasDragged) {
            handleClickInteraction();
        }
        event->accept();
        return;
    }

    if (isDragging || hasDragged) {
        isDragging = false;
        QScreen *screen = QGuiApplication::screenAt(frameGeometry().center());
        if (!screen) {
            screen = QGuiApplication::primaryScreen();
        }
        if (screen) {
            const QRect rect = screen->availableGeometry();
            const int snapThreshold = 40;
            if (x() <= rect.left() + snapThreshold) {
                move(rect.left(), y());
                motionMode = PetMotionMode::Hanging;
                changePetState(QStringLiteral(":/assets/left1.gif"));
                event->accept();
                return;
            }
            if (x() + width() >= rect.right() - snapThreshold) {
                move(rect.right() - width() + 1, y());
                motionMode = PetMotionMode::Hanging;
                changePetState(QStringLiteral(":/assets/right1.gif"));
                event->accept();
                return;
            }
        }

        if (petVelocity.y() < 2.0) {
            petVelocity.setY(4.0);
        }
        motionMode = PetMotionMode::Falling;
        changePetState(QStringLiteral(":/assets/fall.gif"));
        showBubble(QStringLiteral("喵！别突然松手！"));
        addMood(-3);
    }
    event->accept();
}

// 长按超过阈值后进入“被拎起”状态。
void MainWindow::onLongPressTimeout()
{
    if (!isPressing) {
        return;
    }
    isDragging = true;
    motionMode = PetMotionMode::Resting;
    changePetState(QStringLiteral(":/assets/lift.gif"));
}

// 右键菜单：集中提供 DDL 管理、番茄钟、投喂、互动和退出功能。
void MainWindow::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    QAction *ddlAction = menu.addAction(QStringLiteral("打开 DDL 管理"));
    QAction *pomodoroAction = menu.addAction(QStringLiteral("打开番茄钟"));
    menu.addSeparator();
    QAction *statusAction = menu.addAction(QStringLiteral("查看状态"));
    QAction *feedAction = menu.addAction(QStringLiteral("投喂小鱼干"));
    QAction *playAction = menu.addAction(QStringLiteral("逗猫玩"));
    QAction *walkAction = menu.addAction(autoWalkEnabled ? QStringLiteral("关闭自动走动") : QStringLiteral("开启自动走动"));

    QMenu *stateMenu = menu.addMenu(QStringLiteral("切换动画"));
    QAction *idleAction = stateMenu->addAction(QStringLiteral("闲置"));
    QAction *happyAction = stateMenu->addAction(QStringLiteral("开心"));
    QAction *warnAction = stateMenu->addAction(QStringLiteral("提醒"));
    QAction *angryAction = stateMenu->addAction(QStringLiteral("生气"));
    QAction *runAction = stateMenu->addAction(QStringLiteral("跑步"));
    QAction *eatAction = stateMenu->addAction(QStringLiteral("吃东西"));

    QAction *toggleFormAction = menu.addAction(isHumanForm ? QStringLiteral("切换为猫形态") : QStringLiteral("切换为人形态"));
    menu.addSeparator();
    QAction *exitAction = menu.addAction(QStringLiteral("退出"));

    QAction *selected = menu.exec(event->globalPos());
    if (!selected) {
        return;
    }

    markUserOperation();
    if (selected == ddlAction) {
        ddlDialog->refreshTaskListUI();
        ddlDialog->show();
        ddlDialog->raise();
        ddlDialog->activateWindow();
    } else if (selected == pomodoroAction) {
        QMenu pomodoroMenu(this);
        QAction *status = pomodoroMenu.addAction(pomodoroStatusText());
        status->setEnabled(false);
        pomodoroMenu.addSeparator();
        QAction *focus = pomodoroMenu.addAction(QStringLiteral("开始 25 分钟专注"));
        QAction *rest = pomodoroMenu.addAction(QStringLiteral("开始 5 分钟休息"));
        QAction *pause = pomodoroMenu.addAction(QStringLiteral("暂停"));
        QAction *resume = pomodoroMenu.addAction(QStringLiteral("继续"));
        QAction *stop = pomodoroMenu.addAction(QStringLiteral("取消番茄钟"));

        const bool idle = (pomodoroMode == PomodoroMode::Idle);
        focus->setEnabled(idle);
        rest->setEnabled(idle);
        pause->setEnabled(!idle && !isPomodoroPaused);
        resume->setEnabled(!idle && isPomodoroPaused);
        stop->setEnabled(!idle);

        QAction *pomodoroSelected = pomodoroMenu.exec(event->globalPos());
        if (pomodoroSelected == focus) {
            startFocusPomodoro();
        } else if (pomodoroSelected == rest) {
            startBreakPomodoro();
        } else if (pomodoroSelected == pause) {
            pausePomodoro();
        } else if (pomodoroSelected == resume) {
            resumePomodoro();
        } else if (pomodoroSelected == stop) {
            stopPomodoro();
        }
    } else if (selected == statusAction) {
        showPetStatus();
    } else if (selected == feedAction) {
        feedPet();
    } else if (selected == playAction) {
        playWithPet();
    } else if (selected == walkAction) {
        autoWalkEnabled = !autoWalkEnabled;
        showBubble(autoWalkEnabled ? QStringLiteral("自动走动已开启。") : QStringLiteral("我先乖乖待着。"));
    } else if (selected == idleAction) {
        changePetState(QStringLiteral(":/assets/idle.gif"));
        interactionTimer->start(3000);
    } else if (selected == happyAction) {
        changePetState(QStringLiteral(":/assets/happy.gif"));
        interactionTimer->start(3000);
    } else if (selected == warnAction) {
        changePetState(QStringLiteral(":/assets/warn.gif"));
        interactionTimer->start(3000);
    } else if (selected == angryAction) {
        changePetState(QStringLiteral(":/assets/angry.gif"));
        interactionTimer->start(3000);
    } else if (selected == runAction) {
        changePetState(QStringLiteral(":/assets/run.gif"));
        interactionTimer->start(3000);
    } else if (selected == eatAction) {
        changePetState(QStringLiteral(":/assets/eat.gif"));
        interactionTimer->start(3000);
    } else if (selected == toggleFormAction) {
        toggleHumanForm();
    } else if (selected == exitAction) {
        QApplication::quit();
    }
}

// 程序启动后将桌宠放置在当前屏幕右下角。
void MainWindow::placePetAtBottomRight()
{
    QScreen *screen = QGuiApplication::screenAt(pos());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return;
    }
    const QRect rect = screen->availableGeometry();
    move(rect.right() - width() - 20, rect.bottom() - height() + 1);
}

// 每 30ms 更新桌宠运动，包括自动走动、自由下落和边界碰撞。
void MainWindow::updatePetMotion()
{
    if (isDragging || isPressing) {
        return;
    }

    QScreen *screen = QGuiApplication::screenAt(frameGeometry().center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return;
    }

    const QRect rect = screen->availableGeometry();
    const int floorY = rect.bottom() - height() + 1;
    const bool onGround = y() >= floorY - 1;
    updateMotionMode(onGround);

    if (motionMode == PetMotionMode::Walking && onGround) {
        const double speed = (mood < 30 || energy < 30) ? 1.5 : 3.0;
        petVelocity.setX(walkingDirection * speed);
        if (pomodoroMode == PomodoroMode::Idle) {
            changePetState(QStringLiteral(":/assets/run.gif"));
        }
    } else if (motionMode != PetMotionMode::Falling && onGround) {
        petVelocity.setX(0);
        if (currentGifPath.contains(QStringLiteral("run")) && !interactionTimer->isActive()) {
            restorePetState();
        }
    }

    if (motionMode == PetMotionMode::Falling) {
        changePetState(QStringLiteral(":/assets/fall.gif"));
    }

    if (motionMode == PetMotionMode::Hanging) {
        petVelocity = QPointF(0, 0);
        return;
    }

    if (!onGround || petVelocity.y() > 0.1 || motionMode == PetMotionMode::Falling) {
        petVelocity.setY(petVelocity.y() + 0.55);
    }

    QPointF nextPos(x() + petVelocity.x(), y() + petVelocity.y());
    if (nextPos.x() < rect.left()) {
        nextPos.setX(rect.left());
        walkingDirection = 1;
        petVelocity.setX(qAbs(petVelocity.x()) * 0.5);
    }
    if (nextPos.x() + width() > rect.right() + 1) {
        nextPos.setX(rect.right() - width() + 1);
        walkingDirection = -1;
        petVelocity.setX(-qAbs(petVelocity.x()) * 0.5);
    }
    if (nextPos.y() < rect.top()) {
        nextPos.setY(rect.top());
        petVelocity.setY(0);
    }
    if (nextPos.y() >= floorY) {
        const double landingSpeed = petVelocity.y();
        const bool wasFalling = motionMode == PetMotionMode::Falling;
        nextPos.setY(floorY);
        petVelocity.setY(0);
        if (wasFalling) {
            motionMode = PetMotionMode::Resting;
            lastOperationTimeMs = QDateTime::currentMSecsSinceEpoch();
            if (landingSpeed > 8.0) {
                changePetState(QStringLiteral(":/assets/warn.gif"));
                showBubble(QStringLiteral("晕乎乎……"));
                addMood(-2);
                interactionTimer->start(2500);
            } else {
                restorePetState();
            }
        }
    }
    move(nextPos.toPoint());
}

// 根据是否落地、是否有交互、番茄钟和自动走动开关判断运动模式。
void MainWindow::updateMotionMode(bool onGround)
{
    if (motionMode == PetMotionMode::Hanging) {
        return;
    }
    if (!onGround || motionMode == PetMotionMode::Falling) {
        motionMode = PetMotionMode::Falling;
        return;
    }
    if (!autoWalkEnabled || pomodoroMode != PomodoroMode::Idle || interactionTimer->isActive() || hasUnacknowledgedDDL) {
        motionMode = PetMotionMode::Resting;
        return;
    }

    const qint64 idleMs = QDateTime::currentMSecsSinceEpoch() - lastOperationTimeMs;
    if (idleMs < 6000) {
        motionMode = PetMotionMode::Resting;
    } else if (idleMs < 45000) {
        motionMode = PetMotionMode::Walking;
    } else {
        motionMode = PetMotionMode::Sleeping;
        changePetState(QStringLiteral(":/assets/idle.gif"));
    }
}

// 周期性降低饱腹和体力，并在状态过低时触发提醒。
void MainWindow::updatePetStatus()
{
    addHunger(-1);
    addEnergy(-1);
    if (hunger < 25) {
        addMood(-1);
    }
    if (energy < 20) {
        addMood(-1);
    }
    if (pomodoroMode == PomodoroMode::Idle && (mood < 25 || hunger < 20)) {
        changePetState(QStringLiteral(":/assets/warn.gif"));
        showBubble(hunger < 20 ? QStringLiteral("有点饿了……") : QStringLiteral("陪陪我嘛……"));
    }
}

// 随机触发说话、转向或开心动作，让桌宠更有生命感。
void MainWindow::triggerRandomAction()
{
    if (isDragging || isPressing || interactionTimer->isActive()) {
        return;
    }
    const int r = QRandomGenerator::global()->bounded(100);
    if (motionMode == PetMotionMode::Sleeping) {
        if (r < 20 && pomodoroMode == PomodoroMode::Idle) {
            showBubble(QStringLiteral("我先安静陪你一会儿……"));
        }
        return;
    }
    if (r < 20 && motionMode == PetMotionMode::Walking) {
        walkingDirection *= -1;
    } else if (r < 35 && pomodoroMode == PomodoroMode::Idle) {
        changePetState(QStringLiteral(":/assets/happy.gif"));
        showBubble(QStringLiteral("今天也要按时写作业喵~"));
        interactionTimer->start(2500);
    } else if (r < 45) {
        showBubble(QStringLiteral("右键可以管理 DDL 和番茄钟。"));
    }
}

// 普通点击视为摸头互动；悬挂状态下点击会让桌宠掉落。
void MainWindow::handleClickInteraction()
{
    if (motionMode == PetMotionMode::Falling) {
        return;
    }
    if (motionMode == PetMotionMode::Hanging) {
        motionMode = PetMotionMode::Falling;
        changePetState(QStringLiteral(":/assets/fall.gif"));
        showBubble(QStringLiteral("喵呜！掉下来了！"));
        return;
    }
    markUserOperation();
    addMood(5);
    addEnergy(1);
    changePetState(QStringLiteral(":/assets/happy.gif"));
    showBubble(QStringLiteral("摸摸头 +1"));
    interactionTimer->start(1200);
}

// 投喂小鱼干：提高饱腹和心情。
void MainWindow::feedPet()
{
    markUserOperation();
    addHunger(18);
    addMood(8);
    changePetState(QStringLiteral(":/assets/eat.gif"));
    showBubble(QStringLiteral("小鱼干真好吃！"));
    interactionTimer->start(5000);
}

// 逗猫玩：提高心情，但会消耗体力。
void MainWindow::playWithPet()
{
    markUserOperation();
    addMood(15);
    addEnergy(-6);
    changePetState(QStringLiteral(":/assets/play.gif"));
    showBubble(QStringLiteral("逗猫棒！开玩！"));
    interactionTimer->start(3500);
}

// 用气泡显示当前养成数值和运动状态。
void MainWindow::showPetStatus()
{
    const QString text = QStringLiteral("心情：%1/100\n饱腹：%2/100\n体力：%3/100\n状态：%4")
                             .arg(mood)
                             .arg(hunger)
                             .arg(energy)
                             .arg(motionModeText());
    showBubble(text, 3000);
}

// 显示桌宠气泡，并在指定时间后自动隐藏。
void MainWindow::showBubble(const QString &text, int msec)
{
    if (!bubbleLabel) {
        return;
    }
    bubbleLabel->setText(text);
    bubbleLabel->adjustSize();
    updateBubblePosition();
    bubbleLabel->show();
    bubbleTimer->start(msec);
}

// 根据桌宠当前位置更新气泡位置，并避免气泡超出屏幕。
void MainWindow::updateBubblePosition()
{
    if (!bubbleLabel) {
        return;
    }

    int bubbleX = x() + (width() - bubbleLabel->width()) / 2;
    int bubbleY = y() - bubbleLabel->height() - 10;
    QScreen *screen = QGuiApplication::screenAt(frameGeometry().center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen) {
        const QRect rect = screen->availableGeometry();
        bubbleX = qBound(rect.left(), bubbleX, rect.right() - bubbleLabel->width());
        if (bubbleY < rect.top()) {
            bubbleY = y() + height() + 10;
        }
    }
    bubbleLabel->move(bubbleX, bubbleY);
}

// 记录最近一次用户操作，用于控制自动走动和睡眠逻辑。
void MainWindow::markUserOperation()
{
    lastOperationTimeMs = QDateTime::currentMSecsSinceEpoch();
    if (motionMode != PetMotionMode::Falling && motionMode != PetMotionMode::Hanging) {
        motionMode = PetMotionMode::Resting;
    }
    hasUnacknowledgedDDL = false;
}

// 将运动枚举转换为用户可读的中文描述。
QString MainWindow::motionModeText() const
{
    switch (motionMode) {
    case PetMotionMode::Resting:
        return QStringLiteral("原地待机");
    case PetMotionMode::Walking:
        return QStringLiteral("自动走动");
    case PetMotionMode::Sleeping:
        return QStringLiteral("安静休息");
    case PetMotionMode::Falling:
        return QStringLiteral("下落中");
    case PetMotionMode::Hanging:
        return QStringLiteral("吸附在屏幕边缘");
    }
    return QStringLiteral("未知");
}

// 将心情值限制在 0 到 100 之间。
void MainWindow::addMood(int delta)
{
    mood = qBound(0, mood + delta, 100);
}

// 将饱腹值限制在 0 到 100 之间。
void MainWindow::addHunger(int delta)
{
    hunger = qBound(0, hunger + delta, 100);
}

// 将体力值限制在 0 到 100 之间。
void MainWindow::addEnergy(int delta)
{
    energy = qBound(0, energy + delta, 100);
}

// 将番茄钟剩余秒数格式化为 mm:ss。
QString MainWindow::formatSeconds(int seconds) const
{
    seconds = qMax(0, seconds);
    return QStringLiteral("%1:%2")
        .arg(seconds / 60, 2, 10, QChar('0'))
        .arg(seconds % 60, 2, 10, QChar('0'));
}

// 生成右键菜单中显示的番茄钟状态文本。
QString MainWindow::pomodoroStatusText() const
{
    if (pomodoroMode == PomodoroMode::Idle) {
        return QStringLiteral("当前状态：未开始");
    }
    QString modeText = pomodoroMode == PomodoroMode::Focus ? QStringLiteral("专注中") : QStringLiteral("休息中");
    if (isPomodoroPaused) {
        modeText += QStringLiteral("（已暂停）");
    }
    return QStringLiteral("当前状态：%1，剩余 %2").arg(modeText, formatSeconds(pomodoroSecondsRemaining));
}

// 开始 25 分钟专注计时，桌宠切换为学习状态。
void MainWindow::startFocusPomodoro()
{
    markUserOperation();
    pomodoroMode = PomodoroMode::Focus;
    pomodoroSecondsRemaining = 25 * 60;
    isPomodoroPaused = false;
    changePetState(QStringLiteral(":/assets/study.gif"));
    showBubble(QStringLiteral("开始 25 分钟专注。"), 3000);
    pomodoroTimer->start(1000);
}

// 开始 5 分钟休息计时，桌宠切换为开心状态。
void MainWindow::startBreakPomodoro()
{
    markUserOperation();
    pomodoroMode = PomodoroMode::Break;
    pomodoroSecondsRemaining = 5 * 60;
    isPomodoroPaused = false;
    changePetState(QStringLiteral(":/assets/happy.gif"));
    showBubble(QStringLiteral("进入 5 分钟休息时间。"), 3000);
    pomodoroTimer->start(1000);
}

// 暂停当前番茄钟。
void MainWindow::pausePomodoro()
{
    if (pomodoroMode == PomodoroMode::Idle || isPomodoroPaused) {
        return;
    }
    isPomodoroPaused = true;
    pomodoroTimer->stop();
    showBubble(QStringLiteral("番茄钟已暂停。"), 3000);
}

// 继续已暂停的番茄钟。
void MainWindow::resumePomodoro()
{
    if (pomodoroMode == PomodoroMode::Idle || !isPomodoroPaused) {
        return;
    }
    isPomodoroPaused = false;
    pomodoroTimer->start(1000);
    restorePetState();
    showBubble(QStringLiteral("番茄钟继续。"), 3000);
}

// 取消当前番茄钟并恢复普通状态。
void MainWindow::stopPomodoro()
{
    if (pomodoroMode == PomodoroMode::Idle) {
        return;
    }
    markUserOperation();
    pomodoroTimer->stop();
    pomodoroMode = PomodoroMode::Idle;
    pomodoroSecondsRemaining = 0;
    isPomodoroPaused = false;
    restorePetState();
    showBubble(QStringLiteral("番茄钟已取消。"), 3000);
}

// 每秒更新番茄钟倒计时，并在结束时给出提醒。
void MainWindow::updatePomodoro()
{
    if (pomodoroMode == PomodoroMode::Idle || isPomodoroPaused) {
        return;
    }

    --pomodoroSecondsRemaining;
    if (pomodoroSecondsRemaining > 0) {
        return;
    }

    pomodoroTimer->stop();
    if (pomodoroMode == PomodoroMode::Focus) {
        addMood(8);
        addEnergy(-3);
        pomodoroMode = PomodoroMode::Idle;
        pomodoroSecondsRemaining = 0;
        isPomodoroPaused = false;
        changePetState(QStringLiteral(":/assets/happy.gif"));
        raise();
        showBubble(QStringLiteral("25 分钟专注完成！记得休息一下。"), 5000);
        interactionTimer->start(5000);
    } else if (pomodoroMode == PomodoroMode::Break) {
        addEnergy(8);
        pomodoroMode = PomodoroMode::Idle;
        pomodoroSecondsRemaining = 0;
        isPomodoroPaused = false;
        changePetState(QStringLiteral(":/assets/happy.gif"));
        raise();
        showBubble(QStringLiteral("5 分钟休息结束，准备下一轮吧。"), 5000);
        interactionTimer->start(5000);
    }
}

// 定时检查 DDL：24 小时内提醒，已截止则切换为更焦急的状态。
void MainWindow::checkDDLTasks()
{
    QVector<DDLTask> &tasks = ddlDialog->getTasks();
    const QDateTime now = QDateTime::currentDateTime();
    bool needsSave = false;
    bool needsRefresh = false;

    for (int i = tasks.size() - 1; i >= 0; --i) {
        DDLTask &task = tasks[i];
        const qint64 seconds = now.secsTo(task.deadline);

        if (!task.deadline.isValid() || seconds < -24 * 3600) {
            tasks.removeAt(i);
            needsSave = true;
            needsRefresh = true;
            continue;
        }

        if (seconds < 0 && !task.expiredNotified) {
            hasUnacknowledgedDDL = true;
            unacknowledgedDDLState = QStringLiteral(":/assets/angry.gif");
            changePetState(unacknowledgedDDLState);
            addMood(-8);
            raise();
            showBubble(QStringLiteral("喵呜！\n《%1》已经截止了！").arg(task.name), 6000);
            task.expiredNotified = true;
            needsSave = true;
            needsRefresh = true;
        } else if (seconds >= 0 && seconds <= 24 * 3600 && !task.reminded) {
            hasUnacknowledgedDDL = true;
            unacknowledgedDDLState = QStringLiteral(":/assets/warn.gif");
            changePetState(unacknowledgedDDLState);
            addMood(-3);
            raise();
            showBubble(QStringLiteral("DDL 提醒：\n《%1》将在 24 小时内截止！").arg(task.name), 6000);
            task.reminded = true;
            needsSave = true;
            needsRefresh = true;
        }
    }

    if (needsSave) {
        ddlDialog->saveTasksToFile();
    }
    if (needsRefresh) {
        ddlDialog->refreshTaskListUI();
    }
}

// GIF 每帧刷新时保存当前图像，并根据透明通道裁剪窗口形状。
void MainWindow::onPetFrameChanged()
{
    if (!currentMovie || currentMovie->state() != QMovie::Running) {
        return;
    }

    QImage frame = currentMovie->currentImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (walkingDirection == 1 && currentGifPath.contains(QStringLiteral("run"))) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        frame = frame.flipped(Qt::Horizontal);
#else
        frame = frame.mirrored(true, false);
#endif
    }

    const QImage displayFrame = frame.scaled(size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    currentPetPixmap = QPixmap::fromImage(displayFrame);

    const QBitmap mask = QBitmap::fromImage(displayFrame.createAlphaMask(Qt::AvoidDither));
    if (!mask.isNull()) {
        setMask(mask);
    }
    update();
}

// 自绘桌宠当前帧，同时清空透明背景，避免矩形残影。
void MainWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), QColor(0, 0, 0, 0));

    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    if (!currentPetPixmap.isNull()) {
        painter.drawPixmap(rect(), currentPetPixmap);
    }
}

// 窗口显示后再次移除系统边框，避免部分系统环境下样式恢复。
void MainWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    removeSystemWindowBorder(this);
}

// 在猫形态和人形态之间切换，并恢复当前应显示的状态。
void MainWindow::toggleHumanForm()
{
    isHumanForm = !isHumanForm;
    markUserOperation();
    showBubble(isHumanForm ? QStringLiteral("变身！罗小黑人形态！") : QStringLiteral("变回小猫。"), 3000);
    restorePetState();
}

// 桌宠移动时同步更新气泡位置。
void MainWindow::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    updateBubblePosition();
}
