//
// Created by lukas on 3/30/25.
//

#ifndef VFS_H
#define VFS_H
#include <util.h>
struct FileSystem;
struct File;
struct FileDescriptor;
struct Process;

// operations that can be done on a file
typedef struct FileDescriptorOperations {
    // members of this struct are all allowed to be NULL, indicating that the
    // specified operation is not supported.
    int (*open)(struct FileDescriptor *file);
    int (*read)(struct FileDescriptor *file, uint32_t count);
    int (*write)(struct FileDescriptor *file, uint32_t count);
    struct File *(*lookup)(struct FileSystem *file_system, struct File *file,
                           char *name);
} FileDescriptorOperations;

typedef struct FileDescriptor {
    uint32_t id;
    FileDescriptorOperations *operations;
    struct File *file;
    struct Process *process;
    uint32_t offset;
    ListElement *blockedWritingThreads;
    ListElement *blockedReadingThreads;
} FileDescriptor;

enum FileType {
    FILE_TYPE_DIRECTORY = 0,
    FILE_TYPE_SYMLINK = 1,
    FILE_TYPE_FILE = 2,
    FILE_TYPE_FIFO = 3,
    FILE_TYPE_SOCKET = 4,
    FILE_TYPE_LINK = 5,
};

#define S_IFSOCK FILE_TYPE_SOCKET
#define S_IFLNK FILE_TYPE_LINK
#define S_IFREG FILE_TYPE_FILE

// A file describes all the metadata for a file in a file system
// File structs pointing to the same file should be considered equal
// linux equivalent is an inode
typedef struct File {
    struct FileSystem *file_system;
    enum FileType type;
    uint32_t size;
    bool livesInMemory; // used to determine if the file could benefit from
                        // being cached in memory or if that would just waste
                        // resources.
    // data for the filesystem to track the position of the file and other
    // information. This could refer to a block address, URL or any other data.
    // for pipe files, this is the queue of datagrams to process.
    void *data;
    void *data_;
} File;

typedef struct {
    struct FileSystem *file_system;
    enum FileType type;
    uint32_t size;
    bool livesInMemory; // used to determine if the file could benefit from
                        // being cached in memory or if that would just waste
                        // resources.
    // data for the filesystem to track the position of the file and other
    // information. This could refer to a block address, URL or any other data.
    // for pipe files, this is the queue of datagrams to process.
    struct ProcessThread *blockedReadingThread;
    ListElement *queue;
} FiFoFile;

typedef struct {
    File *(*getFile)(struct FileSystem *file_system, char *path);
    File *(*create)(File *file, char *path, enum FileType type);
    uint32_t (*write)(File *file, void *data, uint32_t size, uint32_t offset);
    uint32_t (*read)(File *file, void *data, uint32_t size, uint32_t offset);
} FileSystemType;

typedef struct FileSystem {
    char *name;
    FileSystemType *type;
    ListElement
        *mountedInstances; // we can clean up if this is empty? Probably yes,
                           // because no more references can be created.
    void *data;
} FileSystem;

typedef struct {
    FileSystem *file_system;
    char *pathOffset;
    char *mountPoint;
} Mount;

typedef struct {
    uint32_t length;
    void *data;
} PipeData;

extern FileSystem *createRamfs();
extern FileSystem *createKernelFs();
void processInitrd(void *fileData, uint32_t tarFileSize,
                   FileSystem *file_system);
extern FileDescriptor *allocateFileDescriptor(struct Process *process);

#endif // VFS_H
