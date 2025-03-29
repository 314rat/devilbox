/*******************************************************************************
 * Simplified Backup Utilities Implementation
 *******************************************************************************/

 #include "backup_utils.h"
 #include <stdio.h>
 #include <string.h>
 #include <commctrl.h>
 
 // Forward declaration for internal recursive function
 static void backup_directory_internal(const char *source_dir, const char *target_dir,
                                      const char *extensions, int include_subdirs,
                                      int days, HWND hStatus, const char *source_root,
                                      int *file_count);
 
 /**
  * Check if file has matching extension
  */
 BOOL file_has_extension(const char *filename, const char *extensions)
 {
     char ext_list[MAX_PATH_LEN];
     char *token;
     char file_ext[MAX_PATH_LEN] = "";
 
     // Get file extension
     const char *ext = strrchr(filename, '.');
     if (ext) {
         strcpy(file_ext, ext);
     }
     else if (strncmp(filename, ".htaccess", 9) == 0) {
         // Special case for .htaccess
         strcpy(file_ext, ".htaccess");
     }
     else {
         return FALSE;
     }
 
     // Copy extension list for safe tokenization
     strcpy(ext_list, extensions);
 
     // Check all extensions
     token = strtok(ext_list, ",;");
     while (token) {
         // Trim whitespace
         while (*token == ' ')
             token++;
 
         // Check if extension matches
         if (*token == '.') {
             if (_stricmp(file_ext, token) == 0)
                 return TRUE;
         }
         else {
             char temp[MAX_PATH_LEN] = ".";
             strcat(temp, token);
             if (_stricmp(file_ext, temp) == 0)
                 return TRUE;
         }
 
         // Next token
         token = strtok(NULL, ",;");
     }
 
     // Check special case for .htaccess
     if (strcmp(filename, ".htaccess") == 0 && strstr(extensions, "htaccess") != NULL)
         return TRUE;
 
     return FALSE;
 }
 
 /**
  * Create directory path recursively
  */
 BOOL create_directory_path(const char *path)
 {
     char temp[MAX_PATH_LEN];
     char *p;
 
     strcpy(temp, path);
 
     // Replace forward slashes with backslashes
     for (p = temp; *p; p++) {
         if (*p == '/') *p = '\\';
     }
 
     // Remove trailing slash if exists
     size_t len = strlen(temp);
     if (len > 0 && temp[len - 1] == '\\') {
         temp[len - 1] = 0;
     }
 
     // Create all subdirectories
     for (p = temp; *p; p++) {
         if (*p == '\\') {
             *p = 0;
 
             // Skip empty parts
             if (strlen(temp) == 0) {
                 *p = '\\';
                 continue;
             }
 
             // If directory doesn't exist, create it
             if (GetFileAttributes(temp) == INVALID_FILE_ATTRIBUTES) {
                 if (!CreateDirectory(temp, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
                     *p = '\\';
                     return FALSE;
                 }
             }
 
             *p = '\\';
         }
     }
 
     // Create final directory if it doesn't exist
     if (GetFileAttributes(temp) == INVALID_FILE_ATTRIBUTES) {
         if (!CreateDirectory(temp, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
             return FALSE;
         }
     }
 
     return TRUE;
 }
 
 /**
  * Check if file was modified within the specified number of days
  */
 BOOL is_file_modified_recently(const char *filename, int days)
 {
     WIN32_FILE_ATTRIBUTE_DATA attr;
     FILETIME ft;
     SYSTEMTIME st;
     ULARGE_INTEGER now, file_time;
 
     // Get current system time
     GetSystemTime(&st);
     SystemTimeToFileTime(&st, &ft);
 
     // Convert to ULARGE_INTEGER for comparison
     now.LowPart = ft.dwLowDateTime;
     now.HighPart = ft.dwHighDateTime;
 
     // Subtract specified days (in 100-nanosecond intervals)
     now.QuadPart -= (ULONGLONG)days * 24 * 60 * 60 * 10000000;
 
     // Get file's last modification time
     if (!GetFileAttributesEx(filename, GetFileExInfoStandard, &attr)) {
         return FALSE;
     }
 
     // Convert file time for comparison
     file_time.LowPart = attr.ftLastWriteTime.dwLowDateTime;
     file_time.HighPart = attr.ftLastWriteTime.dwHighDateTime;
 
     // File was modified within the specified days?
     return (file_time.QuadPart >= now.QuadPart);
 }
 
 /**
  * Copy file while preserving the directory structure
  */
 BOOL copy_file_with_path(const char *source_file, const char *source_root,
                         const char *target_root, char *status_message)
 {
     char relative_path[MAX_PATH_LEN] = {0};
     char target_file[MAX_PATH_LEN] = {0};
     char target_dir[MAX_PATH_LEN] = {0};
     char *p;
 
     // Get the relative path by removing source_root from source_file
     if (strncmp(source_file, source_root, strlen(source_root)) == 0) {
         // Calculate the relative path starting after source_root
         strcpy(relative_path, source_file + strlen(source_root));
     } else {
         // Fallback (should not happen with correct implementation)
         strcpy(relative_path, source_file);
     }
 
     // Normalize slashes
     for (p = relative_path; *p; p++) {
         if (*p == '/') *p = '\\';
     }
 
     // Remove leading slash if present
     if (relative_path[0] == '\\') {
         memmove(relative_path, relative_path + 1, strlen(relative_path) + 1);
     }
 
     // Create full target path including directories
     _snprintf(target_file, MAX_PATH_LEN, "%s\\%s", target_root, relative_path);
 
     // Extract target directory path
     strcpy(target_dir, target_file);
     p = strrchr(target_dir, '\\');
     if (p) {
         *p = 0; // Truncate to get directory path
 
         // Create target directory structure
         if (!create_directory_path(target_dir)) {
             if (status_message) {
                 sprintf(status_message, "Failed to create directory: %s", target_dir);
             }
             return FALSE;
         }
     }
 
     // Copy the file
     if (!CopyFile(source_file, target_file, FALSE)) {
         if (status_message) {
             sprintf(status_message, "Failed to copy file: %s (Error: %d)",
                     source_file, GetLastError());
         }
         return FALSE;
     }
 
     if (status_message) {
         sprintf(status_message, "Copied: %s", relative_path);
     }
 
     return TRUE;
 }
 
 /**
  * Backup directory with proper directory structure preservation
  */
 void backup_directory(const char *source_dir, const char *target_dir,
                      const char *extensions, int include_subdirs,
                      int days, HWND hStatus)
 {
     int file_count = 0;
     
     // Start the recursive backup process with source_dir as the source_root
     backup_directory_internal(source_dir, target_dir, extensions, include_subdirs,
                              days, hStatus, source_dir, &file_count);
     
     // Show final status message
     if (hStatus) {
         char status_message[MAX_PATH_LEN * 2];
         sprintf(status_message, "Backup complete. Copied %d files.", file_count);
         SetWindowText(hStatus, status_message);
     }
 }
 
 /**
  * Internal recursive function to backup directories and files while preserving structure
  */
 static void backup_directory_internal(const char *source_dir, const char *target_dir,
                                      const char *extensions, int include_subdirs,
                                      int days, HWND hStatus, const char *source_root,
                                      int *file_count)
 {
     char search_path[MAX_PATH_LEN];
     char full_path[MAX_PATH_LEN];
     char status_message[MAX_PATH_LEN * 2];
     WIN32_FIND_DATA fd;
     HANDLE hFind;
 
     // Format search path for finding all files in current directory
     _snprintf(search_path, MAX_PATH_LEN, "%s\\*", source_dir);
 
     // Start search
     hFind = FindFirstFile(search_path, &fd);
     if (hFind == INVALID_HANDLE_VALUE)
         return;
 
     do {
         // Skip . and ..
         if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
             continue;
 
         // Create full path to the current file/directory
         _snprintf(full_path, MAX_PATH_LEN, "%s\\%s", source_dir, fd.cFileName);
 
         if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
             // If this is a directory and we need to include subdirectories
             if (include_subdirs) {
                 // Recursively process subdirectory, maintaining original source_root
                 backup_directory_internal(full_path, target_dir, extensions, include_subdirs,
                                          days, hStatus, source_root, file_count);
             }
         } else {
             // This is a file - check if it matches our criteria
             if (file_has_extension(fd.cFileName, extensions) &&
                 is_file_modified_recently(full_path, days)) {
 
                 // Copy file with preserved directory structure
                 if (copy_file_with_path(full_path, source_root, target_dir, status_message)) {
                     (*file_count)++;
                     
                     if (hStatus) {
                         SetWindowText(hStatus, status_message);
                     }
                 } else if (hStatus) {
                     SetWindowText(hStatus, status_message);
                 }
             }
         }
     } while (FindNextFile(hFind, &fd));
 
     FindClose(hFind);
 }
 
 /**
  * Execute backup with progress dialog
  */
 void execute_backup(const char *source_path, const char *target_path,
                    const char *extensions, int include_subdirs, int days)
 {
     HWND hDlg;
     HWND hStatus;
     HWND hProgress;
     HWND hButton;
 
     // Create simple progress window
     hDlg = CreateWindowEx(
         WS_EX_DLGMODALFRAME,
         "STATIC",
         "Backup in Progress",
         WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
         CW_USEDEFAULT, CW_USEDEFAULT, 500, 150,
         NULL, NULL, GetModuleHandle(NULL), NULL);
 
     if (!hDlg) {
         MessageBox(NULL, "Failed to create progress window.", "Error", MB_ICONERROR);
         return;
     }
 
     // Add controls
     hStatus = CreateWindowEx(
         0, "STATIC", "Starting backup...",
         WS_CHILD | WS_VISIBLE | SS_LEFT,
         10, 20, 470, 20,
         hDlg, NULL, GetModuleHandle(NULL), NULL);
 
     hProgress = CreateWindowEx(
         0, PROGRESS_CLASS, NULL,
         WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
         10, 50, 470, 20,
         hDlg, NULL, GetModuleHandle(NULL), NULL);
 
     hButton = CreateWindowEx(
         0, "BUTTON", "Cancel",
         WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
         200, 80, 100, 30,
         hDlg, (HMENU)IDCANCEL, GetModuleHandle(NULL), NULL);
 
     // Initialize progress bar
     SendMessage(hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
     SendMessage(hProgress, PBM_SETSTEP, 1, 0);
 
     // Update window
     ShowWindow(hDlg, SW_SHOW);
     UpdateWindow(hDlg);
 
     // Start backup operation - use consistent int parameters
     backup_directory(source_path, target_path, extensions, include_subdirs, days, hStatus);
 
     // Show completion message
     MessageBox(hDlg, "Backup completed successfully!", "Backup Complete", MB_OK | MB_ICONINFORMATION);
 
     // Close progress window
     DestroyWindow(hDlg);
 }