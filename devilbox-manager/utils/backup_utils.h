/*******************************************************************************
 * Simplified Backup Utilities Header
 *******************************************************************************/
#ifndef BACKUP_UTILS_H
#define BACKUP_UTILS_H

#include <windows.h>

#ifndef MAX_PATH_LEN
#define MAX_PATH_LEN 260
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Function declarations with simple signatures to avoid type conflicts
BOOL file_has_extension(const char *filename, const char *extensions);
BOOL create_directory_path(const char *path);
BOOL is_file_modified_recently(const char *filename, int days);
BOOL copy_file_with_path(const char *source_file, const char *source_root,
                         const char *target_root, char *status_message);
void backup_directory(const char *source_dir, const char *target_dir,
                     const char *extensions, int include_subdirs,
                     int days, HWND hStatus);
void execute_backup(const char *source_path, const char *target_path,
                   const char *extensions, int include_subdirs, int days);

#ifdef __cplusplus
}
#endif

#endif /* BACKUP_UTILS_H */