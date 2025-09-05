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
struct Container;

typedef struct FiFoData {
    ListElement *queue;
    // if thread != null, we are blocked on read and should write to write_data,
    // etc.
    struct ProcessThread *thread;
    void *write_data;
    uint32_t len;
    uint32_t *bytes_read;
} FiFoData;

typedef struct FileDescriptor {
    uint32_t id;
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

typedef enum FileOperationStatus {
    FILE_OPERATION_DONE = 0,
    FILE_OPERATION_WILL_SCHEDULE = 1,
} FileOperationStatus;

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
    FileOperationStatus (*getFile)(struct FileSystem *file_system, char *path,
                                   struct ProcessThread *thread, File **result,
                                   void **scratchpad);
    File *(*create)(File *file, char *path, enum FileType type);
    void (*write)(File *file, void *data, uint32_t size, uint32_t offset,
                  struct ProcessThread *thread,
                  struct FileDescriptor *descriptor, uint32_t *bytes_written);
    void (*read)(File *file, void *data, uint32_t size, uint32_t offset,
                 struct ProcessThread *thread,
                 struct FileDescriptor *descriptor, uint32_t *bytes_read);
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
extern FileSystem *create_process_fs(struct Container *container);

extern void initialize_proc_files(struct Process *process, char *exe);

void processInitrd(void *fileData, uint32_t tarFileSize,
                   FileSystem *file_system);
extern FileDescriptor *allocateFileDescriptor(struct Process *process);
extern void fill_dirent(FillDirData *buf, char *name, int file_type);

void fifo_write(File *file, void *write_data, uint32_t len,
                uint32_t *bytes_written, struct ProcessThread *thread);
extern void fifo_read(void *write_data, uint32_t len, FiFoData *fifo,
                      struct ProcessThread *thread, uint32_t *bytes_read);

extern bool read_integer_from_filename(char **filename, uint32_t *data);

enum ProcFiles {
    PROC_FILE_ROOT,
    PROC_FILE_SIGNAL,
    PROC_FILE_PIPEIN,
    PROC_FILE_EXECUTABLE,
    PROC_FILE_WORKDIR,

    // workaround to statically get the number of files
    PROC_FILE_MAX
};

typedef struct {
    File;
    struct Process *process;
    enum ProcFiles file_type;
    uint32_t length;
} ProcessFile;

#endif // VFS_H
