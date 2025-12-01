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
    // Clear existing items and reset map for the robust single-pass approach
    treeWidget->clear();

    // Map to keep track of the parent QTreeWidgetItem* for extended partitions, keyed by device path/identifier
    QMap<QString, QTreeWidgetItem*> extendedPartitionsMap;

    for (const auto& dev : devices) {
        QTreeWidgetItem *devItem = new QTreeWidgetItem(treeWidget);
        treeWidget->setColumnWidth(0, 300);
        treeWidget->setColumnWidth(5, 160);

        // Display the main device name and path (e.g., "Hitachi 500GB (/dev/sda)")
        devItem->setText(0, QString("%1 (%2)").arg(dev.model).arg(dev.path));

        // Display size formatted to 2 decimal places in GB
        devItem->setText(1, QString::number(dev.size / (1024.0 * 1024.0 * 1024.0), 'f', 2));

        // Store the main device path internally in the root item
        devItem->setData(0, Qt::UserRole, dev.path);
        treeWidget->addTopLevelItem(devItem);

        // Iterate through all partitions in this device
        for (const auto& part : dev.partitions) {
            QTreeWidgetItem* parentItem = devItem;

            // Determine if this is the extended container definition using string comparison
            bool isExtendedContainer = part.type.contains("Extended", Qt::CaseInsensitive) || part.type.contains("0x05");

            if (isExtendedContainer) {
                // Create the extended container item itself
                QTreeWidgetItem *extItem = new QTreeWidgetItem(devItem);
                extItem->setText(0, QString("Extended Partition Container"));
                extItem->setText(1, QString::number(part.size / (1024.0 * 1024.0 * 1024.0), 'f', 2));

                // Map this item so subsequent logical/free spaces can find their parent QWidgetItem
                // The key should be the identifier that links logical partitions back to this container
                extendedPartitionsMap.insert(part.devicePath, extItem);

            } else if (extendedPartitionsMap.contains(part.devicePath)) {
                // If this partition/free space belongs to an extended partition we've mapped,
                // set the parent to the mapped container item instead of the main device item
                parentItem = extendedPartitionsMap.value(part.devicePath);
            }

            // Determine the descriptive name for the current partition/free space
            QString name;
            if (part.isFreeSpace) {
                name = "Free Space";
            } else if (parentItem != devItem) {
                // If the parent is the extended container (not the top-level device)
                name = QString("Logical Partition %1").arg(part.number);
            } else {
                // Must be a primary partition
                name = QString("Partition %1").arg(part.number);
            }

            // Create the actual partition/free space item
            QTreeWidgetItem *partItem = new QTreeWidgetItem(parentItem);

            // Set display text for all columns
            partItem->setText(0, name); // Displays just the name, without device path
            partItem->setText(1, QString::number(part.size / (1024.0 * 1024.0 * 1024.0), 'f', 2)); // Size in GB
            partItem->setText(2, QString::number(part.start / (1024.0 * 1024.0), 'f', 2)); // Start in MB
            partItem->setText(3, QString::number(part.end / (1024.0 * 1024.0), 'f', 2));   // End in MB
            partItem->setText(4, part.type);
            partItem->setText(5, part.fileSystem);
            partItem->setText(6, part.flags);

            // Store internal data using User Roles (for reliable data retrieval later)
            // Ensure you use consistent indices across displayDevices and getSelectedPartitionInfo
            partItem->setData(0, Qt::UserRole + 0, part.number);        // Partition Number
            partItem->setData(0, Qt::UserRole + 1, part.devicePath);    // Device Path (e.g., /dev/sda5)
            partItem->setData(0, Qt::UserRole + 2, part.isFreeSpace);   // Is Free Space Flag
            partItem->setData(0, Qt::UserRole + 3, part.size);          // Raw Size (bytes/double)
            partItem->setData(0, Qt::UserRole + 4, part.start);        // Raw Start (bytes/MB/double)
            partItem->setData(0, Qt::UserRole + 5, part.end);          // Raw End (bytes/MB/double)
        }
    }
    // Ensure all items are visible in their hierarchy
    treeWidget->expandAll();
}



// Helper to get info from selected item
PartitionInfo MainWindow::getSelectedPartitionInfo() {
    QTreeWidgetItem *currentItem = treeWidget->currentItem();

    // Check if an item is selected and it is a child (a partition/free space, not the main device or the extended container)
    // You might want to allow selection of the "Extended Partition Container" if you add data storage to that item too.
    if (!currentItem || currentItem->parent() == nullptr) {
        return {};
    }

    PartitionInfo info;

    // Retrieve all data from the stored User Roles
    // (We use Qt::UserRole + index to avoid collisions if multiple columns have user data)
    info.number = currentItem->data(0, Qt::UserRole + 0).toInt();
    info.devicePath = currentItem->data(0, Qt::UserRole + 1).toString();
    info.isFreeSpace = currentItem->data(0, Qt::UserRole + 2).toBool();

    // Retrieve raw numerical values safely using toDouble() or qulonglong/quint64 if using bytes
    // Adjust the types based on how you stored them in displayDevices (e.g., as raw bytes or a double representation of MB/GB)
    info.start =  currentItem->text(2).toDouble();
    info.end =  currentItem->text(3).toDouble();

    // Your debug statements
    // qDebug() << "Info.start: " << info.start << "Info.end: " << info.end << "info.size: " << info.size;

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
     if (!ok || fsType.isEmpty()) return;

    // 3. Get the Partition type from the user
    QString PartitionType = QInputDialog::getText(
        this,
        "Create Partition",
        "Enter Partition Type (e.g., primary, extended, logical):",
        QLineEdit::Normal,
        "primary",
        &ok
        );
    // Perform custom validation (e.g., check Partition Type)
     if (PartitionType == "primary" && PartitionType == "extended" && PartitionType == "logical") {
         // Input is valid, break the loop
         return;
     } else {
         // Input is invalid, show a warning and the loop continues
         QMessageBox::warning(this, tr("Invalid Input"),
                              tr("Partition Type must be primary, extended or logical. Please try again."));
         return;
     }
     if (!ok || PartitionType.isEmpty()) return;

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
        if (diskManager.createPartition(pInfo.devicePath, pInfo.start, newEndMB, fsType, PartitionType)) {
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


