#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTreeWidget>
#include <QPushButton>
#include "diskmanager.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void refreshDiskList();
    void onCreatePartitionClicked();
    void onDeletePartitionClicked();
    void onResizePartitionClicked();
    void oncCreateDiskFlagClicked();

private:
    DiskManager diskManager;
    QTreeWidget *treeWidget;
    QPushButton *refreshButton;
    QPushButton *createButton;
    QPushButton *deleteButton;
    QPushButton *resizeButton;
    QPushButton *createDiskLabelButton;

    void displayDevices(const std::vector<DeviceInfo>& devices);
    PartitionInfo getSelectedPartitionInfo();
    QString getSelectedDevicePath();
};
#endif // MAINWINDOW_H
