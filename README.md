# DesktopPetDDL

DesktopPetDDL 是一个基于 C++ 和 Qt Widgets 的桌面宠物项目，主题为“罗小黑桌宠管家”。项目集成了动态桌宠、DDL 提醒、番茄钟和简单养成互动，适合作为课程大作业展示。

## 已实现功能

### 动态桌宠

- 透明背景、无边框、始终置顶的桌宠窗口
- 使用 `QMovie` 播放 `assets` 中的 GIF 动画
- 支持猫形态和人形态资源切换
- 支持闲置、开心、提醒、生气、吃东西、学习、跑步、下落、吸附等状态
- 长时间无操作后可自动沿屏幕底部走动

### 鼠标交互

- 左键点击触发互动反馈
- 左键拖动移动桌宠
- 松手后带简单下落效果
- 靠近屏幕左右边缘时可吸附在边缘
- 气泡提示会跟随桌宠位置移动

### 右键菜单

- 打开 DDL 管理
- 打开番茄钟
- 查看状态
- 投喂小鱼干
- 逗猫玩
- 开启或关闭自动走动
- 手动切换动画
- 切换猫形态 / 人形态
- 退出程序

### DDL 管理

- 添加 DDL 任务并设置截止时间
- 删除选中的 DDL 任务
- 按截止时间自动排序
- 使用 JSON 文件保存任务数据
- 程序重启后自动加载历史任务
- 任务在 24 小时内截止时提醒
- 任务已经截止时再次提醒并切换桌宠状态

### 番茄钟

- 25 分钟专注计时
- 5 分钟休息计时
- 支持暂停、继续、取消
- 专注时桌宠切换到学习状态
- 计时结束后通过气泡提示用户

### 简单养成状态

- 心情值
- 饱腹值
- 体力值

投喂、点击互动、逗猫和番茄钟会影响这些数值；数值较低时桌宠会切换到提醒状态。

## 项目结构

```text
DesktopPetDDL
├── DesktopPetDDL.pro
├── main.cpp
├── mainwindow.h
├── mainwindow.cpp
├── mainwindow.ui
├── ddldialog.h
├── ddldialog.cpp
├── ddldialog.ui
├── res.qrc
├── assets/
│   ├── idle.gif
│   ├── happy.gif
│   ├── warn.gif
│   ├── angry.gif
│   ├── eat.gif
│   ├── play.gif
│   ├── run.gif
│   ├── study.gif
│   ├── fall.gif
│   ├── lift.gif
│   ├── left1.gif
│   ├── right1.gif
│   └── 1*.gif
└── data/
```

## 构建与运行

开发环境建议：

- Windows
- Qt Creator
- Qt 6
- MinGW 64-bit
- C++ / Qt Widgets

运行方式：

1. 用 Qt Creator 打开 `DesktopPetDDL.pro`
2. 选择合适的 Qt Kit
3. 建议使用 Release 模式构建
4. 点击运行按钮启动程序

如果使用命令行构建，请先让 qmake 重新生成 Makefile，再运行 `mingw32-make`。

## 关键类说明

### MainWindow

桌宠主窗口，负责透明置顶窗口、GIF 绘制、鼠标拖拽、右键菜单、气泡提示、番茄钟和 DDL 定时检查。

### DDLDialog

DDL 管理对话框，负责任务输入、任务列表显示、JSON 持久化和任务增删信号。

### DDLTask

任务数据结构，包含任务名称、截止时间、是否已经提前提醒、是否已经过期提醒。

## 后续可扩展方向

- 将番茄钟拆分为独立 `PomodoroTimer` 类
- 将桌宠状态拆分为状态模式类
- 增加任务完成勾选功能
- 增加系统托盘和开机自启动设置
- 增加更丰富的动画和音效
