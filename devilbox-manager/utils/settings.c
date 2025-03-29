/*******************************************************************************
 * Settings Module Implementation
 * Provides functionality for configuring application settings
 *******************************************************************************/

 #include "settings.h"
 #include <stdio.h>
 #include <string.h>
 #include <commctrl.h>
 #include <ctype.h> // Для функции tolower
 
 #define APP_NAME "DevilboxManager"
 #define REG_KEY "Software\\DevilboxManager"
 #define REG_RUN_KEY "Software\\Microsoft\\Windows\\CurrentVersion\\Run"
 
 // Определение пользовательского сообщения для асинхронной инициализации
 #define WM_INIT_DIALOG (WM_USER + 100)
 
 // Global settings structure
 static AppSettings app_settings = {0};
 
 // Settings dialog state
 typedef struct
 {
     HWND hDlg;
     HWND hAutostart;
     HWND hStartupScriptPath;
     HWND hStartupScriptPathLabel;
     BOOL modified;
 } SettingsDialogState;
 
 static SettingsDialogState settings_dialog = {0};
 
 // Forward declarations
 static LRESULT CALLBACK SettingsDialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
 static void init_dialog_controls(HWND hwnd, HWND hwndParent);
 
 /**
  * Create a control for the settings dialog
  */
 HWND create_settings_control(HWND parent, const char *class_name, const char *text,
                      DWORD style, int x, int y, int width, int height,
                      HMENU id, DWORD ex_style)
 {
     return CreateWindowEx(ex_style, class_name, text, style,
                           x, y, width, height, parent, id,
                           GetModuleHandle(NULL), NULL);
 }
 
 /**
  * Load settings from registry
  */
 void load_settings(AppSettings *settings)
 {
     if (!settings)
         return;
 
     // Set defaults
     settings->autostart = FALSE;
     settings->startup_script_path[0] = '\0';
 
     // Load from registry
     HKEY key;
     DWORD type, size;
 
     // Check if app is in autostart
     settings->autostart = is_autostart_enabled();
 
     // Load other settings
     if (RegOpenKeyEx(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &key) == ERROR_SUCCESS)
     {
         size = sizeof(settings->startup_script_path);
         RegQueryValueEx(key, "StartupScriptPath", NULL, &type, (BYTE *)settings->startup_script_path, &size);
         
         RegCloseKey(key);
     }
 }
 
 /**
  * Save settings to registry
  */
 void save_settings(const AppSettings *settings)
 {
     if (!settings)
         return;
 
     // Update autostart status based on settings
     set_autostart(settings->autostart);
 
     // Save other settings to registry
     HKEY key;
     if (RegCreateKeyEx(HKEY_CURRENT_USER, REG_KEY, 0, NULL, 0, KEY_WRITE, NULL, &key, NULL) == ERROR_SUCCESS)
     {
         if (settings->startup_script_path[0] != '\0')
         {
             RegSetValueEx(key, "StartupScriptPath", 0, REG_SZ, (BYTE *)settings->startup_script_path, 
                          strlen(settings->startup_script_path) + 1);
         }
         
         RegCloseKey(key);
     }
 }
 
 /**
  * Check if application is set to run at startup
  */
 BOOL is_autostart_enabled(void)
 {
     HKEY key;
     BOOL result = FALSE;
     char app_path[MAX_PATH_LEN] = {0};
     DWORD size = sizeof(app_path);
     DWORD type;
 
     // Check if key exists in Run registry
     if (RegOpenKeyEx(HKEY_CURRENT_USER, REG_RUN_KEY, 0, KEY_READ, &key) == ERROR_SUCCESS)
     {
         if (RegQueryValueEx(key, APP_NAME, NULL, &type, (BYTE *)app_path, &size) == ERROR_SUCCESS)
         {
             result = TRUE;
         }
         RegCloseKey(key);
     }
 
     return result;
 }
 
 /**
  * Add or remove application from Windows startup
  */
 BOOL set_autostart(BOOL add)
 {
     HKEY key;
     LONG result;
     char app_path[MAX_PATH_LEN] = {0};
 
     // Get the path to the current executable
     GetModuleFileName(NULL, app_path, MAX_PATH_LEN);
 
     // Open the Run registry key
     if (RegOpenKeyEx(HKEY_CURRENT_USER, REG_RUN_KEY, 0, KEY_WRITE, &key) != ERROR_SUCCESS)
     {
         return FALSE;
     }
 
     if (add)
     {
         // Add the application to startup
         result = RegSetValueEx(key, APP_NAME, 0, REG_SZ, (BYTE *)app_path, strlen(app_path) + 1);
     }
     else
     {
         // Remove the application from startup
         result = RegDeleteValue(key, APP_NAME);
     }
 
     RegCloseKey(key);
     return (result == ERROR_SUCCESS);
 }
 
 /**
  * Show settings dialog
  */
 BOOL show_settings_dialog(HWND hwndParent)
 {
     // Check if dialog is already open
     if (settings_dialog.hDlg && IsWindow(settings_dialog.hDlg))
     {
         SetForegroundWindow(settings_dialog.hDlg);
         return FALSE;
     }
 
     // Reset modified flag
     settings_dialog.modified = FALSE;
 
     // Register dialog class
     WNDCLASSEX wcDialog;
     memset(&wcDialog, 0, sizeof(WNDCLASSEX));
     wcDialog.cbSize = sizeof(WNDCLASSEX);
     wcDialog.lpfnWndProc = SettingsDialogProc;
     wcDialog.hInstance = GetModuleHandle(NULL);
     wcDialog.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
     wcDialog.lpszClassName = "DevilboxSettingsDialog";
     RegisterClassEx(&wcDialog);
 
     // Create dialog window
     settings_dialog.hDlg = CreateWindowEx(
         WS_EX_DLGMODALFRAME,
         "DevilboxSettingsDialog",
         "DevilboxManager Settings",
         WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
         CW_USEDEFAULT, CW_USEDEFAULT, 500, 250,
         hwndParent, NULL, GetModuleHandle(NULL), NULL);
 
     if (!settings_dialog.hDlg)
     {
         MessageBox(hwndParent, "Failed to create settings dialog window.", "Error", MB_ICONERROR);
         return FALSE;
     }
 
     // Добавляем индикатор загрузки
     HWND hLoading = create_settings_control(
         settings_dialog.hDlg, "STATIC", "Loading settings...",
         WS_CHILD | WS_VISIBLE,
         20, 110, 450, 20, (HMENU)ID_LOADING_LABEL, 0);
 
     // Show window immediately
     ShowWindow(settings_dialog.hDlg, SW_SHOW);
     UpdateWindow(settings_dialog.hDlg);
 
     // Start asynchronous initialization of controls
     PostMessage(settings_dialog.hDlg, WM_INIT_DIALOG, (WPARAM)hwndParent, 0);
 
     return TRUE;
 }
 
 /**
  * Asynchronously initialize dialog controls
  */
 static void init_dialog_controls(HWND hwnd, HWND hwndParent)
 {
     // Load current settings (can be slow due to registry operations)
     load_settings(&app_settings);
 
     // Remove the loading label
     DestroyWindow(GetDlgItem(hwnd, ID_LOADING_LABEL));
 
     // Create all controls
     // Autostart checkbox
     settings_dialog.hAutostart = create_settings_control(
         hwnd, "BUTTON", "Start with Windows",
         WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
         20, 20, 200, 24, (HMENU)ID_AUTOSTART_CHECK, 0);
 
     // Startup script section
     settings_dialog.hStartupScriptPathLabel = create_settings_control(
         hwnd, "STATIC", "Startup script (cmd, bat, ps1 file to execute after loading):",
         WS_CHILD | WS_VISIBLE,
         20, 60, 450, 20, NULL, 0);
 
     settings_dialog.hStartupScriptPath = create_settings_control(
         hwnd, "EDIT", app_settings.startup_script_path,
         WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
         20, 85, 360, 24, (HMENU)ID_STARTUP_SCRIPT_PATH, WS_EX_CLIENTEDGE);
 
     create_settings_control(
         hwnd, "BUTTON", "...",
         WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
         390, 85, 30, 24, (HMENU)ID_STARTUP_SCRIPT_BROWSE, 0);
 
     create_settings_control(
         hwnd, "STATIC", "Note: This script will be executed after DevilboxManager starts.",
         WS_CHILD | WS_VISIBLE,
         20, 115, 450, 20, NULL, 0);
 
     // Buttons
     create_settings_control(
         hwnd, "BUTTON", "Save",
         WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
         290, 180, 80, 30, (HMENU)ID_SAVE_SETTINGS_BTN, 0);
 
     create_settings_control(
         hwnd, "BUTTON", "Cancel",
         WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
         380, 180, 80, 30, (HMENU)ID_CANCEL_SETTINGS_BTN, 0);
 
     // Set initial checkbox state
     SendMessage(settings_dialog.hAutostart, BM_SETCHECK, app_settings.autostart ? BST_CHECKED : BST_UNCHECKED, 0);
 
     // Set icon
     HICON hIcon = LoadIcon(NULL, IDI_APPLICATION);
     SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
     SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
 
     // Make dialog modal after controls are initialized
     if (hwndParent && IsWindow(hwndParent))
     {
         EnableWindow(hwndParent, FALSE);
     }
 
     // Final update
     UpdateWindow(hwnd);
 }
 
 /**
  * Dialog procedure for settings window
  */
 static LRESULT CALLBACK SettingsDialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
 {
     HWND parent;
     
     switch (msg)
     {
     case WM_INIT_DIALOG:
         // Асинхронная инициализация контролов
         init_dialog_controls(hwnd, (HWND)wp);
         break;
     
     case WM_COMMAND:
         switch (LOWORD(wp))
         {
         case ID_STARTUP_SCRIPT_BROWSE:
         {
             // Open file browser for script file
             OPENFILENAME ofn;
             char szFile[MAX_PATH_LEN] = {0};
             
             ZeroMemory(&ofn, sizeof(ofn));
             ofn.lStructSize = sizeof(ofn);
             ofn.hwndOwner = hwnd;
             ofn.lpstrFile = szFile;
             ofn.nMaxFile = sizeof(szFile);
             ofn.lpstrFilter = "Script Files (*.cmd;*.bat;*.ps1)\0*.cmd;*.bat;*.ps1\0All Files (*.*)\0*.*\0";
             ofn.nFilterIndex = 1;
             ofn.lpstrFileTitle = NULL;
             ofn.nMaxFileTitle = 0;
             ofn.lpstrInitialDir = NULL;
             ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
             
             if (GetOpenFileName(&ofn))
             {
                 SetWindowText(settings_dialog.hStartupScriptPath, szFile);
                 settings_dialog.modified = TRUE;
             }
             break;
         }
 
         case ID_AUTOSTART_CHECK:
             // Mark settings as modified when checkbox changes
             settings_dialog.modified = TRUE;
             break;
 
         case ID_STARTUP_SCRIPT_PATH:
             if (HIWORD(wp) == EN_CHANGE)
             {
                 settings_dialog.modified = TRUE;
             }
             break;
 
         case ID_SAVE_SETTINGS_BTN:
         {
             // Get values from controls
             app_settings.autostart = SendMessage(settings_dialog.hAutostart, BM_GETCHECK, 0, 0) == BST_CHECKED;
             
             GetWindowText(settings_dialog.hStartupScriptPath, app_settings.startup_script_path, MAX_PATH_LEN);
             
             // Save settings
             save_settings(&app_settings);
             
             // Close dialog
             parent = GetWindow(hwnd, GW_OWNER);
             if (parent && IsWindow(parent))
                 EnableWindow(parent, TRUE);
             DestroyWindow(hwnd);
             break;
         }
 
         case ID_CANCEL_SETTINGS_BTN:
             // Close dialog without saving
             parent = GetWindow(hwnd, GW_OWNER);
             if (parent && IsWindow(parent))
                 EnableWindow(parent, TRUE);
             DestroyWindow(hwnd);
             break;
         }
         break;
 
     case WM_CLOSE:
         if (settings_dialog.modified)
         {
             // Ask for confirmation if settings were modified
             if (MessageBox(hwnd, "You have unsaved changes. Do you want to discard them?", 
                           "Confirm", MB_YESNO | MB_ICONQUESTION) == IDNO)
             {
                 return 0;
             }
         }
         
         parent = GetWindow(hwnd, GW_OWNER);
         if (parent && IsWindow(parent))
             EnableWindow(parent, TRUE);
         DestroyWindow(hwnd);
         break;
 
     case WM_DESTROY:
         settings_dialog.hDlg = NULL;
         break;
 
     default:
         return DefWindowProc(hwnd, msg, wp, lp);
     }
     return 0;
 }
 
 /**
  * Выполняет скрипт, указанный в настройках, при запуске приложения
  * Эту функцию нужно вызвать после инициализации приложения
  */
 void execute_startup_script(void)
 {
     AppSettings settings;
     
     // Загрузить настройки
     load_settings(&settings);
     
     // Проверить, есть ли скрипт для выполнения
     if (settings.startup_script_path[0] != '\0')
     {
         // Проверить, существует ли файл
         if (GetFileAttributes(settings.startup_script_path) != INVALID_FILE_ATTRIBUTES)
         {
             char extension[5] = {0};
             char command[MAX_PATH_LEN * 2] = {0};
             
             // Получить расширение файла
             const char* dot = strrchr(settings.startup_script_path, '.');
             if (dot && strlen(dot) > 1)
             {
                 strcpy(extension, dot + 1);
                 
                 // Преобразовать к нижнему регистру
                 for (int i = 0; extension[i]; i++)
                 {
                     extension[i] = tolower(extension[i]);
                 }
                 
                 // Формирование команды в зависимости от типа скрипта
                 if (strcmp(extension, "ps1") == 0)
                 {
                     // PowerShell скрипт
                     snprintf(command, sizeof(command), 
                             "powershell.exe -ExecutionPolicy Bypass -File \"%s\"", 
                             settings.startup_script_path);
                 }
                 else if (strcmp(extension, "cmd") == 0 || strcmp(extension, "bat") == 0)
                 {
                     // CMD или BAT скрипт
                     snprintf(command, sizeof(command), "\"%s\"", settings.startup_script_path);
                 }
                 
                 if (command[0] != '\0')
                 {
                     // Выполнить команду без ожидания (асинхронно)
                     STARTUPINFO si;
                     memset(&si, 0, sizeof(STARTUPINFO));
                     si.cb = sizeof(STARTUPINFO);
                     PROCESS_INFORMATION pi = {0};
                     
                     // Запускаем процесс
                     if (CreateProcess(NULL, command, NULL, NULL, FALSE,
                                     CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi))
                     {
                         // Закрываем хэндлы, так как нам не нужно ждать завершения
                         CloseHandle(pi.hProcess);
                         CloseHandle(pi.hThread);
                     }
                 }
             }
         }
     }
 }