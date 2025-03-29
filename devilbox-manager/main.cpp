#define _WIN32_IE 0x0600

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>
#include <direct.h>
#include <tlhelp32.h>
#include <commctrl.h>
#include <time.h>
#include "utils/backup_utils.h"
#include "utils/logs_viewer.h"
#include "utils/settings.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' \
version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

/*******************************************************************************
 * Constants and Definitions
 *******************************************************************************/
#define APP_NAME "DevilboxManager"
#define REG_KEY "Software\\DevilboxManager"
#define MAX_PATH_LEN 260
#define MAX_PROJECTS 100
#define MAX_VERSIONS 20
#define MAX_LINE 1024
#define MAX_BUFFER 1048576 // 1MB buffer for logs

// Status refresh interval in milliseconds
#define STATUS_REFRESH_INTERVAL 5000
// Cache expiry time for right-click menu
#define MENU_CACHE_EXPIRY 60000 // 60 seconds

// Menu IDs
enum
{
    IDM_START = 1001,
    IDM_STOP,
    IDM_RESTART,
    IDM_RESTART_PHP,
    IDM_RESTART_MYSQL,
    IDM_RESTART_HTTPD,
    IDM_HOSTS,
    IDM_ENV,
    IDM_EXIT,
    IDM_WWW,
    IDM_CHANGEDIR,
    IDM_CONTROL_PANEL,
    IDM_WEBSITE_OPEN = 2000,
    IDM_WEBSITE_FOLDER = 2500,
    IDM_WEBSITE_BACKUP = 2750, // Новый ID для функции бэкапа
    IDM_WEBSITE_VSCODE = 3000, // New menu ID for VSCode
    IDM_PHP_VERSION = 5000,
    IDM_HTTPD_VERSION = 5500,
    IDM_MYSQL_VERSION = 6000,
    IDM_CHECK_STATUS = 6500,
    IDM_PHP_LOGS = 6600,
    IDM_SETTINGS = 6700,
};

// ID контролов для диалога бэкапа
enum
{
    ID_SOURCE_PATH = 2001,
    ID_TARGET_PATH,
    ID_BROWSE_BTN,
    ID_EXTENSIONS_EDIT,
    ID_SUBDIRS_CHECK,
    ID_DAYS_EDIT,
    ID_BACKUP_BTN,
    ID_CANCEL_BTN
};

// Server status enum
typedef enum
{
    STATUS_UNKNOWN,
    STATUS_RUNNING,
    STATUS_STOPPED
} ServerStatus;

/*******************************************************************************
 * Structures
 *******************************************************************************/
// Project structure
typedef struct
{
    char name[100];
    char path[MAX_PATH_LEN];
    char url[MAX_PATH_LEN];
} Project;

// App state
typedef struct
{
    char path[MAX_PATH_LEN];
    char php[50];
    char httpd[50];
    char mysql[50];
    char php_versions[MAX_VERSIONS][50];
    char httpd_versions[MAX_VERSIONS][50];
    char mysql_versions[MAX_VERSIONS][50];
    int php_count;
    int httpd_count;
    int mysql_count;
    ServerStatus status;
    DWORD last_status_check;
    DWORD last_full_refresh;

    Project projects[MAX_PROJECTS];
    int project_count;

    HWND hwnd;
    HMENU menu;
    HMENU mainMenu;
    HMENU projectsMenu;
    HMENU versionsMenu;
    HMENU phpMenu;
    HMENU httpdMenu;
    HMENU mysqlMenu;
    HMENU servicesMenu;
    NOTIFYICONDATA nid;
    HANDLE mutex;
    BOOL isMenuCreated;
} AppState;

static AppState app = {0};

// Состояние диалога бэкапа
typedef struct
{
    HWND hDlg;
    HWND hSourcePath;
    HWND hTargetPath;
    HWND hExtensions;
    HWND hSubdirs;
    HWND hDays;
    char project_path[MAX_PATH_LEN];
    int project_index;
} BackupDialogState;

static BackupDialogState backup_dialog = {0};

/*******************************************************************************
 * Function Prototypes
 *******************************************************************************/
// Core functions
static BOOL init_app(HINSTANCE hInst);
static BOOL select_devilbox_dir(void);
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
static void execute_cmd(const char *cmd, BOOL wait);
static BOOL is_valid_path(const char *path);

// Configuration and state management
static void refresh_app_state(BOOL force_check);
static ServerStatus check_server_status(void);
static void do_full_refresh(void);
static void update_status_background(void);
static void scan_projects(void);
static void load_versions(void);
static void set_version(const char *type, const char *version);
static void update_tray(void);
static void restart_service(const char *service);

// Menu management
static void clean_menus(void);
static void create_menus(void);
static void update_menu_status(void);

// Функции для диалога бэкапа
static void show_backup_dialog(int project_index);
static LRESULT CALLBACK BackupDialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

/*******************************************************************************
 * Application Initialization and Core Functions
 *******************************************************************************/

/**
 * Initialize application
 */
static BOOL init_app(HINSTANCE hInst)
{
    // Initialize common controls (for date picker)
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_DATE_CLASSES;
    InitCommonControlsEx(&icex);

    // Check single instance
    app.mutex = CreateMutex(NULL, TRUE, APP_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (HWND hw = FindWindow(APP_NAME, NULL))
            SendMessage(hw, WM_USER, 0, 0);
        return FALSE;
    }

    // Register window class
    WNDCLASSEX wc;
    memset(&wc, 0, sizeof(WNDCLASSEX));
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInst;
    wc.lpszClassName = APP_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassEx(&wc);

    // Create hidden window
    app.hwnd = CreateWindowEx(0, APP_NAME, APP_NAME, WS_OVERLAPPED,
                              0, 0, 0, 0, NULL, NULL, hInst, NULL);
    if (!app.hwnd)
        return FALSE;
    ShowWindow(app.hwnd, SW_HIDE);

    // Load path from registry
    HKEY key;
    DWORD size = sizeof(app.path);
    if (RegOpenKeyEx(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &key) == ERROR_SUCCESS)
    {
        RegQueryValueEx(key, "Path", NULL, NULL, (BYTE *)app.path, &size);
        RegCloseKey(key);
    }

    // If no valid path found, ask user to select
    if (!app.path[0] || !is_valid_path(app.path))
    {
        if (!select_devilbox_dir())
            return FALSE;
    }

    // Initialize app state
    app.isMenuCreated = FALSE;
    app.last_status_check = 0;
    app.last_full_refresh = 0;
    refresh_app_state(TRUE);

    // Setup tray icon
    app.nid.cbSize = sizeof(NOTIFYICONDATA);
    app.nid.hWnd = app.hwnd;
    app.nid.uID = 1;
    app.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    app.nid.uCallbackMessage = WM_USER + 1;

    HMODULE hLib = LoadLibrary("imageres.dll");
    app.nid.hIcon = (HICON)LoadImage(hLib, MAKEINTRESOURCE(109), IMAGE_ICON, 16, 16, LR_SHARED);
    FreeLibrary(hLib);

    update_tray();
    Shell_NotifyIcon(NIM_ADD, &app.nid);

    // Загрузка настроек приложения
    AppSettings settings;
    load_settings(&settings);

    // По умолчанию начинаем свернутым в трей
    ShowWindow(app.hwnd, SW_HIDE);

    // Выполнить стартовый скрипт, если он задан в настройках
    execute_startup_script();

    return TRUE;
}

/**
 * Select Devilbox directory
 */
static BOOL select_devilbox_dir(void)
{
    BROWSEINFO bi;
    memset(&bi, 0, sizeof(BROWSEINFO));
    bi.hwndOwner = app.hwnd;
    bi.lpszTitle = "Select Devilbox Directory";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
    if (!pidl || !SHGetPathFromIDList(pidl, app.path) || !is_valid_path(app.path))
    {
        if (pidl)
            CoTaskMemFree(pidl);
        MessageBox(NULL, "Valid Devilbox path required. It should contain .env and docker-compose.yml files.",
                   "Error", MB_ICONERROR);
        return FALSE;
    }
    CoTaskMemFree(pidl);

    // Save path to registry
    HKEY key;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, REG_KEY, 0, NULL, 0, KEY_WRITE, NULL, &key, NULL) == ERROR_SUCCESS)
    {
        RegSetValueEx(key, "Path", 0, REG_SZ, (BYTE *)app.path, strlen(app.path) + 1);
        RegCloseKey(key);
    }

    return TRUE;
}

/**
 * Check if path is valid Devilbox installation
 */
static BOOL is_valid_path(const char *path)
{
    char check_path[MAX_PATH_LEN];

    snprintf(check_path, sizeof(check_path), "%s\\.env", path);
    if (GetFileAttributes(check_path) == INVALID_FILE_ATTRIBUTES)
        return FALSE;

    snprintf(check_path, sizeof(check_path), "%s\\docker-compose.yml", path);
    if (GetFileAttributes(check_path) == INVALID_FILE_ATTRIBUTES)
        return FALSE;

    return TRUE;
}

/**
 * Execute command in Devilbox directory
 */
static void execute_cmd(const char *cmd, BOOL wait)
{
    STARTUPINFO si;
    memset(&si, 0, sizeof(STARTUPINFO));
    si.cb = sizeof(STARTUPINFO);
    PROCESS_INFORMATION pi = {0};
    char full_cmd[512];

    // Change directory to the Devilbox path
    _chdir(app.path);

    if (wait)
    {
        // Execute with hidden window and wait for completion
        snprintf(full_cmd, sizeof(full_cmd), "cmd.exe /c cd %s && %s", app.path, cmd);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        if (CreateProcess(NULL, full_cmd, NULL, NULL, FALSE,
                          CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi))
        {
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }
    else
    {
        // Execute with visible window and don't wait
        snprintf(full_cmd, sizeof(full_cmd), "cmd.exe /k cd %s && %s", app.path, cmd);

        if (CreateProcess(NULL, full_cmd, NULL, NULL, FALSE,
                          CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi))
        {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }
}

/*******************************************************************************
 * Configuration and State Management
 *******************************************************************************/

/**
 * Background status update only
 */
static void update_status_background(void)
{
    app.status = check_server_status();
    app.last_status_check = GetTickCount();
    update_menu_status();
    update_tray();
}

/**
 * Do a full refresh in background (after menu appears)
 */
static void do_full_refresh(void)
{
    // Check status in a non-blocking way
    app.status = check_server_status();
    app.last_status_check = GetTickCount();

    // Reload configurations
    app.php_count = app.httpd_count = app.mysql_count = app.project_count = 0;
    load_versions();
    scan_projects();

    // Update UI
    clean_menus();
    create_menus();
    app.last_full_refresh = GetTickCount();
    update_tray();
}

/**
 * Window procedure
 */
/**
 * Window procedure
 */
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_USER + 1: // Tray icon
        if (lp == WM_RBUTTONUP || lp == WM_LBUTTONUP)
        {
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);

            // Проверка, что меню создано
            if (!app.isMenuCreated)
            {
                // Только если меню еще не создано, загружаем минимальные данные
                app.php_count = app.httpd_count = app.mysql_count = app.project_count = 0;
                load_versions();
                scan_projects();
                create_menus();
                app.isMenuCreated = TRUE;
                app.last_full_refresh = GetTickCount();
            }
            
            // Обновляем состояние меню с имеющимися данными
            update_menu_status();
            
            // Показываем меню немедленно без блокирующих операций
            TrackPopupMenu(app.menu, TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, NULL);
            
            // Планируем фоновое обновление статуса
            PostMessage(hwnd, WM_USER + 3, 0, 0);
        }
        break;

    case WM_USER + 2: // Background refresh after menu closes
        refresh_app_state(TRUE);
        break;

    case WM_USER + 3: // Background status update only
        update_status_background();
        break;

    case WM_USER + 4: // Full background refresh
        do_full_refresh();
        break;

    case WM_SIZE:
        if (wp == SIZE_MINIMIZED)
            ShowWindow(hwnd, SW_HIDE);
        break;

    case WM_COMMAND:
    {
        int cmd = LOWORD(wp);

        // Version selection
        if (cmd >= IDM_PHP_VERSION && cmd < IDM_PHP_VERSION + MAX_VERSIONS)
        {
            set_version("PHP_SERVER", app.php_versions[cmd - IDM_PHP_VERSION]);
        }
        else if (cmd >= IDM_HTTPD_VERSION && cmd < IDM_HTTPD_VERSION + MAX_VERSIONS)
        {
            set_version("HTTPD_SERVER", app.httpd_versions[cmd - IDM_HTTPD_VERSION]);
        }
        else if (cmd >= IDM_MYSQL_VERSION && cmd < IDM_MYSQL_VERSION + MAX_VERSIONS)
        {
            set_version("MYSQL_SERVER", app.mysql_versions[cmd - IDM_MYSQL_VERSION]);
        }
        // Project actions
        else if (cmd >= IDM_WEBSITE_OPEN && cmd < IDM_WEBSITE_OPEN + MAX_PROJECTS)
        {
            ShellExecute(NULL, "open", app.projects[cmd - IDM_WEBSITE_OPEN].url,
                         NULL, NULL, SW_SHOW);
        }
        else if (cmd >= IDM_WEBSITE_FOLDER && cmd < IDM_WEBSITE_FOLDER + MAX_PROJECTS)
        {
            ShellExecute(NULL, "explore", app.projects[cmd - IDM_WEBSITE_FOLDER].path,
                         NULL, NULL, SW_SHOW);
        }
        else if (cmd >= IDM_WEBSITE_BACKUP && cmd < IDM_WEBSITE_BACKUP + MAX_PROJECTS)
        {
            // Показать диалог бэкапа с индексом проекта
            show_backup_dialog(cmd - IDM_WEBSITE_BACKUP);
        }
        // New handler for VSCode integration
        else if (cmd >= IDM_WEBSITE_VSCODE && cmd < IDM_WEBSITE_VSCODE + MAX_PROJECTS)
        {
            // Launch VSCode with the project path
            ShellExecute(NULL, "open", "code",
                         app.projects[cmd - IDM_WEBSITE_VSCODE].path, NULL, SW_SHOW);
        }
        // Main menu actions
        else
            switch (cmd)
            {
            case IDM_START:
                execute_cmd("docker-compose up -d", FALSE);
                // Планируем проверку статуса через 2 секунды после запуска команды
                SetTimer(hwnd, 1, 2000, NULL);
                break;
            case IDM_STOP:
                execute_cmd("docker-compose stop", FALSE);
                // Планируем проверку статуса через 2 секунды после запуска команды
                SetTimer(hwnd, 1, 2000, NULL);
                break;
            case IDM_RESTART:
                execute_cmd("docker-compose stop && docker-compose rm -f && docker-compose up -d", FALSE);
                // Планируем проверку статуса через 3 секунды после запуска команды
                SetTimer(hwnd, 1, 3000, NULL);
                break;
            case IDM_RESTART_PHP:
                restart_service("php");
                break;
            case IDM_RESTART_MYSQL:
                restart_service("mysql");
                break;
            case IDM_RESTART_HTTPD:
                restart_service("httpd");
                break;
            case IDM_CONTROL_PANEL:
                ShellExecute(NULL, "open", "http://localhost", NULL, NULL, SW_SHOW);
                break;
            case IDM_HOSTS:
                ShellExecute(NULL, "open", "C:\\Windows\\System32\\drivers\\etc\\hosts",
                             NULL, NULL, SW_SHOW);
                break;
            case IDM_ENV:
            {
                char path[MAX_PATH_LEN];
                snprintf(path, sizeof(path), "%s\\.env", app.path);
                ShellExecute(NULL, "open", path, NULL, NULL, SW_SHOW);
                break;
            }
            case IDM_WWW:
            {
                char path[MAX_PATH_LEN];
                snprintf(path, sizeof(path), "%s\\data\\www", app.path);
                ShellExecute(NULL, "explore", path, NULL, NULL, SW_SHOW);
                break;
            }
            case IDM_CHANGEDIR:
                if (select_devilbox_dir())
                {
                    // Планируем полное обновление после смены директории
                    PostMessage(hwnd, WM_USER + 4, 0, 0);
                }
                break;
            case IDM_CHECK_STATUS:
                // Сначала показываем текущий статус
                {
                    char status_msg[100];
                    snprintf(status_msg, sizeof(status_msg), "Server Status: %s",
                             app.status == STATUS_RUNNING ? "Running" : app.status == STATUS_STOPPED ? "Stopped"
                                                                                                     : "Unknown");
                    MessageBox(NULL, status_msg, "Server Status", MB_OK | MB_ICONINFORMATION);
                }
                // Затем обновляем его в фоне
                PostMessage(hwnd, WM_USER + 3, 0, 0);
                break;
            case IDM_SETTINGS:
                show_settings_dialog(hwnd);
                break;
            case IDM_PHP_LOGS:
                show_php_logs(app.path, app.php);
                break;
            case IDM_EXIT:
                DestroyWindow(hwnd);
                break;
            }
        break;
    }

    case WM_TIMER:
        if (wp == 1)
        {
            // Таймер для отложенной проверки статуса
            KillTimer(hwnd, 1);
            PostMessage(hwnd, WM_USER + 3, 0, 0);
        }
        break;

    case WM_DESTROY:
        Shell_NotifyIcon(NIM_DELETE, &app.nid);
        clean_menus();
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wp, lp);
    }
    return 0;
}

/**
 * Refresh the application state (optimized)
 */
static void refresh_app_state(BOOL force_check)
{
    DWORD current_time = GetTickCount();

    // First, make sure we have a menu to show
    if (!app.isMenuCreated)
    {
        // Create menus immediately if they don't exist
        app.php_count = app.httpd_count = app.mysql_count = app.project_count = 0;
        load_versions();
        scan_projects();

        clean_menus();
        create_menus();
        app.isMenuCreated = TRUE;
        app.last_full_refresh = current_time;

        // Use cached status initially to avoid delays
        if (app.status == STATUS_UNKNOWN)
        {
            app.status = STATUS_STOPPED; // Default assumption
        }
    }

    // Quick status update for menu appearance - only if it's been a while
    if (current_time - app.last_status_check > STATUS_REFRESH_INTERVAL)
    {
        // For immediate response, we'll just update the menu with cached status
        // and schedule a background refresh
        app.last_status_check = current_time;
        update_menu_status();

        // Post a message to update status after menu appears
        PostMessage(app.hwnd, WM_USER + 3, 0, 0);
    }
    else if (force_check)
    {
        // For full refresh requests, we'll still defer to avoid UI blocking
        PostMessage(app.hwnd, WM_USER + 4, 0, 0);
    }
    else
    {
        // Otherwise just update status in menus
        update_menu_status();
    }

    update_tray();
}

/**
 * Check server status (more efficient, with timeout)
 */
static ServerStatus check_server_status(void)
{
    STARTUPINFO si;
    memset(&si, 0, sizeof(STARTUPINFO));
    si.cb = sizeof(STARTUPINFO);
    PROCESS_INFORMATION pi = {0};
    char cmd[512];

    _chdir(app.path);
    // Added timeout to prevent hanging if docker is unresponsive
    snprintf(cmd, sizeof(cmd), "cmd.exe /c cd %s && timeout /t 2 /nobreak > nul && docker-compose ps -q 2>nul", app.path);

    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(SECURITY_ATTRIBUTES));
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    HANDLE hReadPipe, hWritePipe;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0))
    {
        return STATUS_UNKNOWN;
    }

    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;

    DWORD start_time = GetTickCount();
    BOOL process_created = CreateProcess(NULL, cmd, NULL, NULL, TRUE,
                                         CREATE_NO_WINDOW, NULL, NULL, &si, &pi);

    if (process_created)
    {
        CloseHandle(hWritePipe);

        char buffer[1024] = {0};
        DWORD bytesRead;

        // Add a timeout to ReadFile using WaitForSingleObject
        DWORD wait_result = WaitForSingleObject(pi.hProcess, 3000); // 3 second timeout
        if (wait_result == WAIT_TIMEOUT)
        {
            // Process is taking too long, terminate it
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(hReadPipe);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return STATUS_UNKNOWN;
        }

        BOOL result = ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL);
        CloseHandle(hReadPipe);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);

        // Check if we got a response within a reasonable time
        if (GetTickCount() - start_time > 5000)
        { // Over 5 seconds is too slow
            return STATUS_UNKNOWN;
        }

        if (result && bytesRead > 0)
        {
            return strlen(buffer) > 0 ? STATUS_RUNNING : STATUS_STOPPED;
        }
    }
    else
    {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
    }

    return STATUS_UNKNOWN;
}

/**
 * Scan for projects in Devilbox data directory
 */
static void scan_projects(void)
{
    char search_path[MAX_PATH_LEN];
    snprintf(search_path, sizeof(search_path), "%s\\data\\www\\*", app.path);

    WIN32_FIND_DATA fd;
    HANDLE hFind = FindFirstFile(search_path, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY &&
            strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0)
        {

            if (app.project_count >= MAX_PROJECTS)
                break;

            strncpy(app.projects[app.project_count].name, fd.cFileName, sizeof(app.projects[0].name) - 1);
            app.projects[app.project_count].name[sizeof(app.projects[0].name) - 1] = '\0';

            snprintf(app.projects[app.project_count].path, sizeof(app.projects[0].path),
                     "%s\\data\\www\\%s\\htdocs", app.path, fd.cFileName);

            snprintf(app.projects[app.project_count].url, sizeof(app.projects[0].url),
                     "http://%s.local", fd.cFileName);

            app.project_count++;
        }
    } while (FindNextFile(hFind, &fd));

    FindClose(hFind);
}

/**
 * Load server versions from .env file (optimized)
 */
static void load_versions(void)
{
    char env_path[MAX_PATH_LEN];
    snprintf(env_path, sizeof(env_path), "%s\\.env", app.path);

    FILE *f = fopen(env_path, "r");
    if (!f)
        return;

    char line[MAX_LINE];
    int section = 0; // 0=none, 1=php, 2=httpd, 3=mysql

    // First pass - get active versions
    while (fgets(line, sizeof(line), f))
    {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;

        if (strncmp(line, "PHP_SERVER=", 11) == 0)
        {
            strncpy(app.php, line + 11, sizeof(app.php) - 1);
            app.php[strcspn(app.php, "\r\n")] = 0;
        }
        else if (strncmp(line, "HTTPD_SERVER=", 13) == 0)
        {
            strncpy(app.httpd, line + 13, sizeof(app.httpd) - 1);
            app.httpd[strcspn(app.httpd, "\r\n")] = 0;
        }
        else if (strncmp(line, "MYSQL_SERVER=", 13) == 0)
        {
            strncpy(app.mysql, line + 13, sizeof(app.mysql) - 1);
            app.mysql[strcspn(app.mysql, "\r\n")] = 0;
        }
    }

    // Second pass - collect all versions (including commented)
    rewind(f);
    while (fgets(line, sizeof(line), f))
    {
        // Section detection
        if (strstr(line, "Choose PHP Server Image"))
        {
            section = 1;
            continue;
        }
        else if (strstr(line, "Choose HTTPD Server Image"))
        {
            section = 2;
            continue;
        }
        else if (strstr(line, "Choose MySQL Server Image"))
        {
            section = 3;
            continue;
        }

        int offset = 0;
        if (line[0] == '#')
        {
            offset = 1;
            while (line[offset] == ' ' || line[offset] == '\t')
                offset++;
        }

        // Extract and store versions
        char *value_ptr = NULL;
        int value_len = 0;
        char value[50] = {0};

        // Process based on section
        if (section == 1 && (value_ptr = strstr(line + offset, "PHP_SERVER=")))
        {
            value_ptr += 11;
            value_len = sizeof(app.php_versions[0]);
            if (app.php_count < MAX_VERSIONS)
            {
                strncpy(value, value_ptr, value_len - 1);
                value[strcspn(value, "\r\n")] = 0;
                if (value[0] != 0)
                {
                    // Check for duplicates
                    BOOL found = FALSE;
                    for (int i = 0; i < app.php_count; i++)
                    {
                        if (strcmp(app.php_versions[i], value) == 0)
                        {
                            found = TRUE;
                            break;
                        }
                    }
                    if (!found)
                        strncpy(app.php_versions[app.php_count++], value, value_len);
                }
            }
        }
        else if (section == 2 && (value_ptr = strstr(line + offset, "HTTPD_SERVER=")))
        {
            value_ptr += 13;
            value_len = sizeof(app.httpd_versions[0]);
            if (app.httpd_count < MAX_VERSIONS)
            {
                strncpy(value, value_ptr, value_len - 1);
                value[strcspn(value, "\r\n")] = 0;
                if (value[0] != 0)
                {
                    // Check for duplicates
                    BOOL found = FALSE;
                    for (int i = 0; i < app.httpd_count; i++)
                    {
                        if (strcmp(app.httpd_versions[i], value) == 0)
                        {
                            found = TRUE;
                            break;
                        }
                    }
                    if (!found)
                        strncpy(app.httpd_versions[app.httpd_count++], value, value_len);
                }
            }
        }
        else if (section == 3 && (value_ptr = strstr(line + offset, "MYSQL_SERVER=")))
        {
            value_ptr += 13;
            value_len = sizeof(app.mysql_versions[0]);
            if (app.mysql_count < MAX_VERSIONS)
            {
                strncpy(value, value_ptr, value_len - 1);
                value[strcspn(value, "\r\n")] = 0;
                if (value[0] != 0)
                {
                    // Check for duplicates
                    BOOL found = FALSE;
                    for (int i = 0; i < app.mysql_count; i++)
                    {
                        if (strcmp(app.mysql_versions[i], value) == 0)
                        {
                            found = TRUE;
                            break;
                        }
                    }
                    if (!found)
                        strncpy(app.mysql_versions[app.mysql_count++], value, value_len);
                }
            }
        }
    }

    fclose(f);
}

/**
 * Set server version (optimized)
 */
static void set_version(const char *type, const char *version)
{
    char env_path[MAX_PATH_LEN], tmp_path[MAX_PATH_LEN];
    snprintf(env_path, sizeof(env_path), "%s\\.env", app.path);
    snprintf(tmp_path, sizeof(tmp_path), "%s\\.env.tmp", app.path);

    FILE *in = fopen(env_path, "r");
    FILE *out = fopen(tmp_path, "w");
    if (!in || !out)
    {
        if (in)
            fclose(in);
        if (out)
            fclose(out);
        return;
    }

    // Update app state
    if (strcmp(type, "PHP_SERVER") == 0)
        strncpy(app.php, version, sizeof(app.php) - 1);
    else if (strcmp(type, "HTTPD_SERVER") == 0)
        strncpy(app.httpd, version, sizeof(app.httpd) - 1);
    else if (strcmp(type, "MYSQL_SERVER") == 0)
        strncpy(app.mysql, version, sizeof(app.mysql) - 1);

    // Process env file
    char line[MAX_LINE];
    char type_prefix[50];
    snprintf(type_prefix, sizeof(type_prefix), "%s=", type);

    while (fgets(line, sizeof(line), in))
    {
        if (strncmp(line, type_prefix, strlen(type_prefix)) == 0)
        {
            fprintf(out, "%s%s\n", type_prefix, version);

            // Comment out other versions
            while (fgets(line, sizeof(line), in))
            {
                if (line[0] == '\n' || !strstr(line, type_prefix))
                {
                    fputs(line, out);
                    break;
                }
                fprintf(out, "#%s", line);
            }
        }
        else
        {
            fputs(line, out);
        }
    }

    fclose(in);
    fclose(out);

    remove(env_path);
    rename(tmp_path, env_path);

    // Update the interface
    update_tray();
    update_menu_status();

    // Ask user if they want to restart services
    if (MessageBox(NULL, "Version changed. Do you want to restart Devilbox to apply changes?",
                   "Restart Required", MB_YESNO | MB_ICONQUESTION) == IDYES)
    {
        execute_cmd("docker-compose stop && docker-compose rm -f && docker-compose up -d", FALSE);
        refresh_app_state(TRUE);
    }
}

/**
 * Update tray icon tooltip
 */
static void update_tray(void)
{
    char tooltip[128];
    snprintf(tooltip, sizeof(tooltip),
             "Devilbox Manager\nStatus: %s\nPHP: %s\nWeb: %s\nDB: %s",
             app.status == STATUS_RUNNING ? "Running" : app.status == STATUS_STOPPED ? "Stopped"
                                                                                     : "Unknown",
             app.php, app.httpd, app.mysql);
    strncpy(app.nid.szTip, tooltip, sizeof(app.nid.szTip) - 1);
    Shell_NotifyIcon(NIM_MODIFY, &app.nid);
}

/**
 * Restart individual service
 */
static void restart_service(const char *service)
{
    if (!strcmp(service, "php") || !strcmp(service, "httpd") || !strcmp(service, "mysql"))
    {
        char cmd[512];
        snprintf(cmd, sizeof(cmd),
                 "docker-compose stop %s && docker-compose rm -f %s && docker-compose up -d %s",
                 service, service, service);

        char msg[512];
        snprintf(msg, sizeof(msg), "Restarting %s service...", service);
        MessageBox(NULL, msg, "Service Restart", MB_OK | MB_ICONINFORMATION);

        execute_cmd(cmd, FALSE);
        refresh_app_state(TRUE);
    }
    else
    {
        MessageBox(NULL, "Unknown service type", "Error", MB_ICONERROR);
    }
}

/*******************************************************************************
 * Menu Management Functions
 *******************************************************************************/

/**
 * Clean up menus
 */
static void clean_menus(void)
{
    if (app.phpMenu)
        DestroyMenu(app.phpMenu);
    if (app.httpdMenu)
        DestroyMenu(app.httpdMenu);
    if (app.mysqlMenu)
        DestroyMenu(app.mysqlMenu);
    if (app.versionsMenu)
        DestroyMenu(app.versionsMenu);
    if (app.projectsMenu)
        DestroyMenu(app.projectsMenu);
    if (app.servicesMenu)
        DestroyMenu(app.servicesMenu);
    if (app.menu)
        DestroyMenu(app.menu);
    if (app.mainMenu)
        DestroyMenu(app.mainMenu);

    app.phpMenu = app.httpdMenu = app.mysqlMenu = NULL;
    app.versionsMenu = app.projectsMenu = app.servicesMenu = NULL;
    app.menu = app.mainMenu = NULL;
}

/**
 * Create all menus - optimized to combine main menu & context menu creation
 */
static void create_menus(void)
{
    // Create base menus
    app.menu = CreatePopupMenu();
    app.mainMenu = CreateMenu();
    app.versionsMenu = CreatePopupMenu();
    app.projectsMenu = CreatePopupMenu();
    app.phpMenu = CreatePopupMenu();
    app.httpdMenu = CreatePopupMenu();
    app.mysqlMenu = CreatePopupMenu();
    app.servicesMenu = CreatePopupMenu();

    // Status section for tray menu
    char status_text[50];
    snprintf(status_text, sizeof(status_text), "Status: %s",
             app.status == STATUS_RUNNING ? "Running" : app.status == STATUS_STOPPED ? "Stopped"
                                                                                     : "Unknown");
    AppendMenu(app.menu, MF_STRING | MF_DISABLED, 0, status_text);

    char config_text[100];
    snprintf(config_text, sizeof(config_text), "PHP: %s | Web: %s | DB: %s",
             app.php, app.httpd, app.mysql);
    AppendMenu(app.menu, MF_STRING | MF_DISABLED, 0, config_text);
    AppendMenu(app.menu, MF_SEPARATOR, 0, NULL);

    // Create version menus
    BOOL servicesRunning = (app.status == STATUS_RUNNING);

    // PHP version menu
    for (int i = 0; i < app.php_count; i++)
        AppendMenu(app.phpMenu, MF_STRING | (strcmp(app.php, app.php_versions[i]) == 0 ? MF_CHECKED : 0),
                   IDM_PHP_VERSION + i, app.php_versions[i]);

    // Web server version menu
    for (int i = 0; i < app.httpd_count; i++)
        AppendMenu(app.httpdMenu, MF_STRING | (strcmp(app.httpd, app.httpd_versions[i]) == 0 ? MF_CHECKED : 0),
                   IDM_HTTPD_VERSION + i, app.httpd_versions[i]);

    // MySQL version menu
    for (int i = 0; i < app.mysql_count; i++)
        AppendMenu(app.mysqlMenu, MF_STRING | (strcmp(app.mysql, app.mysql_versions[i]) == 0 ? MF_CHECKED : 0),
                   IDM_MYSQL_VERSION + i, app.mysql_versions[i]);

    // Add version submenus
    AppendMenu(app.versionsMenu, MF_POPUP, (UINT_PTR)app.phpMenu, "PHP Version");
    AppendMenu(app.versionsMenu, MF_POPUP, (UINT_PTR)app.httpdMenu, "Web Server Version");
    AppendMenu(app.versionsMenu, MF_POPUP, (UINT_PTR)app.mysqlMenu, "Database Version");

    // Create projects menu
    if (app.project_count > 0)
    {
        for (int i = 0; i < app.project_count; i++)
        {
            HMENU proj_menu = CreatePopupMenu();
            AppendMenu(proj_menu, MF_STRING, IDM_WEBSITE_OPEN + i, "Open in Browser");
            AppendMenu(proj_menu, MF_STRING, IDM_WEBSITE_FOLDER + i, "Open Folder");
            AppendMenu(proj_menu, MF_STRING, IDM_WEBSITE_BACKUP + i, "Backup Files");
            // Add the VSCode option to each project's submenu
            AppendMenu(proj_menu, MF_STRING, IDM_WEBSITE_VSCODE + i, "Open in VSCode");
            AppendMenu(app.projectsMenu, MF_POPUP, (UINT_PTR)proj_menu, app.projects[i].name);
        }
    }
    else
    {
        AppendMenu(app.projectsMenu, MF_STRING | MF_DISABLED, 0, "No projects found");
    }

    // Create services control submenu - Always enabled to ensure restart options are available
    AppendMenu(app.servicesMenu, MF_STRING | (servicesRunning ? MF_ENABLED : MF_ENABLED),
               IDM_RESTART_PHP, "Restart PHP");
    AppendMenu(app.servicesMenu, MF_STRING | (servicesRunning ? MF_ENABLED : MF_ENABLED),
               IDM_RESTART_MYSQL, "Restart MySQL");
    AppendMenu(app.servicesMenu, MF_STRING | (servicesRunning ? MF_ENABLED : MF_ENABLED),
               IDM_RESTART_HTTPD, "Restart Web Server");

    // Create tray context menu
    AppendMenu(app.menu, MF_POPUP, (UINT_PTR)app.versionsMenu, "Server Versions");
    AppendMenu(app.menu, MF_SEPARATOR, 0, NULL);
    AppendMenu(app.menu, MF_POPUP, (UINT_PTR)app.projectsMenu, "Projects");
    AppendMenu(app.menu, MF_SEPARATOR, 0, NULL);

    // Server control section
    AppendMenu(app.menu, MF_STRING, IDM_START, "Start Devilbox");
    AppendMenu(app.menu, MF_STRING, IDM_STOP, "Stop Devilbox");
    AppendMenu(app.menu, MF_STRING, IDM_RESTART, "Restart Devilbox");
    AppendMenu(app.menu, MF_POPUP, (UINT_PTR)app.servicesMenu, "Service Control");
    AppendMenu(app.menu, MF_STRING, IDM_CHECK_STATUS, "Check Status");
    AppendMenu(app.menu, MF_SEPARATOR, 0, NULL);

    // Configuration section
    AppendMenu(app.menu, MF_STRING, IDM_CONTROL_PANEL, "Control Panel");
    AppendMenu(app.menu, MF_STRING, IDM_WWW, "Open Projects Folder");
    AppendMenu(app.menu, MF_STRING, IDM_HOSTS, "Edit hosts");
    AppendMenu(app.menu, MF_STRING, IDM_ENV, "Edit .env");
    AppendMenu(app.menu, MF_STRING, IDM_PHP_LOGS, "View PHP Error Logs");
    AppendMenu(app.menu, MF_STRING, IDM_CHANGEDIR, "Change Devilbox Directory");
    AppendMenu(app.menu, MF_STRING, IDM_SETTINGS, "Settings");
    AppendMenu(app.menu, MF_SEPARATOR, 0, NULL);

    // Exit option
    AppendMenu(app.menu, MF_STRING, IDM_EXIT, "Exit");

    // Create main window menu (simplified)
    HMENU fileMenu = CreateMenu();
    HMENU configMenu = CreateMenu();

    // File menu
    AppendMenu(fileMenu, MF_STRING, IDM_START, "Start Devilbox");
    AppendMenu(fileMenu, MF_STRING, IDM_STOP, "Stop Devilbox");
    AppendMenu(fileMenu, MF_STRING, IDM_RESTART, "Restart Devilbox");
    AppendMenu(fileMenu, MF_STRING, IDM_CHECK_STATUS, "Check Status");
    AppendMenu(fileMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(fileMenu, MF_STRING, IDM_CHANGEDIR, "Change Devilbox Directory");
    AppendMenu(fileMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(fileMenu, MF_STRING, IDM_EXIT, "Exit");

    // Config menu
    AppendMenu(configMenu, MF_STRING, IDM_CONTROL_PANEL, "Control Panel");
    AppendMenu(configMenu, MF_STRING, IDM_WWW, "Open Projects Folder");
    AppendMenu(configMenu, MF_STRING, IDM_HOSTS, "Edit hosts");
    AppendMenu(configMenu, MF_STRING, IDM_ENV, "Edit .env");
    AppendMenu(configMenu, MF_STRING, IDM_PHP_LOGS, "View PHP Error Logs");

    // Add all menus to main menu
    AppendMenu(app.mainMenu, MF_POPUP, (UINT_PTR)fileMenu, "File");
    AppendMenu(app.mainMenu, MF_POPUP, (UINT_PTR)app.projectsMenu, "Sites");
    AppendMenu(app.mainMenu, MF_POPUP, (UINT_PTR)app.versionsMenu, "Versions");
    AppendMenu(app.mainMenu, MF_POPUP, (UINT_PTR)app.servicesMenu, "Service Control");
    AppendMenu(app.mainMenu, MF_POPUP, (UINT_PTR)configMenu, "Configuration");

    // Set the menu
    SetMenu(app.hwnd, app.mainMenu);
}

/**
 * Показать диалог бэкапа для проекта
 */
static void show_backup_dialog(int project_index)
{
    // Проверка, не открыт ли уже диалог
    if (backup_dialog.hDlg && IsWindow(backup_dialog.hDlg))
    {
        SetForegroundWindow(backup_dialog.hDlg);
        return;
    }

    // Сохраняем информацию о проекте
    backup_dialog.project_index = project_index;
    strncpy(backup_dialog.project_path, app.projects[project_index].path, MAX_PATH_LEN - 1);

    // Регистрируем класс диалога
    WNDCLASSEX wcDialog;
    memset(&wcDialog, 0, sizeof(WNDCLASSEX));
    wcDialog.cbSize = sizeof(WNDCLASSEX);
    wcDialog.lpfnWndProc = BackupDialogProc;
    wcDialog.hInstance = GetModuleHandle(NULL);
    wcDialog.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcDialog.lpszClassName = "DevilboxBackupDialog";
    RegisterClassEx(&wcDialog);

    // Создаем окно диалога
    backup_dialog.hDlg = CreateWindowEx(
        WS_EX_DLGMODALFRAME,
        "DevilboxBackupDialog",
        "Backup Project Files",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        100, 100, 600, 380,
        NULL, NULL, GetModuleHandle(NULL), NULL);

    if (!backup_dialog.hDlg)
    {
        MessageBox(NULL, "Failed to create backup dialog window.", "Error", MB_ICONERROR);
        return;
    }

    // Создаем элементы управления
    create_control(backup_dialog.hDlg, "STATIC", "Source Directory:",
                   WS_CHILD | WS_VISIBLE, 10, 20, 120, 20, NULL, 0);

    backup_dialog.hSourcePath = create_control(backup_dialog.hDlg, "EDIT", backup_dialog.project_path,
                                               WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_READONLY,
                                               130, 20, 450, 25, (HMENU)ID_SOURCE_PATH, WS_EX_CLIENTEDGE);

    create_control(backup_dialog.hDlg, "STATIC", "Target Directory:",
                   WS_CHILD | WS_VISIBLE, 10, 60, 120, 20, NULL, 0);

    backup_dialog.hTargetPath = create_control(backup_dialog.hDlg, "EDIT", "",
                                               WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                               130, 60, 370, 25, (HMENU)ID_TARGET_PATH, WS_EX_CLIENTEDGE);

    create_control(backup_dialog.hDlg, "BUTTON", "Browse...",
                   WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                   510, 60, 70, 25, (HMENU)ID_BROWSE_BTN, 0);

    create_control(backup_dialog.hDlg, "STATIC", "File Extensions (separated by comma):",
                   WS_CHILD | WS_VISIBLE, 10, 100, 250, 20, NULL, 0);

    backup_dialog.hExtensions = create_control(backup_dialog.hDlg, "EDIT", "php,js,css,tpl,.htaccess",
                                               WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                               10, 125, 570, 25, (HMENU)ID_EXTENSIONS_EDIT, WS_EX_CLIENTEDGE);

    backup_dialog.hSubdirs = create_control(backup_dialog.hDlg, "BUTTON", "Include subdirectories",
                                            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
                                            10, 165, 200, 25, (HMENU)ID_SUBDIRS_CHECK, 0);

    create_control(backup_dialog.hDlg, "STATIC", "Days to look back:",
                   WS_CHILD | WS_VISIBLE, 10, 200, 120, 20, NULL, 0);

    backup_dialog.hDays = create_control(backup_dialog.hDlg, "EDIT", "7",
                                         WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
                                         130, 200, 50, 25, (HMENU)ID_DAYS_EDIT, WS_EX_CLIENTEDGE);

    // Устанавливаем состояние чекбокса по умолчанию (включено)
    SendMessage(backup_dialog.hSubdirs, BM_SETCHECK, BST_CHECKED, 0);

    // Кнопки
    create_control(backup_dialog.hDlg, "BUTTON", "Backup",
                   WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
                   400, 300, 80, 30, (HMENU)ID_BACKUP_BTN, 0);

    create_control(backup_dialog.hDlg, "BUTTON", "Cancel",
                   WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                   490, 300, 80, 30, (HMENU)ID_CANCEL_BTN, 0);

    // Информационная надпись
    create_control(backup_dialog.hDlg, "STATIC",
                   "Files modified in the specified number of days will be copied to the target directory.",
                   WS_CHILD | WS_VISIBLE, 10, 240, 570, 20, NULL, 0);

    // Устанавливаем иконку
    HICON hIcon = LoadIcon(NULL, IDI_APPLICATION);
    SendMessage(backup_dialog.hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    SendMessage(backup_dialog.hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

    // Отображаем окно
    ShowWindow(backup_dialog.hDlg, SW_SHOW);
    UpdateWindow(backup_dialog.hDlg);
}

/**
 * Процедура окна для диалога бэкапа
 */
static LRESULT CALLBACK BackupDialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case ID_BROWSE_BTN:
        {
            BROWSEINFO bi;
            memset(&bi, 0, sizeof(BROWSEINFO));
            bi.hwndOwner = hwnd;
            bi.lpszTitle = "Select Target Directory";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

            LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
            if (pidl)
            {
                char path[MAX_PATH_LEN];
                if (SHGetPathFromIDList(pidl, path))
                {
                    SetWindowText(backup_dialog.hTargetPath, path);
                }
                CoTaskMemFree(pidl);
            }
            break;
        }

        case ID_BACKUP_BTN:
        {
            char source_path[MAX_PATH_LEN] = {0};
            char target_path[MAX_PATH_LEN] = {0};
            char extensions[MAX_PATH_LEN] = {0};
            char days_str[10] = {0};
            int days = 7;        // Default value
            int include_subdirs; // Changed to int type for compatibility

            // Get values from controls
            GetWindowText(backup_dialog.hSourcePath, source_path, sizeof(source_path));
            GetWindowText(backup_dialog.hTargetPath, target_path, sizeof(target_path));
            GetWindowText(backup_dialog.hExtensions, extensions, sizeof(extensions));
            GetWindowText(backup_dialog.hDays, days_str, sizeof(days_str));

            // Convert checkbox state to int (0 or 1)
            include_subdirs = (SendMessage(backup_dialog.hSubdirs, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;

            // Convert days string to number
            if (strlen(days_str) > 0)
            {
                days = atoi(days_str);
                if (days <= 0)
                    days = 7; // Default if invalid
            }

            // Validate required fields
            if (strlen(source_path) == 0 || strlen(target_path) == 0)
            {
                MessageBox(hwnd, "Source and target directories must be specified.",
                           "Validation Error", MB_ICONWARNING);
                break;
            }

            // Execute backup using function from backup_utils.h
            execute_backup(source_path, target_path, extensions, include_subdirs, days);
            DestroyWindow(hwnd);
            break;
        }

        case ID_CANCEL_BTN:
            DestroyWindow(hwnd);
            break;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        break;

    case WM_DESTROY:
        backup_dialog.hDlg = NULL;
        break;

    default:
        return DefWindowProc(hwnd, msg, wp, lp);
    }
    return 0;
}

/**
 * Update menu status without rebuilding
 */
static void update_menu_status(void)
{
    if (!app.menu)
        return;

    // Update status text
    char status_text[50];
    snprintf(status_text, sizeof(status_text), "Status: %s",
             app.status == STATUS_RUNNING ? "Running" : app.status == STATUS_STOPPED ? "Stopped"
                                                                                     : "Unknown");

    MENUITEMINFO mii;
    memset(&mii, 0, sizeof(MENUITEMINFO));
    mii.cbSize = sizeof(MENUITEMINFO);
    mii.fMask = MIIM_STRING;
    mii.dwTypeData = status_text;
    SetMenuItemInfo(app.menu, 0, TRUE, &mii);

    // Update configuration text
    char config_text[100];
    snprintf(config_text, sizeof(config_text), "PHP: %s | Web: %s | DB: %s",
             app.php, app.httpd, app.mysql);
    mii.dwTypeData = config_text;
    mii.cbSize = sizeof(MENUITEMINFO);
    mii.fMask = MIIM_STRING;
    SetMenuItemInfo(app.menu, 1, TRUE, &mii);

    // Update checkmarks in version menus
    if (app.phpMenu)
    {
        for (int i = 0; i < app.php_count; i++)
        {
            CheckMenuItem(app.phpMenu, IDM_PHP_VERSION + i,
                          MF_BYCOMMAND | (strcmp(app.php, app.php_versions[i]) == 0 ? MF_CHECKED : MF_UNCHECKED));
        }
    }

    if (app.httpdMenu)
    {
        for (int i = 0; i < app.httpd_count; i++)
        {
            CheckMenuItem(app.httpdMenu, IDM_HTTPD_VERSION + i,
                          MF_BYCOMMAND | (strcmp(app.httpd, app.httpd_versions[i]) == 0 ? MF_CHECKED : MF_UNCHECKED));
        }
    }

    if (app.mysqlMenu)
    {
        for (int i = 0; i < app.mysql_count; i++)
        {
            CheckMenuItem(app.mysqlMenu, IDM_MYSQL_VERSION + i,
                          MF_BYCOMMAND | (strcmp(app.mysql, app.mysql_versions[i]) == 0 ? MF_CHECKED : MF_UNCHECKED));
        }
    }

    // Make sure service restart options are always enabled
    if (app.servicesMenu)
    {
        EnableMenuItem(app.servicesMenu, IDM_RESTART_PHP, MF_BYCOMMAND | MF_ENABLED);
        EnableMenuItem(app.servicesMenu, IDM_RESTART_MYSQL, MF_BYCOMMAND | MF_ENABLED);
        EnableMenuItem(app.servicesMenu, IDM_RESTART_HTTPD, MF_BYCOMMAND | MF_ENABLED);
    }
}

/*******************************************************************************
 * Entry Point
 *******************************************************************************/
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    // Initialize COM
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    // Initialize application
    if (!init_app(hInstance))
    {
        CoUninitialize();
        return 0;
    }

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();
    return msg.wParam;
}