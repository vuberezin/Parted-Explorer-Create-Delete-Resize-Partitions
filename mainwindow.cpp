#include "mainwindow.h"
#include <QVBoxLayout>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <iostream>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    // Setup UI elements
    resize(1000, 400);
    treeWidget = new QTreeWidget(this);
    treeWidget->setColumnCount(7);
    treeWidget->setHeaderLabels({"Device/Partition", "Size (GB)", "Start (MB)", "End (MB)", "Type", "File System", "Flags"});

    refreshButton = new QPushButton("Refresh", this);
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::refreshDiskList);

    createButton = new QPushButton("Create Partition", this);
    connect(createButton, &QPushButton::clicked, this, &MainWindow::onCreatePartitionClicked);

    deleteButton = new QPushButton("Delete Partition", this);
    connect(deleteButton, &QPushButton::clicked, this, &MainWindow::onDeletePartitionClicked);

    resizeButton = new QPushButton("Resize Partition", this);
    connect(resizeButton, &QPushButton::clicked, this, &MainWindow::onResizePartitionClicked);

    createDiskLabelButton = new QPushButton("Set Disk Partition Flag", this);
    connect(createDiskLabelButton, &QPushButton::clicked, this, &MainWindow::oncCreateDiskFlagClicked);

    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(centralWidget);
    QHBoxLayout *buttonLayout = new QHBoxLayout();

    buttonLayout->addWidget(refreshButton);
    buttonLayout->addWidget(createButton);
    buttonLayout->addWidget(deleteButton);
    buttonLayout->addWidget(resizeButton);
    buttonLayout->addWidget(createDiskLabelButton);

    layout->addLayout(buttonLayout);
    layout->addWidget(treeWidget);
    setCentralWidget(centralWidget);
    setWindowTitle("Qt Parted Explorer");

    refreshDiskList(); // Initial population

}

MainWindow::~MainWindow() {}

void MainWindow::refreshDiskList() {
    treeWidget->clear();
    std::vector<DeviceInfo> devices = diskManager.listAllDevices();
    displayDevices(devices);
}

void MainWindow::displayDevices(const std::vector<DeviceInfo>& devices) {
    for (const auto& dev : devices) {
        QTreeWidgetItem *devItem = new QTreeWidgetItem(treeWidget);
        treeWidget->setColumnWidth(0, 300);
        treeWidget->setColumnWidth(5, 160);
        devItem->setText(0, QString("%1 (%2)").arg(dev.model).arg(dev.path));
        devItem->setText(1, QString::number(dev.size / (1024*1024*1024.0), 'f', 2));
        devItem->setData(0, Qt::UserRole, dev.path); // Store path for operations
        treeWidget->addTopLevelItem(devItem);

        for (const auto& part : dev.partitions) {
            QTreeWidgetItem *partItem = new QTreeWidgetItem(devItem);
            QString name = part.isFreeSpace ? "Free Space" : QString("Partition %1").arg(part.number);
            partItem->setText(0, name);
            partItem->setText(1, QString::number(part.size / (1024*1024*1024.0), 'f', 2));
            partItem->setText(2, QString::number(part.start / (1024*1024.0), 'f', 2));
            partItem->setText(3, QString::number(part.end / (1024*1024.0), 'f', 2));
            partItem->setText(4, part.type);
            partItem->setText(5, part.fileSystem);
            partItem->setText(6, part.flags);
            partItem->setData(0, Qt::UserRole, part.number); // Store partition number
            partItem->setData(1, Qt::UserRole, part.devicePath); // Store device path
            partItem->setData(2, Qt::UserRole, part.isFreeSpace); // Store isFreeSpace flag
            devItem->addChild(partItem);
        }
    }
    treeWidget->expandAll();
}

// Helper to get info from selected item
PartitionInfo MainWindow::getSelectedPartitionInfo() {
    QTreeWidgetItem *currentItem = treeWidget->currentItem();
    if (!currentItem || currentItem->parent() == nullptr) return {}; // Need a partition item

    PartitionInfo info;
    info.number = currentItem->data(0, Qt::UserRole).toInt();
    info.devicePath = currentItem->data(1, Qt::UserRole).toString();
    info.isFreeSpace = currentItem->data(2, Qt::UserRole).toBool();
    info.start =  currentItem->text(2).toDouble();
    info.end =  currentItem->text(3).toDouble();
    //qDebug() << "Info.start: " << info.start << "Info.end: " << info.end;
    // Retrieve other data from the display text (simplification)
    //info.size = (info.end - info.start)/1024; //currentItem->text(1).toDouble(); // Approx size
    //qDebug() << "Info.start: " << info.start << "Info.end: " << info.end << "info.size: " << info.size;
    return info;
}

QString MainWindow::getSelectedDevicePath() {
    QTreeWidgetItem *currentItem = treeWidget->currentItem();
    if (!currentItem) return {};

    if (currentItem->parent() == nullptr) { // It's a device item
        return currentItem->data(0, Qt::UserRole).toString();
    } else { // It's a partition item, get parent's path
        return currentItem->parent()->data(0, Qt::UserRole).toString();
    }
}


void MainWindow::onCreatePartitionClicked() {
    // Requires selecting a "Free Space" partition
    PartitionInfo pInfo = getSelectedPartitionInfo();
    if (pInfo.devicePath.isEmpty() || !pInfo.isFreeSpace) {
        QMessageBox::warning(this, "Error", "Please select a 'Free Space' entry to create a new partition.");
        return;
    }

    bool ok;
    // Calculate total available free space size for the prompt (e.g., convert sectors to MB)
    long long totalFreeSpaceMB = (pInfo.end - pInfo.start); // (1024 * 1024);
    qDebug() << "pInfo.start: " << pInfo.start << "pInfo.end: " << pInfo.end << "totalFreeSpaceMB: "<< totalFreeSpaceMB;

    // 1. Get the desired size from the user
    int desiredSizeMB = QInputDialog::getInt(
        this,
        "Create Partition",
        QString("Enter desired size in MB (Max available: %1 MB):").arg(totalFreeSpaceMB),
        totalFreeSpaceMB, // Default value
        1,              // Minimum size
        totalFreeSpaceMB, // Maximum size
        1,              // Step
        &ok
        );

    if (!ok) {
        // User cancelled the input dialog
        return;
    }

    // 2. Get the file system type from the user
    QString fsType = QInputDialog::getText(
        this,
        "Create Partition",
        "Enter File System Type (e.g., ext4, ntfs):",
        QLineEdit::Normal,
        "ext4",
        &ok
        );

    if (ok && !fsType.isEmpty()) {
        // 3. Calculate the new end sector based on the desired size in MB
        // Assumes 512-byte sectors (standard) - adjust if your system uses 4096-byte sectors
        long long sizeInMB = (long long)desiredSizeMB;// * 1024 * 1024 / 512;
        long long newEndMB = pInfo.start /* 1024 * 1024 / 512)*/ + sizeInMB;

        // Ensure the new end is not past the original free space end (QInputDialog handles the size limit, but good practice)
        if (newEndMB > pInfo.end) {
            newEndMB = pInfo.end;
        }

        // 4. Call createPartition with the specified start and *new* end points
        // Assuming diskManager.createPartition uses start and end sectors
        //qDebug() << "fsType: " << fsType;
        if (diskManager.createPartition(pInfo.devicePath, pInfo.start, newEndMB, fsType, true)) {
            QMessageBox::information(this, "Success", "Partition created. You may need to run 'partprobe' in terminal to update OS view.");
            refreshDiskList();
        } else {
            QMessageBox::critical(this, "Failed", "Failed to create partition. Check root privileges and console output.");
        }
    }
}


void MainWindow::onDeletePartitionClicked() {
    // Requires selecting an actual partition (not free space)
    PartitionInfo pInfo = getSelectedPartitionInfo();
    if (pInfo.devicePath.isEmpty() || pInfo.isFreeSpace || pInfo.number <= 0) {
        QMessageBox::warning(this, "Error", "Please select an active partition to delete.");
        return;
    }

    if (QMessageBox::question(this, "Confirm Delete", QString("Are you sure you want to delete Partition %1 on %2? This will erase all data!").arg(pInfo.number).arg(pInfo.devicePath), QMessageBox::Yes | QMessageBox::No) == QMessageBox::No) {
        return;
    }

    if (diskManager.deletePartition(pInfo.devicePath, pInfo.number)) {
        QMessageBox::information(this, "Success", "Partition deleted. You may need to run 'partprobe' in terminal to update OS view.");
        refreshDiskList();
    } else {
        QMessageBox::critical(this, "Failed", "Failed to delete partition. Ensure it is unmounted and check root privileges.");
    }
}

void MainWindow::onResizePartitionClicked() {
    // Requires selecting an active partition
    PartitionInfo pInfo = getSelectedPartitionInfo();
    if (pInfo.devicePath.isEmpty() || pInfo.isFreeSpace || pInfo.number <= 0) {
        QMessageBox::warning(this, "Error", "Please select an active partition to resize.");
        return;
    }

    bool ok;
    // Simple input for new size in GB
    double currentSizeMB = (pInfo.end - pInfo.start); // (1024*1024*1024.0);
    qDebug() << "currentSizeGB: " << pInfo.start << "####" << pInfo.end;
    double newSizeMB = QInputDialog::getDouble(this, "Resize Partition", QString("Enter new size in MB (Current: %1 MB):").arg(currentSizeMB, 0, 'f', 2), currentSizeMB, 1.0, 100000.0, 2, &ok);

    if (ok) {
        long long newEndBytes = pInfo.start + newSizeMB; // * 1024 * 1024 * 1024;
        if (diskManager.resizePartition(pInfo.devicePath, pInfo.number, newEndBytes)) {
            QMessageBox::information(this, "Success", "Partition geometry resized. Note: Filesystem resize must be done separately (e.g., using resize2fs via command line).");
            refreshDiskList();
        } else {
            QMessageBox::critical(this, "Failed", "Failed to resize partition. Ensure it is unmounted and check root privileges.");
        }
    }
}

void MainWindow::oncCreateDiskFlagClicked() {

    PartitionInfo pInfo = getSelectedPartitionInfo();
    if (pInfo.devicePath.isEmpty() || pInfo.isFreeSpace || pInfo.number <= 0) {
        QMessageBox::warning(this, "Error", "Please select an active partition to manage a disk flag.");
        return;
    }

    bool ok;
    QString flagName = QInputDialog::getText( // Renamed from diskLabelType
        this,
        "Create/Set Disk Partition Flag",
        "Enter Disk partition flag (e.g., boot, esp):",
        QLineEdit::Normal,
        "boot",
        &ok
        );

    if (!ok || flagName.isEmpty()) {
        return;
    }

    // We need a way to get the *actual* raw device pointer (PedDevice *dev) here.
    PedDevice *dev = diskManager.getDeviceFromPath(pInfo.devicePath);

    // Convert the string name to the enum value
    PedPartitionFlag flagToSet = diskManager.flagNameToEnum(flagName.toStdString());
    qDebug() << "dev->path: " << dev->path << "pInfo.number: " << pInfo.number;
    // Check if the conversion was successful and we have a valid device pointer
    if (dev != nullptr && flagToSet >= 0) { // Assuming flagNameToEnum returns -1 or similar on failure
        // Now call the function with the correct, actual variables
        // We assume we are always setting the flag to 'true' (state = true)
        if (diskManager.setPartitionFlag(dev, pInfo.number, flagToSet, true)) {
            QMessageBox::information(this, "Success", QString("Disk flag '%1' set successfully.").arg(flagName));
            refreshDiskList();
        } else {
            QMessageBox::critical(this, "Failed", "Failed to set disk flag. Check root privileges and console output.");
        }
    } else {
        QMessageBox::critical(this, "Failed", "Invalid flag name entered or device not found.");
    }
}

