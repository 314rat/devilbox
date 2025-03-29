/*******************************************************************************
 * Settings Module Header
 * Provides functionality for configuring application settings
 *******************************************************************************/
#ifndef SETTINGS_H
#define SETTINGS_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

// Maximum path length constant (if not already defined)
#ifndef MAX_PATH_LEN
#define MAX_PATH_LEN 260
#endif

// Settings dialog control IDs
enum
{
    ID_AUTOSTART_CHECK = 200,
    ID_STARTUP_SCRIPT_PATH,
    ID_STARTUP_SCRIPT_BROWSE,
    ID_SAVE_SETTINGS_BTN,
    ID_CANCEL_SETTINGS_BTN,
    ID_LOADING_LABEL
};

// Settings structure
typedef struct
{
    BOOL autostart;
    char startup_script_path[MAX_PATH_LEN];
} AppSettings;

/**
 * Show settings dialog
 * @param hwnd Parent window handle
 * @return TRUE if settings were changed, FALSE otherwise
 */
BOOL show_settings_dialog(HWND hwnd);

/**
 * Load settings from registry
 * @param settings Pointer to settings structure to populate
 */
void load_settings(AppSettings *settings);

/**
 * Save settings to registry
 * @param settings Pointer to settings structure to save
 */
void save_settings(const AppSettings *settings);

/**
 * Add or remove application from Windows startup
 * @param add TRUE to add to startup, FALSE to remove
 * @return TRUE if operation was successful
 */
BOOL set_autostart(BOOL add);

/**
 * Check if application is set to run at startup
 * @return TRUE if application is in startup, FALSE otherwise
 */
BOOL is_autostart_enabled(void);

/**
 * Helper function to create a dialog control
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
HWND create_settings_control(HWND parent, const char *class_name, const char *text,
                    DWORD style, int x, int y, int width, int height,
                    HMENU id, DWORD ex_style);

/**
 * Выполняет скрипт, указанный в настройках, при запуске приложения
 * Эту функцию нужно вызвать после инициализации приложения
 */
void execute_startup_script(void);

#ifdef __cplusplus
}
#endif

#endif /* SETTINGS_H */