#ifndef DISKMANAGER_H
#define DISKMANAGER_H

#include <QList>
#include <QString>
#include <parted/parted.h>
#include <parted/device.h>
#include <parted/disk.h>
#include <parted/filesys.h>
#include <parted/exception.h>
#include <vector>

// Structure to hold partition details
struct PartitionInfo {
    int number;
    QString type;
    QString fileSystem;
    long long start;
    long long end;
    long long size; // in bytes
    QString flags;
    bool isFreeSpace = false;
    QString devicePath; // To know which device it belongs to
};

// Structure to hold device details
struct DeviceInfo {
    QString model;
    QString path;
    long long size; // in bytes
    std::vector<PartitionInfo> partitions;
};

class DiskManager {
public:
    DiskManager();
    ~DiskManager();
    std::vector<DeviceInfo> listAllDevices();

    // Disk operations (require root privileges)
    bool createPartition(const QString& devicePath, long long startBytes, long long endBytes, const QString& fsType, bool isPrimary);
    bool deletePartition(const QString& devicePath, int partitionNumber);
    bool resizePartition(const QString& devicePath, int partitionNumber, long long newEndMBytes);
    bool format_ext4_library(const char* partition_path);

private:
    QString getPartitionFlags(PedPartition *partition);
    // Helper for exception handling in libparted
    static PedExceptionOption exceptionHandler(PedException *exception);
};

#endif // DISKMANAGER_H
