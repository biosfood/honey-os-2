//
// Created by lukas on 3/30/25.
//

#ifndef VFS_H
#define VFS_H
#include <sys/stat.h>
#include <util.h>

struct ProcessThread;
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

typedef struct FiFoData {
    struct ProcessThread *blocked_reading_thread;
    ListElement *queue;
} FiFoData;

typedef struct FileDescriptor {
    uint32_t id;
    FileDescriptorOperations *operations;
    struct File *file;
    struct Process *process;
    uint32_t offset;
    FiFoData fifo_data;
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
    // for the filesystem to do with whatever it wants
    // most should probably allocate a descriptor for the file but if it's just
    // a single number, feel free to store it here as well.
    void *data;
    ListElement *file_descriptors;
} File;

typedef struct {
    File *(*getFile)(struct FileSystem *file_system, char *path);
    File *(*create)(File *file, char *path, enum FileType type);
    uint32_t (*write)(File *file, void *data, uint32_t size, uint32_t offset);
    uint32_t (*read)(File *file, void *data, uint32_t size, uint32_t offset);
    uint32_t (*getattr)(File *file, struct stat *stbuf);
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
    void *data;
    uint32_t size, offset, current_offset, bytes_written;
} FillDirData;

extern FileSystem *createRamfs();
extern FileSystem *createKernelFs();
void processInitrd(void *fileData, uint32_t tarFileSize,
                   FileSystem *file_system);
extern FileDescriptor *allocateFileDescriptor(struct Process *process);
extern void fill_dirent(FillDirData *buf, char *name, int file_type);

extern void fifo_write(File *file, struct Process *process, void *process_address,
                uint32_t size);
extern void fifo_read(struct ProcessThread *thread, FiFoData *fifo);

extern bool read_integer_from_filename(char **filename, uint32_t *data);

#endif // VFS_H
