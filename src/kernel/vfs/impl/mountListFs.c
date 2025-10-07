#include <fcntl.h>
#include "process.h"

#include <stddef.h>
#include <stddef.h>
#include <fcntl.h>

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
    listAdd((void*)&mount_list_file_system->data, mount);
}

void clean_path(char *path) {
    // cleans the path in place by shortening it as much as possible
    // TODO
}

enum MountlistGetStage {
    MOUNTLIST_GET_PRE = 0,
    MOUNTLIST_GET_FILE,
    MOUNTLIST_READ_SYMLINK,
    MOUNTLIST_READ_SYMLINK_POST,
    MOUNTLIST_GET_POST,
};

struct MountlistGetState {
    enum MountlistGetStage stage;
    char *current_path;
    void *fs_scratchpad;
    char *symlink_read;
};

char *mountlist_lookup(FileSystem *file_system, char *path, Mount **mount) {
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
            *mount = current_mount;
            current_match_length = match_length;
        }
    })
        ;
    if (!mount) {
        return NULL;
    }
    return combineStrings((*mount)->pathOffset,
                          path + strlen((*mount)->mountPoint));
}

uint32_t get_last_slash_position(char *string) {
    uint32_t result = strlen(string);
    if (!result) {
        return 0;
    }
    result--;
    while (result && string[result] != '/') {
        result--;
    }
    return result;
}

FileOperationStatus mountlist_get_file(FileSystem *file_system, char *path,
                                       struct ProcessThread *thread,
                                       File **result, void **scratchpad, uint32_t options) {
    if (!*scratchpad) {
        *scratchpad = malloc(sizeof(struct MountlistGetState));
        ((struct MountlistGetState *)*scratchpad)->stage = MOUNTLIST_GET_PRE;
    }
    struct MountlistGetState *state = *scratchpad;
    switch (state->stage) {
    case MOUNTLIST_GET_PRE:
        clean_path(path);
        state->current_path = malloc(strlen(path) + 2);
        memcpy(path, state->current_path, strlen(path));
        state->current_path[strlen(path) + 1] = 0; // no remainder for start
        state->fs_scratchpad = NULL;

        state->stage = MOUNTLIST_GET_FILE;
        // fallthrough
    case MOUNTLIST_GET_FILE:
        Mount *mount = NULL;
        char *real_path =
            mountlist_lookup(file_system, state->current_path, &mount);
        if (!real_path) {
            *result = NULL;
            state->stage = MOUNTLIST_GET_POST;
            goto end;
        }
        FileOperationStatus status = mount->file_system->type->getFile(
            mount->file_system, real_path, thread, result,
            &state->fs_scratchpad, options);
        if (status != FILE_OPERATION_DONE) {
            // fs should schedule us at some point
            return status;
        }
        state->fs_scratchpad = NULL;
        if (*result) {
            if ((*result)->type != FILE_TYPE_SYMLINK) {
                if (state->current_path[strlen(state->current_path) + 1]) {
                    // we have remainder but no match
                    *result = NULL;
                }
                state->stage = MOUNTLIST_GET_POST;
                goto end;
            }
            if ((options & O_SYMLINK) && !state->current_path[strlen(state->current_path) + 1]) {
                state->stage = MOUNTLIST_GET_POST;
                goto end;
            }
            state->symlink_read = NULL;
            state->stage = MOUNTLIST_READ_SYMLINK;
            break;
        }
        // file wasn't found, need to go back a step.
        uint32_t last_slash_position =
            get_last_slash_position(state->current_path);
        if (last_slash_position == 0) {
            *result = NULL;
            state->stage = MOUNTLIST_GET_POST;
            goto end;
        }
        // put together current remainder so /a/b\0c -> /a\0b/c
        if (state->current_path[strlen(state->current_path) + 1]) {
            state->current_path[strlen(state->current_path)] = '/';
        }
        state->current_path[last_slash_position] = 0;
        break;
    case MOUNTLIST_READ_SYMLINK:
        state->symlink_read = malloc(1024);
        memset(state->symlink_read, 0, 1024);
        uint32_t bytes_read;
        (*result)->file_system->type->read(*result, state->symlink_read, 1024,
                                           0, thread, NULL, &bytes_read);
        state->stage = MOUNTLIST_READ_SYMLINK_POST;
        return FILE_OPERATION_WILL_SCHEDULE;
    case MOUNTLIST_READ_SYMLINK_POST:
        char *new;
        char *remainder = state->current_path + strlen(state->current_path) + 1;
        if (state->symlink_read[0] == '/') {
            new = combineStrings(state->symlink_read, remainder);
        } else {
            last_slash_position =
                get_last_slash_position(state->current_path);
            if (last_slash_position == 0) {
                new = combineStrings("/", state->symlink_read);
            } else {
                state->current_path[last_slash_position] = 0;
                // new = state->current_path + '/' + state->symlink_read + '/' +
                // remainder
                uint32_t current_path_length = strlen(state->current_path);
                uint32_t symlink_length = strlen(state->symlink_read);
                uint32_t remainder_length = strlen(remainder);
                new =
                    malloc(current_path_length + 1 +
                           symlink_length + 1 + remainder_length);
                memcpy(state->current_path, new, current_path_length);
                new[current_path_length] = '/';
                memcpy(state->symlink_read, new + current_path_length + 1, symlink_length);
                new[current_path_length + symlink_length + 1] = '/';
                memcpy(remainder, new + current_path_length + 1 + symlink_length + 1, remainder_length);
            }
        }
        free(state->current_path);
        state->current_path = malloc(strlen(new) + 2);
        memcpy(new, state->current_path, strlen(new) + 1);
        state->current_path[strlen(new) + 1] = 0;
        clean_path(state->current_path);
        state->stage = MOUNTLIST_GET_FILE;
        break;
    case MOUNTLIST_GET_POST:
    end:
        free(state->current_path);
        free(state);
        return FILE_OPERATION_DONE;
    }
    listAdd(&threads_to_process, thread);
    return FILE_OPERATION_WILL_SCHEDULE;
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
