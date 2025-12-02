#include "diskmanager.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <mntent.h>
#include <ext2fs/ext2_fs.h>
#include <ext2fs/ext2fs.h>
#include <uuid/uuid.h>
#include <iostream>
#include <QDebug>
#include <QProcess>
#include <QMessageBox>



 DiskManager::DiskManager() {
     // Set a custom exception handler to catch libparted errors gracefully
     ped_exception_set_handler(DiskManager::exceptionHandler);
}

DiskManager::~DiskManager() {
    // libparted automatically cleans up at exit.
}

PedExceptionOption DiskManager::exceptionHandler(PedException *exception) {
    qCritical() << "libparted Exception:" << exception->message;
    // QMessageBox msgBox;
    // msgBox.setWindowTitle("Libparted Exception");
    // msgBox.setText("libparted Exception:" + QString::fromUtf8(exception->message));
    return PED_EXCEPTION_FIX; // Try to fix the issue if possible, otherwise it may abort.
}

// bool DiskManager::format_ext4_library(const char* partition_path) {
//     errcode_t retval;
//     ext2_filsys fs;
//     blk64_t blocks_count = 0; // Use 0 to use all available space on the partition
//     int block_size = 4096;
//     const char *fs_type_opts = "ext4";

//     // 1. Setup I/O Manager (standard choice for basic block device access)
//     io_manager io_mgr = unix_io_manager;

//     // 2. Open the device using the I/O manager and calculate basic geometry
//     // This step allocates memory for the 'fs' structure but doesn't write to disk yet.
//     retval = ext2fs_open(partition_path, EXT2_FLAG_RW | EXT2_FLAG_EXCLUSIVE, 0, block_size, io_mgr, &fs);

//     if (retval) {
//         fprintf(stderr, "Error opening partition %s: %s\n", partition_path, error_message(retval));
//         return false;
//     }

//     // Set filesystem name/volume name if needed
//     // strcpy(fs->super->s_volume, "MyNewExt4Partition");

//     // 3. Initialize the filesystem structure on the disk
//     // This function writes the actual filesystem data/superblocks
//     // The parameters are: filesystem structure, flags, options string, I/O manager

//     retval = ext2fs_initialize(fs->device_name, 0, block_size, fs_type_opts, io_mgr);

//     if (retval) {
//         fprintf(stderr, "Error initializing ext4 filesystem on %s: %s\n", partition_path, error_message(retval));
//         ext2fs_close(fs);
//         return false;
//     }

//     printf("Successfully created ext4 filesystem on %s\n", partition_path);

//     // 4. Close the device, flushing all changes to disk
//     ext2fs_close(fs);
//     return true;
// }


std::vector<DeviceInfo> DiskManager::listAllDevices() {
    std::vector<DeviceInfo> devicesList;
    ped_device_probe_all();

    PedDevice *device = nullptr;
    //"/dev/loop0" is used for safe testing only
    const char* loop_device_path = "/dev/loop0";
    PedDevice* loop_dev = ped_device_get(loop_device_path);
    while ((device = ped_device_get_next(device)) != nullptr) {
        // Skip busy devices for listing to avoid issues
        //if (ped_device_is_busy(device)) continue; // we dont use it for tests on /dev/loop0

        DeviceInfo info;
        info.model = QString::fromUtf8(device->model);
        info.path = QString::fromUtf8(device->path);
        info.size = device->length * device->sector_size;

        PedDisk *disk = ped_disk_new(device);
        if (disk) {
            PedPartition *partition = nullptr;
            while ((partition = ped_disk_next_partition(disk, partition)) != nullptr) {
                // Include free space partitions for operation targeting
                 if (!ped_partition_is_active(partition) && !(partition->type & PED_PARTITION_FREESPACE)) {
                     continue;
                 }

                PartitionInfo pInfo;
                pInfo.number = partition->num;
                pInfo.type = QString::fromUtf8(ped_partition_type_get_name(partition->type));
                pInfo.isFreeSpace = (partition->type & PED_PARTITION_FREESPACE);
                pInfo.start = (long long)partition->geom.start * (long long)device->sector_size;
                pInfo.end = (long long)partition->geom.end * (long long)device->sector_size;
                pInfo.size = pInfo.end - pInfo.start;
                // Check if fs_type pointer is valid, then access its internal 'name' field
                pInfo.fileSystem = (partition->fs_type)
                                       ? QString::fromUtf8(partition->fs_type->name)
                                       : "Unknown/None";

                // --- FIX: Use ped_file_system_probe instead of partition->fs ---
                const PedFileSystemType *fs_type = ped_file_system_probe(&(partition->geom));
                if (fs_type) {
                    //const char* fs_name = ped_file_system_type_get_name(fs_type);
                    const char* fs_name = fs_type->name;
                    pInfo.fileSystem = QString::fromUtf8(fs_name);
                } else {
                    pInfo.fileSystem = "Unknown/None";
                }
                // ---------------------------------------------------------------
                //qDebug() << "###info.path: "  << info.path;
                pInfo.flags = getPartitionFlags(partition);
                pInfo.devicePath = info.path;


                info.partitions.push_back(pInfo);
            }
            ped_disk_destroy(disk);
            //ped_device_close(device);
        }
        devicesList.push_back(info);
    }

    return devicesList;
}


QString DiskManager::getPartitionFlags(PedPartition *partition) {
    QString flags;

    if (!partition) {
        return flags; // Handle null pointer
    }

    if (partition->type & PED_PARTITION_FREESPACE) {
        qDebug() << "Error: Partition is not active because there is Free space.";
        // Free space is a "type", but usually not what we consider an "active partition"
        // Return true/false based on your specific requirements (usually false for operations)
         return flags;
    }

    // This is the critical check that matches the assertion failure:
    if (!ped_partition_is_active(partition)) {
        qDebug() << "Error: Partition is not active (parent disk context lost?).";
        return flags;
    }

    // Start iteration of all available flag types from the beginning
    //PedPartitionFlag currentFlag = PED_PARTITION_BOOT; // Start with the first known flag

    // The function ped_partition_flag_get_next acts as an iterator.
    // We loop through *all* possible PedPartitionFlags defined in the library.
    // We stop when ped_partition_flag_get_next returns an invalid/unknown flag (typically 0 or an indicator of end).

    // Note: The loop condition might vary slightly depending on the exact libparted version,
    // but a standard way to iterate is to use the sequential nature:

    for (int i = 0; i < PED_PARTITION_LAST_FLAG; ++i) {
        PedPartitionFlag flag = (PedPartitionFlag)(1 << i);
        const char* flag_name_c = ped_partition_flag_get_name(flag);

        // Check if this specific flag bit is a valid, recognized flag type name
        if (flag_name_c != nullptr) {
            // Check if this specific flag bit is SET on the actual partition object
            if (ped_partition_get_flag(partition, flag)) {
                if (!flags.isEmpty()) {
                    flags += ", ";
                }
                flags += QString::fromUtf8(flag_name_c);
            }
        }
    }

    return flags;

}

// --- Disk Operations ---

bool DiskManager::createPartition(const QString& devicePath, long long startBytes, long long endBytes, const QString& fsType,  const QString& PartitionType) {
    PedDevice *dev = ped_device_get(devicePath.toUtf8().constData());
    if (!dev) return false;

    PedDisk *disk = ped_disk_new(dev);
    if (!disk) {
        ped_device_close(dev);
        return false;
    }

    // Ensure startBytes < endBytes in your calling logic!
    // E.g., if you have 10GB free, startBytes might be 0 (of free space) and endBytes 10000.

    PedSector startSector = (startBytes * 1024 * 1024 / dev->sector_size);
    PedSector endSector = endBytes *  1024 * 1024 / dev->sector_size;
    qDebug() << "startSector:  " << startSector << "endSector:  " << endSector;


    if (endSector <= startSector) {
        qDebug() << "Invalid partition size or end sector calculation.";
        // Clean up disk object before returning
        ped_disk_destroy(disk);
        return false;
    }

    //PedPartitionType type = isPrimary ? PED_PARTITION_NORMAL : PED_PARTITION_EXTENDED;
    const PedFileSystemType *fsTypePtr = ped_file_system_type_get(fsType.toUtf8().constData());
    PedPartitionType type;

    if (PartitionType == QString::fromStdString("primary")) {
        type = PED_PARTITION_NORMAL;
        }
    else if(PartitionType == QString::fromStdString("extended")){
        type = PED_PARTITION_EXTENDED;
        // Assume 'fsTypePtr' is defined often NULL for extended containers as they don't hold a direct filesystem
        fsTypePtr = NULL;
        }
    else if(PartitionType == QString::fromStdString("logical")){
        type = PED_PARTITION_LOGICAL;
        }
    else{
        qDebug() << "Invalid Partition Type specified: " << PartitionType;
        return false; // Error handling
        }

    // Use aligned partition creation
    PedConstraint *constraint = ped_constraint_any(dev);
    PedPartition *newPartition = ped_partition_new(disk, type, fsTypePtr, startSector, endSector);

    if (!newPartition) {
        qDebug() << "Failed to create new partition object (likely alignment or space issue).";
        ped_constraint_destroy(constraint);
        ped_disk_destroy(disk);
        return false;
    }

    qDebug() << "New Partition Num: " << newPartition->num;
    //qDebug() << "Actual Start: " << newPartition->start << " Actual End: " << newPartition->end;

    // ped_disk_add_partition returns 0 on failure
    if (!ped_disk_add_partition(disk, newPartition, constraint)) {
        qDebug() << "Failed to add partition to disk structure.";
        ped_constraint_destroy(constraint);
        ped_disk_destroy(disk);
        return false;
    }

    // ped_disk_commit returns 0 on failure
    bool success = ped_disk_commit(disk);
    if (!success) {
        qDebug() << "Failed to commit changes to disk. The libparted exception likely occurred here.";
    }

    if (success) {
        // After committing, you might want to format the filesystem (e.g., using mkfs external process)
        qDebug() << "Partition created successfully. Committing changes.";

        // Example using QProcess to run mkfs.ext4 (for Linux systems):

        //1. Determine the device path of the new partition (e.g. /dev/sda1, /dev/nvme0n1p3)
        //You can use newPartition->num for this, appending it to the base devicePath
        //we use + "p" only for /dev/loop0p1 as a test partition
        QString newPartPath;
        if(devicePath == "/dev/loop0"){
            newPartPath = devicePath + "p" + QString::number(newPartition->num); // <------- it's only for test
        } else {

            newPartPath = devicePath + QString::number(newPartition->num);
        }

        // 2. Execute the mkfs command
        if (fsType == "ext4" && fsTypePtr != NULL) {
            QProcess::execute("mkfs.ext4", QStringList() << newPartPath);
        } else if (fsType == "ntfs" && fsTypePtr != NULL) {
            QProcess::execute("mkfs.ntfs", QStringList() << newPartPath);
        }else if (fsType == "xfc" && fsTypePtr != NULL) {
            QProcess::execute("mkfs.xfc", QStringList() << newPartPath);
        }
        qDebug() << "Filesystem formatting command executed. Check system logs for final status.";
        // ... handle other fsTypes
        // --- Call the C library function instead of QProcess ---
        // if (format_ext4_library(newPartPath.toUtf8().constData())) {
        //     qDebug() << "Filesystem formatted successfully via library calls.";
        // } else {
        //     qDebug() << "Failed to format via library calls. Check console output for e2fsprogs errors.";
        // }


    //cleanup code for constraint, disk, dev) ...

    ped_constraint_destroy(constraint);
    ped_disk_destroy(disk);
    //ped_device_close(dev);
    close_my_device();
    return success;
    } else {
        qDebug() << "Failed to create partition.";
        ped_disk_delete_all(disk); // Cleanup if commit fails
    }


}

bool DiskManager::deletePartition(const QString& devicePath, int partitionNumber) {
    PedDevice *dev = ped_device_get(devicePath.toUtf8().constData());
    if (!dev) return false;

    PedDisk *disk = ped_disk_new(dev);
    if (!disk) {
        ped_device_close(dev);
        return false;
    }

    // Find the partition by number
    PedPartition *part = ped_disk_get_partition(disk, partitionNumber);
    if (!part || (part->type & PED_PARTITION_FREESPACE)) {
        qDebug() << "Partition not found or is free space.";
        ped_disk_destroy(disk);
        ped_device_close(dev);
        return false;
    }

    // Check if partition is busy
    if(ped_partition_is_busy(part)){
        qDebug() << "Partition is busy (mounted). Cannot delete.";
        ped_disk_destroy(disk);
        ped_device_close(dev);
        return false;
    }

    bool success = ped_disk_delete_partition(disk, part) && ped_disk_commit(disk);

    if (success) {
        qDebug() << "Partition deleted successfully. Committing changes.";
    } else {
        qDebug() << "Failed to delete partition.";
        ped_disk_destroy(disk); // Cleanup if commit fails
    }

    ped_disk_destroy(disk);
    //ped_device_close(dev);
    close_my_device();
    return success;
}



bool DiskManager::resizePartition(const QString& devicePath, int partitionNumber, long long newEndMBytes) {
    // libparted works with device names like "/dev/sda"
    PedDevice *dev = ped_device_get(devicePath.toUtf8().constData());
    if (!dev) {
        qDebug() << "Failed to get device" << devicePath;
        return false;
    }

    // Attempt to read the disk's partition table
    PedDisk *disk = ped_disk_new(dev);
    if (!disk) {
        qDebug() << "Failed to read partition table for" << devicePath;
        ped_device_close(dev);
        return false;
    }

    // Get the partition by its number (1-based index typically)
    PedPartition *part = ped_disk_get_partition(disk, partitionNumber);
    if (!part || (part->type & PED_PARTITION_FREESPACE)) {
        qDebug() << "Partition not found or is free space.";
        ped_disk_destroy(disk);
        ped_device_close(dev);
        return false;
    }
    //We only disable it for testing purposes.
    // if (ped_partition_is_busy(part)) {
    //     qDebug() << "Partition is busy (mounted). Cannot resize.";
    //     ped_disk_destroy(disk);
    //     ped_device_close(dev);
    //     return false;
    // }

    // Calculate the new end sector based on the new end bytes and device sector size
    PedSector newEndSector = newEndMBytes * 1024 * 1024 / dev->sector_size;
    qDebug() << "newEndSector:  " << newEndSector;
    // --- Correct approach for resizing ---

    // 1. Define a constraint. Using ped_constraint_any ensures maximum compatibility,
    //    but for optimal alignment (e.g., SSDs), you might want a specific constraint.
    PedConstraint *constraint = ped_constraint_any(dev);
    if (!constraint) {
        qDebug() << "Failed to create partition constraint.";
        ped_disk_destroy(disk);
        ped_device_close(dev);
        return false;
    }

    // 2. Use the correct function to resize the partition geometry.
    //    The start sector remains the same.
    bool success = ped_disk_set_partition_geom(
        disk,       // The disk object
        part,       // The partition to modify
        constraint, // Alignment constraints
        part->geom.start, // Old (and current) start sector
        newEndSector      // New end sector
        );
    // Note: The size is calculated as end_sector - start_sector + 1 if you count sectors,
    // but the PedDiskSetPartitionGeom uses the end sector directly.

    if (success) {
        qDebug() << "Partition geometry updated in memory.";
        // Commit changes to disk
        if (!ped_disk_commit(disk)) {
            qDebug() << "Failed to commit partition changes to disk.";
            success = false;
        } else {
            qDebug() << "Partition resized successfully (geometry).";
        }
    } else {
        qDebug() << "Failed to resize partition geometry (check constraints/validity).";
    }

    // Clean up
    ped_constraint_destroy(constraint);
    // ped_geometry_destroy(newGeom); // This was for the old, incorrect function
    ped_disk_destroy(disk);
    //ped_device_close(dev);
    close_my_device();
    // Remember to resize the filesystem inside the partition using external tools after this.

    return success;
}
//'my_device' is ManagedDevice struct instance
ManagedDevice my_device = {NULL, true};
// Function to SAFELY close a device (this prevents the assertion failure)
void DiskManager::close_my_device() {
    if (my_device.is_open && my_device.dev != NULL) {
        // This call is now safe because we've confirmed our state
        ped_device_close(my_device.dev);
        my_device.is_open = false;
        my_device.dev = NULL;
        printf("Device closed successfully.\n");
    } else {
        printf("Warning: Attempted to close a device that was not open.\n");
    }
}

#include <cstring>

// Helper function to convert a string name (e.g., "boot", "esp") to the PedPartitionFlag enum value
PedPartitionFlag DiskManager::flagNameToEnum(const std::string& flag_name) {
    // This is a basic mapping; a real application might use a more comprehensive lookup table
    if (flag_name == "boot") return PED_PARTITION_BOOT;
    if (flag_name == "esp") return PED_PARTITION_ESP;
    if (flag_name == "hidden") return PED_PARTITION_HIDDEN;
    if (flag_name == "lvm") return PED_PARTITION_LVM;
    if (flag_name == "raid") return PED_PARTITION_RAID;
    if (flag_name == "swap") return PED_PARTITION_SWAP;
    // And other flags as needed...
    return static_cast<PedPartitionFlag>(-1); // Return an invalid value if not found
}

/**
 * Sets a specific flag on a given partition using libparted.
 *
 * @param dev Pointer to the PedDevice (the physical disk).
 * @param partitionNumber The 1-based index of the partition.
 * @param flag_to_set The PedPartitionFlag enum value (e.g., PED_PARTITION_BOOT).
 * @param state Boolean to set the flag ON (true) or OFF (false).
 * @return True if the operation and commit were successful, false otherwise.
 */
bool DiskManager::setPartitionFlag(PedDevice *dev, int partitionNumber, PedPartitionFlag flag_to_set, bool state) {
    PedDisk *disk = ped_disk_new(dev);
    if (!disk) {
        std::cerr << "Failed to get disk object." << std::endl;
        return false;
    }

    // Find the specific partition by its number
    PedPartition *part = ped_disk_get_partition(disk, partitionNumber);
    if (!part || part->type & PED_PARTITION_METADATA) {
        std::cerr << "Invalid partition number or partition is metadata." << std::endl;
        ped_disk_destroy(disk);
        return false;
    }

    // Check if the flag is available for the current disk label type (MBR, GPT, etc.)
    if (!ped_partition_is_flag_available(part, flag_to_set)) {
        std::cerr << "Flag is not applicable to this disk label type." << std::endl;
        ped_disk_destroy(disk);
        return false;
    }

    // Set the flag state (on or off)
    // Note: PED_FLAG_ON/PED_FLAG_OFF are actually enum values that match true/false but are more explicit
    bool success = ped_partition_set_flag(part, flag_to_set, state);

    if (success) {
        // Commit the changes to the physical disk
        success = ped_disk_commit(disk);
        if (success) {
            std::cout << "Successfully committed flag changes for flag: " << ped_partition_flag_get_name(flag_to_set) << std::endl;
        } else {
            std::cerr << "Failed to commit disk changes. Changes reverted/lost in memory." << std::endl;
        }
    } else {
        std::cerr << "Failed to set the flag in memory (e.g., failed OS permission check)." << std::endl;
    }

    // Cleanup
    ped_disk_destroy(disk); // This handles closing the device too.
    return success;
}

PedDevice* DiskManager::getDeviceFromPath(const QString& path) {
    // libparted functions generally expect a const char* (C-style string)
    const char* devicePathCstr = path.toUtf8().constData();

    PedDevice* dev = ped_device_get(devicePathCstr);

    if (dev == nullptr) {
        qCritical() << "Failed to get device for path:" << path;
        // Check libparted errors here if necessary
        return nullptr;
    }

    return dev;
}

