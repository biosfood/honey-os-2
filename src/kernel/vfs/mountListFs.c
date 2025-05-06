#include <stddef.h>

#include <mountlistfs.h>
#include <vfs.h>

// data is a list of mounted instances

FileSystemType mountlist_file_system_type;

void mount(FileSystem *mount_list_file_system, FileSystem *file_system,
           char *mountPoint, char *pathOffset) {
    if (mount_list_file_system->type != &mountlist_file_system_type) {
        return;
    }
    Mount *mount = malloc(sizeof(Mount));
    mount->file_system = file_system;
    mount->mountPoint = mountPoint;
    mount->pathOffset = pathOffset;
    listAdd(&file_system->mountedInstances, mount);
    listAdd(&mount_list_file_system->data, mount);
}

File *mountlist_get_file(FileSystem *file_system, char *path) {
    Mount *mount = NULL;
    uint32_t current_match_length = 0;
    foreach (file_system->data, Mount *, current_mount, {
        uint32_t match_length = 0;
        bool matches = true;
        while (current_mount->mountPoint[match_length]) {
            if (current_mount->mountPoint[match_length] != path[match_length]) {
                matches = false;
                break;
            }
            match_length++;
        }
        if (!matches) {
            continue;
        }
        if (match_length > current_match_length) {
            mount = current_mount;
            current_match_length = match_length;
        }
    })
        ;
    if (!mount) {
        return NULL;
    }
    char *realpath =
        combineStrings(mount->pathOffset, path + strlen(mount->mountPoint));
    char *nextStep = malloc(strlen(realpath) + 1);
    memset(nextStep, 0, strlen(realpath) + 1);
    uint32_t position = 0;
    uint32_t pathPosition = strlen(mount->mountPoint);
    for (int i = 0; i < strlen(mount->pathOffset); i++) {
        nextStep[position] = realpath[position];
        position++;
    }
    if (realpath[0] == '/' && !realpath[1]) {
        free(realpath);
        free(nextStep);
        return mount->file_system->type->getFile(mount->file_system, "/");
    }
    File *file = NULL;
    while (position != strlen(realpath)) {
        while (path[pathPosition] && path[pathPosition] != '/') {
            nextStep[position] = path[pathPosition];
            position++;
            pathPosition++;
        }
        file = mount->file_system->type->getFile(mount->file_system, nextStep);
        if (file->type == FILE_TYPE_LINK) {
            // TODO
            // if symbolic link, findFile(newPath, mountlist)
            while (1)
                ;
        }
        if (file->type == FILE_TYPE_DIRECTORY && position != strlen(realpath)) {
            nextStep[position] = path[pathPosition];
            position++;
            pathPosition++;
            continue;
        }
        break;
    }

    free(realpath);
    free(nextStep);
    return file;
}

FileSystemType mountlist_file_system_type = {
    .getFile = mountlist_get_file,
};

FileSystem *create_mount_list_file_system() {
    FileSystem *fs = malloc(sizeof(FileSystem));
    fs->data = NULL;
    fs->name = "Mountlist";
    fs->mountedInstances = NULL;
    fs->type = &mountlist_file_system_type;
    return fs;
}
