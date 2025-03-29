/*******************************************************************************
 * Logs Viewer Module Header
 * Provides functionality for viewing and filtering PHP error logs
 *******************************************************************************/
#ifndef LOGS_VIEWER_H
#define LOGS_VIEWER_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

// Maximum path length constant (if not already defined)
#ifndef MAX_PATH_LEN
#define MAX_PATH_LEN 260
#endif

// Maximum buffer size for logs
#ifndef MAX_BUFFER
#define MAX_BUFFER 1048576 // 1MB buffer for logs
#endif

// Log dialog control IDs
enum
{
    ID_LOGS_EDIT = 100,
    ID_REFRESH_BTN,
    ID_CLOSE_BTN,
    ID_CLEAR_BTN,
    ID_DATE_FILTER_START,
    ID_DATE_FILTER_END,
    ID_TIME_FILTER_START,
    ID_TIME_FILTER_END,
    ID_APPLY_FILTER_BTN,
    ID_RESET_FILTER_BTN
};

// Main function to display logs viewer
/**
 * Display PHP error logs viewer dialog
 * @param app_path Base path of the Devilbox installation
 * @param php_version Current PHP version string (e.g. "php-8.1")
 */
void show_php_logs(const char *app_path, const char *php_version);

/**
 * Helper function to create a Windows control
 * @param parent Parent window handle
 * @param class_name Class name of the control
 * @param text Text to display in the control
 * @param style Window style
 * @param x X coordinate
 * @param y Y coordinate
 * @param width Width of the control
 * @param height Height of the control
 * @param id Control identifier
 * @param ex_style Extended window style
 * @return Handle to the created control
 */
HWND create_control(HWND parent, const char *class_name, const char *text,
                    DWORD style, int x, int y, int width, int height,
                    HMENU id, DWORD ex_style);

#ifdef __cplusplus
}
#endif

#endif /* LOGS_VIEWER_H */