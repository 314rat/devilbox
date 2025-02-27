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

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' \
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
#define MAX_BUFFER 1048576  // 1MB buffer for logs

// Status refresh interval in milliseconds
#define STATUS_REFRESH_INTERVAL 5000

// Menu IDs
enum {
    IDM_START = 1001,
    IDM_STOP,
    IDM_RESTART,
    IDM_HOSTS,
    IDM_ENV,
    IDM_EXIT,
    IDM_WWW,
    IDM_CHANGEDIR,
    IDM_CONTROL_PANEL,
    IDM_WEBSITE_OPEN = 2000,
    IDM_WEBSITE_FOLDER = 2500,
    IDM_PHP_VERSION = 5000,
    IDM_HTTPD_VERSION = 5500,
    IDM_MYSQL_VERSION = 6000,
    IDM_CHECK_STATUS = 6500,
    IDM_PHP_LOGS = 6600,
};

// Log dialog control IDs
enum {
    ID_LOGS_EDIT = 100,
    ID_REFRESH_BTN = 101,
    ID_CLOSE_BTN = 102,
    ID_CLEAR_BTN = 103,
    ID_DATE_FILTER_START = 104,
    ID_DATE_FILTER_END = 105,
    ID_TIME_FILTER_START = 106,
    ID_TIME_FILTER_END = 107,
    ID_APPLY_FILTER_BTN = 108,
    ID_RESET_FILTER_BTN = 109
};

// Server status enum
typedef enum {
    STATUS_UNKNOWN,
    STATUS_RUNNING,
    STATUS_STOPPED
} ServerStatus;

/*******************************************************************************
 * Structures
 *******************************************************************************/
// Project structure
typedef struct {
    char name[100];
    char path[MAX_PATH_LEN];
    char url[MAX_PATH_LEN];
} Project;

// App state
typedef struct {
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
    NOTIFYICONDATA nid;
    HANDLE mutex;
    BOOL isMenuCreated;
} AppState;

// Logs dialog state
typedef struct {
    HWND hDlg;
    HWND hEdit;
    HWND hDateStart;
    HWND hDateEnd;
    HWND hTimeStart;
    HWND hTimeEnd;
    char log_path[MAX_PATH_LEN];
    BOOL filtering;
    SYSTEMTIME filterStartDate;
    SYSTEMTIME filterEndDate;
    SYSTEMTIME filterStartTime;
    SYSTEMTIME filterEndTime;
} LogsDialogState;

static AppState app = {0};
static LogsDialogState logs_dialog = {0};

/*******************************************************************************
 * Function Prototypes
 *******************************************************************************/
// Application initialization
static BOOL init_app(HINSTANCE hInst);
static BOOL select_devilbox_dir(void);
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

// Menu functions
static void create_menus(void);
static void create_main_menu(void);
static void update_menu_status(void);
static void clean_menus(void);

// Configuration functions
static void scan_projects(void);
static void load_versions(void);
static void set_version(const char* type, const char* version);
static void update_tray(void);
static BOOL is_valid_path(const char* path);

// Server control functions
static void execute_cmd(const char* cmd, BOOL wait);
static ServerStatus check_server_status(void);
static void refresh_app_state(BOOL force_check);

// Log functions
static void show_php_logs(void);
static void refresh_log_content(void);
static void clear_log_file(void);
static void apply_log_filter(void);
static void reset_log_filter(void);
static BOOL parse_log_date(const char *log_line, SYSTEMTIME *datetime);
static LRESULT CALLBACK LogsDialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

/*******************************************************************************
 * Application Initialization
 *******************************************************************************/

/**
 * Initialize application
 */
static BOOL init_app(HINSTANCE hInst) {
    // Initialize common controls (for date picker)
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_DATE_CLASSES;
    InitCommonControlsEx(&icex);

    // Check single instance
    app.mutex = CreateMutex(NULL, TRUE, APP_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND hw = FindWindow(APP_NAME, NULL)) 
            SendMessage(hw, WM_USER, 0, 0);
        return FALSE;
    }

    // Register window class
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInst;
    wc.lpszClassName = APP_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassEx(&wc);

    // Create window (hidden)
    app.hwnd = CreateWindowEx(0, APP_NAME, APP_NAME, 
        WS_OVERLAPPED, 0, 0, 0, 0, NULL, NULL, hInst, NULL);
    if (!app.hwnd) return FALSE;

    ShowWindow(app.hwnd, SW_HIDE);

    // Load path from registry
    HKEY key;
    DWORD size = sizeof(app.path);
    if (RegOpenKeyEx(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        RegQueryValueEx(key, "Path", NULL, NULL, (BYTE*)app.path, &size);
        RegCloseKey(key);
    }
    
    // If no valid path found, ask user to select
    if (!app.path[0] || !is_valid_path(app.path)) {
        if (!select_devilbox_dir()) {
            return FALSE;
        }
    }

    // Initialize app state
    app.isMenuCreated = FALSE;
    app.last_status_check = 0;
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

    return TRUE;
}

/**
 * Select Devilbox directory
 */
static BOOL select_devilbox_dir(void) {
    BROWSEINFO bi = {0};
    bi.hwndOwner = app.hwnd;
    bi.lpszTitle = "Select Devilbox Directory";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    
    LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
    if (!pidl || !SHGetPathFromIDList(pidl, app.path) || !is_valid_path(app.path)) {
        if (pidl) CoTaskMemFree(pidl);
        MessageBox(NULL, "Valid Devilbox path required. It should contain .env and docker-compose.yml files.", 
                   "Error", MB_ICONERROR);
        return FALSE;
    }
    CoTaskMemFree(pidl);
    
    // Save path to registry
    HKEY key;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, REG_KEY, 0, NULL, 0, KEY_WRITE, NULL, &key, NULL) == ERROR_SUCCESS) {
        RegSetValueEx(key, "Path", 0, REG_SZ, (BYTE*)app.path, strlen(app.path) + 1);
        RegCloseKey(key);
    }
    
    return TRUE;
}

/**
 * Refresh the application state
 */
static void refresh_app_state(BOOL force_check) {
    DWORD current_time = GetTickCount();
    
    // Check if we need to update the server status
    if (force_check || (current_time - app.last_status_check > STATUS_REFRESH_INTERVAL)) {
        app.status = check_server_status();
        app.last_status_check = current_time;
    }
    
    // If first time or forced refresh, reload all configurations
    if (force_check || !app.isMenuCreated) {
        // Clear previous state
        app.php_count = 0;
        app.httpd_count = 0;
        app.mysql_count = 0;
        app.project_count = 0;
        
        // Load versions and scan projects
        load_versions();
        scan_projects();
        
        // Create/recreate menus
        clean_menus();
        create_menus();
        create_main_menu();
        app.isMenuCreated = TRUE;
    } else {
        // Just update status in menus
        update_menu_status();
    }
    
    // Update tray
    update_tray();
}

/*******************************************************************************
 * Window Procedure
 *******************************************************************************/

/**
 * Window procedure
 */
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_USER + 1: // Tray icon
            if (lp == WM_RBUTTONUP || lp == WM_LBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);
                SetForegroundWindow(hwnd);
                
                // Update state without forcing a complete reload
                refresh_app_state(FALSE);
                
                TrackPopupMenu(app.menu, TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, NULL);
            }
            break;

        case WM_SIZE:
            if (wp == SIZE_MINIMIZED) {
                ShowWindow(hwnd, SW_HIDE);
            }
            break;

        case WM_COMMAND: {
            int cmd = LOWORD(wp);
            
            // Version selection
            if (cmd >= IDM_PHP_VERSION && cmd < IDM_PHP_VERSION + MAX_VERSIONS) {
                set_version("PHP_SERVER", app.php_versions[cmd - IDM_PHP_VERSION]);
            }
            else if (cmd >= IDM_HTTPD_VERSION && cmd < IDM_HTTPD_VERSION + MAX_VERSIONS) {
                set_version("HTTPD_SERVER", app.httpd_versions[cmd - IDM_HTTPD_VERSION]);
            }
            else if (cmd >= IDM_MYSQL_VERSION && cmd < IDM_MYSQL_VERSION + MAX_VERSIONS) {
                set_version("MYSQL_SERVER", app.mysql_versions[cmd - IDM_MYSQL_VERSION]);
            }
            // Project actions
            else if (cmd >= IDM_WEBSITE_OPEN && cmd < IDM_WEBSITE_OPEN + MAX_PROJECTS) {
                ShellExecute(NULL, "open", app.projects[cmd - IDM_WEBSITE_OPEN].url, 
                    NULL, NULL, SW_SHOW);
            }
            else if (cmd >= IDM_WEBSITE_FOLDER && cmd < IDM_WEBSITE_FOLDER + MAX_PROJECTS) {
                ShellExecute(NULL, "explore", app.projects[cmd - IDM_WEBSITE_FOLDER].path, 
                    NULL, NULL, SW_SHOW);
            }
            // Main menu actions
            else switch (cmd) {
                case IDM_START:    
                    execute_cmd("docker-compose up -d", FALSE);
                    refresh_app_state(TRUE);
                    break;
                case IDM_STOP:     
                    execute_cmd("docker-compose stop", FALSE);
                    refresh_app_state(TRUE);
                    break;
                case IDM_RESTART:  
                    execute_cmd("docker-compose stop && docker-compose rm -f && docker-compose up -d", FALSE);
                    refresh_app_state(TRUE);
                    break;
                case IDM_CONTROL_PANEL:
                    ShellExecute(NULL, "open", "http://localhost", NULL, NULL, SW_SHOW);
                    break;
                case IDM_HOSTS:
                    ShellExecute(NULL, "open", "C:\\Windows\\System32\\drivers\\etc\\hosts", 
                        NULL, NULL, SW_SHOW);
                    break;
                case IDM_ENV: {
                    char path[MAX_PATH_LEN];
                    snprintf(path, sizeof(path), "%s\\.env", app.path);
                    ShellExecute(NULL, "open", path, NULL, NULL, SW_SHOW);
                    break;
                }
                case IDM_WWW: {
                    char path[MAX_PATH_LEN];
                    snprintf(path, sizeof(path), "%s\\data\\www", app.path);
                    ShellExecute(NULL, "explore", path, NULL, NULL, SW_SHOW);
                    break;
                }
                case IDM_CHANGEDIR:
                    if (select_devilbox_dir()) {
                        refresh_app_state(TRUE);
                    }
                    break;
                case IDM_CHECK_STATUS:
                    refresh_app_state(TRUE);
                    {
                        char status_msg[100];
                        snprintf(status_msg, sizeof(status_msg), "Server Status: %s", 
                                app.status == STATUS_RUNNING ? "Running" : 
                                app.status == STATUS_STOPPED ? "Stopped" : "Unknown");
                        MessageBox(NULL, status_msg, "Server Status", MB_OK | MB_ICONINFORMATION);
                    }
                    break;
                case IDM_PHP_LOGS:
                    show_php_logs();
                    break;
                case IDM_EXIT:
                    DestroyWindow(hwnd);
                    break;
            }
            break;
        }

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

/*******************************************************************************
 * Menu Management Functions
 *******************************************************************************/

/**
 * Clean up menus when exiting or recreating
 */
static void clean_menus(void) {
    if (app.phpMenu) DestroyMenu(app.phpMenu);
    if (app.httpdMenu) DestroyMenu(app.httpdMenu);
    if (app.mysqlMenu) DestroyMenu(app.mysqlMenu);
    if (app.versionsMenu) DestroyMenu(app.versionsMenu);
    if (app.projectsMenu) DestroyMenu(app.projectsMenu);
    if (app.menu) DestroyMenu(app.menu);
    if (app.mainMenu) DestroyMenu(app.mainMenu);
    
    app.phpMenu = NULL;
    app.httpdMenu = NULL;
    app.mysqlMenu = NULL;
    app.versionsMenu = NULL;
    app.projectsMenu = NULL;
    app.menu = NULL;
    app.mainMenu = NULL;
}

/**
 * Update just the status info in the menu without rebuilding entire menu
 */
static void update_menu_status(void) {
    if (!app.menu) return;
    
    // Update status text in first menu item
    char status_text[50];
    snprintf(status_text, sizeof(status_text), "Status: %s", 
            app.status == STATUS_RUNNING ? "Running" : 
            app.status == STATUS_STOPPED ? "Stopped" : "Unknown");
    
    MENUITEMINFO mii = {0};
    mii.cbSize = sizeof(MENUITEMINFO);
    mii.fMask = MIIM_STRING;
    mii.dwTypeData = status_text;
    SetMenuItemInfo(app.menu, 0, TRUE, &mii);
    
    // Update configuration text in second menu item
    char config_text[100];
    snprintf(config_text, sizeof(config_text), "PHP: %s | Web: %s | DB: %s", 
             app.php, app.httpd, app.mysql);
    
    mii.dwTypeData = config_text;
    SetMenuItemInfo(app.menu, 1, TRUE, &mii);
    
    // Update check marks in version submenus
    if (app.phpMenu) {
        for (int i = 0; i < app.php_count; i++) {
            CheckMenuItem(app.phpMenu, IDM_PHP_VERSION + i, 
                         MF_BYCOMMAND | (strcmp(app.php, app.php_versions[i]) == 0 ? MF_CHECKED : MF_UNCHECKED));
        }
    }
    
    if (app.httpdMenu) {
        for (int i = 0; i < app.httpd_count; i++) {
            CheckMenuItem(app.httpdMenu, IDM_HTTPD_VERSION + i, 
                         MF_BYCOMMAND | (strcmp(app.httpd, app.httpd_versions[i]) == 0 ? MF_CHECKED : MF_UNCHECKED));
        }
    }
    
    if (app.mysqlMenu) {
        for (int i = 0; i < app.mysql_count; i++) {
            CheckMenuItem(app.mysqlMenu, IDM_MYSQL_VERSION + i, 
                         MF_BYCOMMAND | (strcmp(app.mysql, app.mysql_versions[i]) == 0 ? MF_CHECKED : MF_UNCHECKED));
        }
    }
}

/**
 * Create tray menu
 */
static void create_menus(void) {
    app.menu = CreatePopupMenu();
    app.versionsMenu = CreatePopupMenu();
    app.projectsMenu = CreatePopupMenu();
    app.phpMenu = CreatePopupMenu();
    app.httpdMenu = CreatePopupMenu();
    app.mysqlMenu = CreatePopupMenu();
    
    // Add server status at the top
    char status_text[50];
    snprintf(status_text, sizeof(status_text), "Status: %s", 
            app.status == STATUS_RUNNING ? "Running" : 
            app.status == STATUS_STOPPED ? "Stopped" : "Unknown");
    AppendMenu(app.menu, MF_STRING | MF_DISABLED, 0, status_text);
    
    // Add current configuration 
    char config_text[100];
    snprintf(config_text, sizeof(config_text), "PHP: %s | Web: %s | DB: %s", app.php, app.httpd, app.mysql);
    AppendMenu(app.menu, MF_STRING | MF_DISABLED, 0, config_text);
    
    AppendMenu(app.menu, MF_SEPARATOR, 0, NULL);
    
    // Create PHP version menu
    for (int i = 0; i < app.php_count; i++)
        AppendMenu(app.phpMenu, MF_STRING | (strcmp(app.php, app.php_versions[i]) == 0 ? MF_CHECKED : 0),
            IDM_PHP_VERSION + i, app.php_versions[i]);
            
    // Create web server version menu
    for (int i = 0; i < app.httpd_count; i++)
        AppendMenu(app.httpdMenu, MF_STRING | (strcmp(app.httpd, app.httpd_versions[i]) == 0 ? MF_CHECKED : 0),
            IDM_HTTPD_VERSION + i, app.httpd_versions[i]);
    
    // Create MySQL version menu       
    for (int i = 0; i < app.mysql_count; i++)
        AppendMenu(app.mysqlMenu, MF_STRING | (strcmp(app.mysql, app.mysql_versions[i]) == 0 ? MF_CHECKED : 0),
            IDM_MYSQL_VERSION + i, app.mysql_versions[i]);
    
    // Add version submenus
    AppendMenu(app.versionsMenu, MF_POPUP, (UINT_PTR)app.phpMenu, "PHP Version");
    AppendMenu(app.versionsMenu, MF_POPUP, (UINT_PTR)app.httpdMenu, "Web Server Version");
    AppendMenu(app.versionsMenu, MF_POPUP, (UINT_PTR)app.mysqlMenu, "Database Version");
    
    // Create projects menu
    if (app.project_count > 0) {
        for (int i = 0; i < app.project_count; i++) {
            HMENU proj_menu = CreatePopupMenu();
            AppendMenu(proj_menu, MF_STRING, IDM_WEBSITE_OPEN + i, "Open in Browser");
            AppendMenu(proj_menu, MF_STRING, IDM_WEBSITE_FOLDER + i, "Open Folder");
            AppendMenu(app.projectsMenu, MF_POPUP, (UINT_PTR)proj_menu, app.projects[i].name);
        }
    } else {
        AppendMenu(app.projectsMenu, MF_STRING | MF_DISABLED, 0, "No projects found");
    }
    
    // Create main menu
    AppendMenu(app.menu, MF_POPUP, (UINT_PTR)app.versionsMenu, "Server Versions");
    AppendMenu(app.menu, MF_SEPARATOR, 0, NULL);
    AppendMenu(app.menu, MF_POPUP, (UINT_PTR)app.projectsMenu, "Projects");
    AppendMenu(app.menu, MF_SEPARATOR, 0, NULL);
    
    // Server control section
    AppendMenu(app.menu, MF_STRING, IDM_START, "Start Devilbox");
    AppendMenu(app.menu, MF_STRING, IDM_STOP, "Stop Devilbox");
    AppendMenu(app.menu, MF_STRING, IDM_RESTART, "Restart Devilbox");
    AppendMenu(app.menu, MF_STRING, IDM_CHECK_STATUS, "Check Status");
    AppendMenu(app.menu, MF_SEPARATOR, 0, NULL);
    
    // Configuration section
    AppendMenu(app.menu, MF_STRING, IDM_CONTROL_PANEL, "Control Panel");
    AppendMenu(app.menu, MF_STRING, IDM_WWW, "Open Projects Folder");
    AppendMenu(app.menu, MF_STRING, IDM_HOSTS, "Edit hosts");
    AppendMenu(app.menu, MF_STRING, IDM_ENV, "Edit .env");
    AppendMenu(app.menu, MF_STRING, IDM_PHP_LOGS, "View PHP Error Logs");
    AppendMenu(app.menu, MF_STRING, IDM_CHANGEDIR, "Change Devilbox Directory");
    AppendMenu(app.menu, MF_SEPARATOR, 0, NULL);
    
    // Exit option
    AppendMenu(app.menu, MF_STRING, IDM_EXIT, "Exit");
}

/**
 * Create main menu bar (for main window if shown)
 */
static void create_main_menu(void) {
    app.mainMenu = CreateMenu();
    HMENU fileMenu = CreateMenu();
    HMENU projectsMenu = CreateMenu();
    HMENU versionsMenu = CreateMenu();
    
    // File menu
    AppendMenu(fileMenu, MF_STRING, IDM_START, "Start Devilbox");
    AppendMenu(fileMenu, MF_STRING, IDM_STOP, "Stop Devilbox");
    AppendMenu(fileMenu, MF_STRING, IDM_RESTART, "Restart Devilbox");
    AppendMenu(fileMenu, MF_STRING, IDM_CHECK_STATUS, "Check Status");
    AppendMenu(fileMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(fileMenu, MF_STRING, IDM_CHANGEDIR, "Change Devilbox Directory");
    AppendMenu(fileMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(fileMenu, MF_STRING, IDM_EXIT, "Exit");
    
    // Create PHP version menu for main menu
    HMENU phpMenu = CreatePopupMenu();
    HMENU httpdMenu = CreatePopupMenu();
    HMENU mysqlMenu = CreatePopupMenu();
    
    for (int i = 0; i < app.php_count; i++)
        AppendMenu(phpMenu, MF_STRING | (strcmp(app.php, app.php_versions[i]) == 0 ? MF_CHECKED : 0),
            IDM_PHP_VERSION + i, app.php_versions[i]);
            
    for (int i = 0; i < app.httpd_count; i++)
        AppendMenu(httpdMenu, MF_STRING | (strcmp(app.httpd, app.httpd_versions[i]) == 0 ? MF_CHECKED : 0),
            IDM_HTTPD_VERSION + i, app.httpd_versions[i]);
            
    for (int i = 0; i < app.mysql_count; i++)
        AppendMenu(mysqlMenu, MF_STRING | (strcmp(app.mysql, app.mysql_versions[i]) == 0 ? MF_CHECKED : 0),
            IDM_MYSQL_VERSION + i, app.mysql_versions[i]);
    
    AppendMenu(versionsMenu, MF_POPUP, (UINT_PTR)phpMenu, "PHP Version");
    AppendMenu(versionsMenu, MF_POPUP, (UINT_PTR)httpdMenu, "Web Server Version");
    AppendMenu(versionsMenu, MF_POPUP, (UINT_PTR)mysqlMenu, "Database Version");
    
    // Create Projects menu for main menu
    for (int i = 0; i < app.project_count; i++) {
        HMENU projectSubMenu = CreateMenu();
        AppendMenu(projectSubMenu, MF_STRING, IDM_WEBSITE_OPEN + i, "Open in Browser");
        AppendMenu(projectSubMenu, MF_STRING, IDM_WEBSITE_FOLDER + i, "Open Folder");
        AppendMenu(projectsMenu, MF_POPUP, (UINT_PTR)projectSubMenu, app.projects[i].name);
    }
    
    if (app.project_count == 0) {
        AppendMenu(projectsMenu, MF_STRING | MF_DISABLED, 0, "No projects found");
    }
    
    // Add Configuration menu
    HMENU configMenu = CreateMenu();
    AppendMenu(configMenu, MF_STRING, IDM_CONTROL_PANEL, "Control Panel");
    AppendMenu(configMenu, MF_STRING, IDM_WWW, "Open Projects Folder");
    AppendMenu(configMenu, MF_STRING, IDM_HOSTS, "Edit hosts");
    AppendMenu(configMenu, MF_STRING, IDM_ENV, "Edit .env");
    AppendMenu(configMenu, MF_STRING, IDM_PHP_LOGS, "View PHP Error Logs");
    
    // Add all menus to main menu
    AppendMenu(app.mainMenu, MF_POPUP, (UINT_PTR)fileMenu, "File");
    AppendMenu(app.mainMenu, MF_POPUP, (UINT_PTR)projectsMenu, "Sites");
    AppendMenu(app.mainMenu, MF_POPUP, (UINT_PTR)versionsMenu, "Versions");
    AppendMenu(app.mainMenu, MF_POPUP, (UINT_PTR)configMenu, "Configuration");
    
    // Set the menu
    SetMenu(app.hwnd, app.mainMenu);
}

/*******************************************************************************
 * Configuration Functions
 *******************************************************************************/

/**
 * Scan for projects in Devilbox data directory
 */
static void scan_projects(void) {
    app.project_count = 0;
    
    char search_path[MAX_PATH_LEN];
    snprintf(search_path, sizeof(search_path), "%s\\data\\www\\*", app.path);
    
    WIN32_FIND_DATA fd;
    HANDLE hFind = FindFirstFile(search_path, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY && 
            strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0) {
            
            strncpy(app.projects[app.project_count].name, fd.cFileName, sizeof(app.projects[0].name)-1);
            app.projects[app.project_count].name[sizeof(app.projects[0].name)-1] = '\0';
            
            snprintf(app.projects[app.project_count].path, sizeof(app.projects[0].path),
                    "%s\\data\\www\\%s\\htdocs", app.path, fd.cFileName);
            
            snprintf(app.projects[app.project_count].url, sizeof(app.projects[0].url),
                    "http://%s.local", fd.cFileName);
            
            app.project_count++;
            if (app.project_count >= MAX_PROJECTS) break;
        }
    }
    while (FindNextFile(hFind, &fd));
    
    FindClose(hFind);
}

/**
 * Load server versions from .env file
 */
static void load_versions(void) {
    char env_path[MAX_PATH_LEN];
    snprintf(env_path, sizeof(env_path), "%s\\.env", app.path);
    
    FILE* f = fopen(env_path, "r");
    if (!f) return;
    
    char line[MAX_LINE];
    int section = 0; // 0=none, 1=php, 2=httpd, 3=mysql
    
    app.php_count = app.httpd_count = app.mysql_count = 0;
    
    // First pass - get active versions
    while (fgets(line, sizeof(line), f)) {
        // Skip comments and empty lines for active version detection
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') 
            continue;
            
        if (strncmp(line, "PHP_SERVER=", 11) == 0) {
            strncpy(app.php, line + 11, sizeof(app.php) - 1);
            app.php[strcspn(app.php, "\r\n")] = 0;
        }
        else if (strncmp(line, "HTTPD_SERVER=", 13) == 0) {
            strncpy(app.httpd, line + 13, sizeof(app.httpd) - 1);
            app.httpd[strcspn(app.httpd, "\r\n")] = 0;
        }
        else if (strncmp(line, "MYSQL_SERVER=", 13) == 0) {
            strncpy(app.mysql, line + 13, sizeof(app.mysql) - 1);
            app.mysql[strcspn(app.mysql, "\r\n")] = 0;
        }
    }
    
    // Rewind file for second pass
    rewind(f);
    
    // Second pass - collect all versions (including commented)
    while (fgets(line, sizeof(line), f)) {
        // More reliable section detection
        if (strstr(line, "Choose PHP Server Image")) {
            section = 1;
            continue;
        }
        else if (strstr(line, "Choose HTTPD Server Image")) {
            section = 2;
            continue;
        }
        else if (strstr(line, "Choose MySQL Server Image")) {
            section = 3;
            continue;
        }
        
        int offset = 0;
        
        // Remove # from beginning of line if present
        if (line[0] == '#') {
            offset = 1;
            while (line[offset] == ' ' || line[offset] == '\t') offset++;
        }
        
        // Process PHP versions
        if (section == 1 && strstr(line + offset, "PHP_SERVER=")) {
            char* value_ptr = strstr(line + offset, "PHP_SERVER=") + 11;
            char value[50] = {0};
            strncpy(value, value_ptr, sizeof(value) - 1);
            value[strcspn(value, "\r\n")] = 0;
            
            // Skip empty values
            if (value[0] == 0) continue;
            
            // Check if this version is already in our list
            int found = 0;
            for (int i = 0; i < app.php_count; i++) {
                if (strcmp(app.php_versions[i], value) == 0) {
                    found = 1;
                    break;
                }
            }
            
            // Add to list if not found and we have space
            if (!found && app.php_count < MAX_VERSIONS) {
                strncpy(app.php_versions[app.php_count++], value, 50);
            }
        }
        // Process HTTPD versions
        else if (section == 2 && strstr(line + offset, "HTTPD_SERVER=")) {
            char* value_ptr = strstr(line + offset, "HTTPD_SERVER=") + 13;
            char value[50] = {0};
            strncpy(value, value_ptr, sizeof(value) - 1);
            value[strcspn(value, "\r\n")] = 0;
            
            // Skip empty values
            if (value[0] == 0) continue;
            
            // Check if this version is already in our list
            int found = 0;
            for (int i = 0; i < app.httpd_count; i++) {
                if (strcmp(app.httpd_versions[i], value) == 0) {
                    found = 1;
                    break;
                }
            }
            
            // Add to list if not found and we have space
            if (!found && app.httpd_count < MAX_VERSIONS) {
                strncpy(app.httpd_versions[app.httpd_count++], value, 50);
            }
        }
        // Process MySQL versions
        else if (section == 3 && strstr(line + offset, "MYSQL_SERVER=")) {
            char* value_ptr = strstr(line + offset, "MYSQL_SERVER=") + 13;
            char value[50] = {0};
            strncpy(value, value_ptr, sizeof(value) - 1);
            value[strcspn(value, "\r\n")] = 0;
            
            // Skip empty values
            if (value[0] == 0) continue;
            
            // Check if this version is already in our list
            int found = 0;
            for (int i = 0; i < app.mysql_count; i++) {
                if (strcmp(app.mysql_versions[i], value) == 0) {
                    found = 1;
                    break;
                }
            }
            
            // Add to list if not found and we have space
            if (!found && app.mysql_count < MAX_VERSIONS) {
                strncpy(app.mysql_versions[app.mysql_count++], value, 50);
            }
        }
    }
    
    fclose(f);
}

/**
 * Set server version
 */
static void set_version(const char* type, const char* version) {
    char env_path[MAX_PATH_LEN];
    char tmp_path[MAX_PATH_LEN];
    snprintf(env_path, sizeof(env_path), "%s\\.env", app.path);
    snprintf(tmp_path, sizeof(tmp_path), "%s\\.env.tmp", app.path);
    
    FILE* in = fopen(env_path, "r");
    FILE* out = fopen(tmp_path, "w");
    if (!in || !out) {
        if (in) fclose(in);
        if (out) fclose(out);
        return;
    }
    
    char line[MAX_LINE];
    char type_prefix[50];
    snprintf(type_prefix, sizeof(type_prefix), "%s=", type);
    
    // Update version in app state
    if (strcmp(type, "PHP_SERVER") == 0)
        strncpy(app.php, version, sizeof(app.php)-1);
    else if (strcmp(type, "HTTPD_SERVER") == 0)
        strncpy(app.httpd, version, sizeof(app.httpd)-1);
    else if (strcmp(type, "MYSQL_SERVER") == 0)
        strncpy(app.mysql, version, sizeof(app.mysql)-1);
    
    while (fgets(line, sizeof(line), in)) {
        if (strncmp(line, type_prefix, strlen(type_prefix)) == 0) {
            fprintf(out, "%s%s\n", type_prefix, version);
            
            // Comment out other versions
            while (fgets(line, sizeof(line), in)) {
                if (line[0] == '\n' || !strstr(line, type_prefix)) {
                    fputs(line, out);
                    break;
                }
                fprintf(out, "#%s", line);
            }
        } else {
            fputs(line, out);
        }
    }
    
    fclose(in);
    fclose(out);
    
    remove(env_path);
    rename(tmp_path, env_path);
    
    // Update the interface
    update_tray();
    update_menu_status(); // Use efficient update instead of recreating menus
    
    // Ask user if they want to restart services
    if (MessageBox(NULL, "Version changed. Do you want to restart Devilbox to apply changes?", 
                  "Restart Required", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        execute_cmd("docker-compose stop && docker-compose rm -f && docker-compose up -d", FALSE);
        refresh_app_state(TRUE);
    }
}

/**
 * Update tray icon tooltip
 */
static void update_tray(void) {
    char tooltip[128];
    snprintf(tooltip, sizeof(tooltip), 
        "Devilbox Manager\nStatus: %s\nPHP: %s\nWeb: %s\nDB: %s",
        app.status == STATUS_RUNNING ? "Running" : 
        app.status == STATUS_STOPPED ? "Stopped" : "Unknown",
        app.php, app.httpd, app.mysql);
    strncpy(app.nid.szTip, tooltip, sizeof(app.nid.szTip) - 1);
    Shell_NotifyIcon(NIM_MODIFY, &app.nid);
}

/**
 * Check if path is valid Devilbox installation
 */
static BOOL is_valid_path(const char* path) {
    char check_path[MAX_PATH_LEN];
    
    snprintf(check_path, sizeof(check_path), "%s\\.env", path);
    if (GetFileAttributes(check_path) == INVALID_FILE_ATTRIBUTES)
        return FALSE;
        
    snprintf(check_path, sizeof(check_path), "%s\\docker-compose.yml", path);
    if (GetFileAttributes(check_path) == INVALID_FILE_ATTRIBUTES)
        return FALSE;
        
    return TRUE;
}

/*******************************************************************************
 * Server Control Functions
 *******************************************************************************/

/**
 * Check server status more efficiently
 */
static ServerStatus check_server_status(void) {
    STARTUPINFO si = {0};
    PROCESS_INFORMATION pi = {0};
    char cmd[512];
    
    si.cb = sizeof(si);
    
    // Change directory to the Devilbox path
    _chdir(app.path);
    
    // Check for running containers
    snprintf(cmd, sizeof(cmd), "cmd.exe /c cd %s && docker-compose ps -q 2>nul", app.path);
    
    SECURITY_ATTRIBUTES sa = {0};
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    
    HANDLE hReadPipe, hWritePipe;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        return STATUS_UNKNOWN;
    }
    
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    
    if (CreateProcess(NULL, cmd, NULL, NULL, TRUE, 
                     CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hWritePipe);
        
        // Read output
        char buffer[1024] = {0};
        DWORD bytesRead;
        
        if (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
            CloseHandle(hReadPipe);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            
            // If we got any output, some containers are running
            return strlen(buffer) > 0 ? STATUS_RUNNING : STATUS_STOPPED;
        }
        
        CloseHandle(hReadPipe);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
    }
    
    return STATUS_UNKNOWN;
}

/**
 * Execute command in Devilbox directory 
 * Added wait parameter to control whether we show visible console
 */
static void execute_cmd(const char* cmd, BOOL wait) {
    STARTUPINFO si = {0};
    PROCESS_INFORMATION pi = {0};
    char full_cmd[512];
    
    si.cb = sizeof(si);
    
    // Change directory to the Devilbox path
    _chdir(app.path);
    
    // Choose appropriate command based on wait parameter
    if (wait) {
        // Execute with hidden window and wait for completion
        snprintf(full_cmd, sizeof(full_cmd), "cmd.exe /c cd %s && %s", app.path, cmd);
        
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        
        if (CreateProcess(NULL, full_cmd, NULL, NULL, FALSE, 
                         CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
            // Wait for the process to complete
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    } else {
        // Execute with visible window and don't wait
        snprintf(full_cmd, sizeof(full_cmd), "cmd.exe /k cd %s && %s", app.path, cmd);
        
        if (CreateProcess(NULL, full_cmd, NULL, NULL, FALSE, 
                         CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }
}

/*******************************************************************************
 * Log Viewing Functions
 *******************************************************************************/

/**
 * Display PHP error logs
 */
static void show_php_logs(void) {
    // Check if dialog is already open
    if (logs_dialog.hDlg && IsWindow(logs_dialog.hDlg)) {
        SetForegroundWindow(logs_dialog.hDlg);
        return;
    }
    
    // Reset filter state
    logs_dialog.filtering = FALSE;
    
    // Extract PHP version number from the PHP_SERVER string (e.g., "php-7.4" -> "7.4")
    char version[10] = {0};
    const char* ver_start = NULL;
    
    // Handle different PHP version string formats
    if (strstr(app.php, "php-")) {
        ver_start = strstr(app.php, "php-") + 4;
    } else {
        // If format is different, try to extract numbers
        for (int i = 0, j = 0; app.php[i] && j < 9; i++) {
            if ((app.php[i] >= '0' && app.php[i] <= '9') || app.php[i] == '.') {
                version[j++] = app.php[i];
            }
        }
        ver_start = version;
    }
    
    // Format the log path correctly
    snprintf(logs_dialog.log_path, sizeof(logs_dialog.log_path), 
             "%s\\log\\php-fpm-%s\\php-fpm.error", app.path, ver_start);
    
    // Check if file exists
    if (GetFileAttributes(logs_dialog.log_path) == INVALID_FILE_ATTRIBUTES) {
        // Try alternative path format if the first one doesn't exist
        snprintf(logs_dialog.log_path, sizeof(logs_dialog.log_path), 
                "%s\\log\\php-%s\\error.log", app.path, app.php);
        
        if (GetFileAttributes(logs_dialog.log_path) == INVALID_FILE_ATTRIBUTES) {
            char message[512];
            snprintf(message, sizeof(message), 
                    "PHP error log file not found at:\n%s\n\nDevilbox may not have generated log files yet.", 
                   logs_dialog.log_path);
            MessageBox(NULL, message, "Error", MB_ICONWARNING);
            return;
        }
    }
    
    // Register dialog class
    WNDCLASSEX wcDialog = {0};
    wcDialog.cbSize = sizeof(WNDCLASSEX);
    wcDialog.lpfnWndProc = LogsDialogProc;
    wcDialog.hInstance = GetModuleHandle(NULL);
    wcDialog.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcDialog.lpszClassName = "DevilboxLogsDialog";
    RegisterClassEx(&wcDialog);
    
    // Create dialog window
    logs_dialog.hDlg = CreateWindowEx(
        WS_EX_DLGMODALFRAME,
        "DevilboxLogsDialog",
        "PHP Error Logs",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        100, 100, 900, 700,
        NULL, NULL, GetModuleHandle(NULL), NULL
    );
    
    if (!logs_dialog.hDlg) {
        MessageBox(NULL, "Failed to create log viewer window.", "Error", MB_ICONERROR);
        return;
    }
    
    // Create text area for logs
    logs_dialog.hEdit = CreateWindowEx(
        WS_EX_CLIENTEDGE,
        "EDIT",
        "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | 
        ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY | ES_WANTRETURN,
        10, 70, 870, 540,
        logs_dialog.hDlg, (HMENU)ID_LOGS_EDIT, GetModuleHandle(NULL), NULL
    );
    
    // Set a monospaced font for better log readability
    HFONT hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, 
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, 
                          DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    SendMessage(logs_dialog.hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // Add date filter controls
    // Start date label
    CreateWindow(
        "STATIC",
        "Start Date:",
        WS_CHILD | WS_VISIBLE,
        10, 10, 80, 20,
        logs_dialog.hDlg, NULL, GetModuleHandle(NULL), NULL
    );
    
    // Start date picker
    logs_dialog.hDateStart = CreateWindowEx(
        0,
        DATETIMEPICK_CLASS,
        "",
        WS_CHILD | WS_VISIBLE | DTS_SHORTDATEFORMAT,
        90, 10, 120, 24,
        logs_dialog.hDlg, (HMENU)ID_DATE_FILTER_START, GetModuleHandle(NULL), NULL
    );
    
    // Start time label
    CreateWindow(
        "STATIC",
        "Start Time:",
        WS_CHILD | WS_VISIBLE,
        220, 10, 80, 20,
        logs_dialog.hDlg, NULL, GetModuleHandle(NULL), NULL
    );
    
    // Start time input
    logs_dialog.hTimeStart = CreateWindowEx(
        WS_EX_CLIENTEDGE,
        "EDIT",
        "00:00:00",
        WS_CHILD | WS_VISIBLE,
        300, 10, 80, 24,
        logs_dialog.hDlg, (HMENU)ID_TIME_FILTER_START, GetModuleHandle(NULL), NULL
    );
    
    // End date label
    CreateWindow(
        "STATIC",
        "End Date:",
        WS_CHILD | WS_VISIBLE,
        400, 10, 80, 20,
        logs_dialog.hDlg, NULL, GetModuleHandle(NULL), NULL
    );
    
    // End date picker
    logs_dialog.hDateEnd = CreateWindowEx(
        0,
        DATETIMEPICK_CLASS,
        "",
        WS_CHILD | WS_VISIBLE | DTS_SHORTDATEFORMAT,
        480, 10, 120, 24,
        logs_dialog.hDlg, (HMENU)ID_DATE_FILTER_END, GetModuleHandle(NULL), NULL
    );
    
    // End time label
    CreateWindow(
        "STATIC",
        "End Time:",
        WS_CHILD | WS_VISIBLE,
        610, 10, 80, 20,
        logs_dialog.hDlg, NULL, GetModuleHandle(NULL), NULL
    );
    
    // End time input
    logs_dialog.hTimeEnd = CreateWindowEx(
        WS_EX_CLIENTEDGE,
        "EDIT",
        "23:59:59",
        WS_CHILD | WS_VISIBLE,
        690, 10, 80, 24,
        logs_dialog.hDlg, (HMENU)ID_TIME_FILTER_END, GetModuleHandle(NULL), NULL
    );
    
    // Filter instructions label
    CreateWindow(
        "STATIC",
        "Filter logs by date/time range in the format: YYYY-MM-DD and HH:MM:SS",
        WS_CHILD | WS_VISIBLE,
        10, 40, 500, 20,
        logs_dialog.hDlg, NULL, GetModuleHandle(NULL), NULL
    );
    
    // Apply filter button
    CreateWindow(
        "BUTTON",
        "Apply Filter",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        790, 10, 100, 24,
        logs_dialog.hDlg, (HMENU)ID_APPLY_FILTER_BTN, GetModuleHandle(NULL), NULL
    );
    
    // Reset filter button
    CreateWindow(
        "BUTTON",
        "Reset Filter",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        790, 40, 100, 24,
        logs_dialog.hDlg, (HMENU)ID_RESET_FILTER_BTN, GetModuleHandle(NULL), NULL
    );
    
    // Action Buttons
    // Refresh button
    CreateWindow(
        "BUTTON",
        "Refresh",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        10, 620, 100, 30,
        logs_dialog.hDlg, (HMENU)ID_REFRESH_BTN, GetModuleHandle(NULL), NULL
    );
    
    // Clear logs button
    CreateWindow(
        "BUTTON",
        "Clear Logs",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        120, 620, 100, 30,
        logs_dialog.hDlg, (HMENU)ID_CLEAR_BTN, GetModuleHandle(NULL), NULL
    );
    
    // Close button
    CreateWindow(
        "BUTTON",
        "Close",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        230, 620, 100, 30,
        logs_dialog.hDlg, (HMENU)ID_CLOSE_BTN, GetModuleHandle(NULL), NULL
    );
    
    // Set default dates to today
    SYSTEMTIME today;
    GetLocalTime(&today);
    SendMessage(logs_dialog.hDateStart, DTM_SETSYSTEMTIME, 0, (LPARAM)&today);
    SendMessage(logs_dialog.hDateEnd, DTM_SETSYSTEMTIME, 0, (LPARAM)&today);
    
    // Load log content
    refresh_log_content();
    
    // Set icon
    HICON hIcon = LoadIcon(NULL, IDI_APPLICATION);
    SendMessage(logs_dialog.hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    SendMessage(logs_dialog.hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    
    // Show window
    ShowWindow(logs_dialog.hDlg, SW_SHOW);
    UpdateWindow(logs_dialog.hDlg);
}

/**
 * Function to refresh log content with proper formatting
 */
static void refresh_log_content(void) {
    if (!logs_dialog.hEdit || !IsWindow(logs_dialog.hEdit))
        return;
        
    // Clear current text
    SetWindowText(logs_dialog.hEdit, "");
    
    // Read log file
    FILE* f = fopen(logs_dialog.log_path, "r");
    if (!f) {
        SetWindowText(logs_dialog.hEdit, "Failed to open log file.");
        return;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (size <= 0) {
        SetWindowText(logs_dialog.hEdit, "Log file is empty.");
        fclose(f);
        return;
    }
    
    // Allocate buffer with room for conversion
    char* buffer = (char*)malloc(size + 1);
    if (!buffer) {
        SetWindowText(logs_dialog.hEdit, "Memory allocation failed.");
        fclose(f);
        return;
    }
    
    size_t read = fread(buffer, 1, size, f);
    buffer[read] = '\0';
    fclose(f);
    
    if (logs_dialog.filtering) {
        // Apply date/time filtering
        char* filtered = (char*)malloc(size + 1);
        if (!filtered) {
            SetWindowText(logs_dialog.hEdit, "Memory allocation failed for filtering.");
            free(buffer);
            return;
        }
        
        char* filtered_ptr = filtered;
        filtered[0] = '\0';  // Initialize as empty string
        
        // Process line by line
        char* line = strtok(buffer, "\n");
        while (line) {
            // Parse date from log line
            SYSTEMTIME lineDate;
            if (parse_log_date(line, &lineDate)) {
                // Check if date is in range
                FILETIME ftLine, ftStart, ftEnd;
                SystemTimeToFileTime(&lineDate, &ftLine);
                
                // Combine date and time for start
                SYSTEMTIME stStart = logs_dialog.filterStartDate;
                stStart.wHour = logs_dialog.filterStartTime.wHour;
                stStart.wMinute = logs_dialog.filterStartTime.wMinute;
                stStart.wSecond = logs_dialog.filterStartTime.wSecond;
                SystemTimeToFileTime(&stStart, &ftStart);
                
                // Combine date and time for end
                SYSTEMTIME stEnd = logs_dialog.filterEndDate;
                stEnd.wHour = logs_dialog.filterEndTime.wHour;
                stEnd.wMinute = logs_dialog.filterEndTime.wMinute;
                stEnd.wSecond = logs_dialog.filterEndTime.wSecond;
                SystemTimeToFileTime(&stEnd, &ftEnd);
                
                // Compare dates
                if (CompareFileTime(&ftLine, &ftStart) >= 0 && 
                    CompareFileTime(&ftLine, &ftEnd) <= 0) {
                    // Line is in date range
                    size_t lineLen = strlen(line);
                    strcpy(filtered_ptr, line);
                    filtered_ptr += lineLen;
                    strcpy(filtered_ptr, "\r\n");
                    filtered_ptr += 2;
                }
            }
            
            line = strtok(NULL, "\n");
        }
        
        *filtered_ptr = '\0';  // Ensure null termination
        
        if (strlen(filtered) > 0) {
            SetWindowText(logs_dialog.hEdit, filtered);
        } else {
            SetWindowText(logs_dialog.hEdit, "No log entries found in the specified date range.");
        }
        
        free(filtered);
    } else {
        // Process the buffer to ensure proper line breaks without filtering
        char* processed = (char*)malloc(size * 2 + 1); // Allocate extra space for potential \r insertions
        if (processed) {
            size_t j = 0;
            for (size_t i = 0; i < read; i++) {
                // Ensure proper Windows line endings (\r\n)
                if (buffer[i] == '\n' && (i == 0 || buffer[i-1] != '\r')) {
                    processed[j++] = '\r';
                    processed[j++] = '\n';
                } else {
                    processed[j++] = buffer[i];
                }
            }
            processed[j] = '\0';
            
            // Set the properly formatted text
            SetWindowText(logs_dialog.hEdit, processed);
            free(processed);
        } else {
            // Fallback if memory allocation failed
            SetWindowText(logs_dialog.hEdit, buffer);
        }
    }
    
    // Scroll to the end to show most recent logs
    SendMessage(logs_dialog.hEdit, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    SendMessage(logs_dialog.hEdit, EM_SCROLLCARET, 0, 0);
    
    free(buffer);
}

/**
 * Parse date from a log line
 */
static BOOL parse_log_date(const char *log_line, SYSTEMTIME *datetime) {
    // Example log format: [25-Feb-2025 07:16:59 UTC] PHP Warning: ...
    int day, hour, minute, second;
    char month[4];
    int year;
    
    if (sscanf(log_line, "[%d-%3[^-]-%d %d:%d:%d", &day, month, &year, &hour, &minute, &second) != 6) {
        return FALSE;
    }
    
    // Convert month name to month number
    int month_num = 0;
    if (strcmp(month, "Jan") == 0) month_num = 1;
    else if (strcmp(month, "Feb") == 0) month_num = 2;
    else if (strcmp(month, "Mar") == 0) month_num = 3;
    else if (strcmp(month, "Apr") == 0) month_num = 4;
    else if (strcmp(month, "May") == 0) month_num = 5;
    else if (strcmp(month, "Jun") == 0) month_num = 6;
    else if (strcmp(month, "Jul") == 0) month_num = 7;
    else if (strcmp(month, "Aug") == 0) month_num = 8;
    else if (strcmp(month, "Sep") == 0) month_num = 9;
    else if (strcmp(month, "Oct") == 0) month_num = 10;
    else if (strcmp(month, "Nov") == 0) month_num = 11;
    else if (strcmp(month, "Dec") == 0) month_num = 12;
    
    if (month_num == 0) return FALSE;
    
    // Fill the SYSTEMTIME structure
    ZeroMemory(datetime, sizeof(SYSTEMTIME));
    datetime->wYear = year;
    datetime->wMonth = month_num;
    datetime->wDay = day;
    datetime->wHour = hour;
    datetime->wMinute = minute;
    datetime->wSecond = second;
    
    return TRUE;
}

/**
 * Function to clear the log file
 */
static void clear_log_file(void) {
    // Confirm with the user
    if (MessageBox(logs_dialog.hDlg, "Are you sure you want to clear the log file?\nThis action cannot be undone.", 
                  "Clear Log File", MB_YESNO | MB_ICONWARNING) != IDYES) {
        return;
    }
    
    // Clear the file
    FILE* f = fopen(logs_dialog.log_path, "w");
    if (f) {
        fclose(f);
        SetWindowText(logs_dialog.hEdit, "Log file has been cleared.");
    } else {
        MessageBox(logs_dialog.hDlg, "Failed to clear log file. The file may be in use or you may not have permission.", 
                  "Error", MB_ICONERROR);
    }
}

/**
 * Function to apply date/time filter
 */
static void apply_log_filter(void) {
    // Get the start date
    if (SendMessage(logs_dialog.hDateStart, DTM_GETSYSTEMTIME, 0, (LPARAM)&logs_dialog.filterStartDate) != GDT_VALID) {
        MessageBox(logs_dialog.hDlg, "Invalid start date selected.", "Error", MB_ICONERROR);
        return;
    }
    
    // Get the end date
    if (SendMessage(logs_dialog.hDateEnd, DTM_GETSYSTEMTIME, 0, (LPARAM)&logs_dialog.filterEndDate) != GDT_VALID) {
        MessageBox(logs_dialog.hDlg, "Invalid end date selected.", "Error", MB_ICONERROR);
        return;
    }
    
    // Get the start time
    char startTimeStr[20] = {0};
    GetWindowText(logs_dialog.hTimeStart, startTimeStr, sizeof(startTimeStr));
    int startHour, startMin, startSec;
    if (sscanf(startTimeStr, "%d:%d:%d", &startHour, &startMin, &startSec) != 3) {
        MessageBox(logs_dialog.hDlg, "Invalid start time format. Use HH:MM:SS.", "Error", MB_ICONERROR);
        return;
    }
    
    logs_dialog.filterStartTime.wHour = startHour;
    logs_dialog.filterStartTime.wMinute = startMin;
    logs_dialog.filterStartTime.wSecond = startSec;
    
    // Get the end time
    char endTimeStr[20] = {0};
    GetWindowText(logs_dialog.hTimeEnd, endTimeStr, sizeof(endTimeStr));
    int endHour, endMin, endSec;
    if (sscanf(endTimeStr, "%d:%d:%d", &endHour, &endMin, &endSec) != 3) {
        MessageBox(logs_dialog.hDlg, "Invalid end time format. Use HH:MM:SS.", "Error", MB_ICONERROR);
        return;
    }
    
    logs_dialog.filterEndTime.wHour = endHour;
    logs_dialog.filterEndTime.wMinute = endMin;
    logs_dialog.filterEndTime.wSecond = endSec;
    
    // Set filtering flag and refresh content
    logs_dialog.filtering = TRUE;
    refresh_log_content();
}

/**
 * Function to reset log filter
 */
static void reset_log_filter(void) {
    logs_dialog.filtering = FALSE;
    refresh_log_content();
}

/**
 * Dialog procedure for logs window
 */
static LRESULT CALLBACK LogsDialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case ID_REFRESH_BTN:  // Refresh button
                    refresh_log_content();
                    break;
                    
                case ID_CLEAR_BTN:    // Clear logs button
                    clear_log_file();
                    break;
                    
                case ID_CLOSE_BTN:    // Close button
                    DestroyWindow(hwnd);
                    break;
                    
                case ID_APPLY_FILTER_BTN:  // Apply filter button
                    apply_log_filter();
                    break;
                    
                case ID_RESET_FILTER_BTN:  // Reset filter button
                    reset_log_filter();
                    break;
            }
            break;
            
        case WM_SIZE:
            if (logs_dialog.hEdit) {
                RECT rcClient;
                GetClientRect(hwnd, &rcClient);
                
                // Resize edit control to fit window
                SetWindowPos(logs_dialog.hEdit, NULL, 
                           10, 70, 
                           rcClient.right - 20, rcClient.bottom - 160,
                           SWP_NOZORDER);
                           
                // Reposition buttons at the bottom
                HWND hRefresh = GetDlgItem(hwnd, ID_REFRESH_BTN);
                HWND hClear = GetDlgItem(hwnd, ID_CLEAR_BTN);
                HWND hClose = GetDlgItem(hwnd, ID_CLOSE_BTN);
                
                if (hRefresh) {
                    SetWindowPos(hRefresh, NULL,
                               10, rcClient.bottom - 40,
                               100, 30,
                               SWP_NOZORDER);
                }
                
                if (hClear) {
                    SetWindowPos(hClear, NULL,
                               120, rcClient.bottom - 40,
                               100, 30,
                               SWP_NOZORDER);
                }
                
                if (hClose) {
                    SetWindowPos(hClose, NULL,
                               230, rcClient.bottom - 40,
                               100, 30,
                               SWP_NOZORDER);
                }
            }
            break;
            
        case WM_CLOSE:
            DestroyWindow(hwnd);
            break;
            
        case WM_DESTROY:
            logs_dialog.hDlg = NULL;
            logs_dialog.hEdit = NULL;
            logs_dialog.hDateStart = NULL;
            logs_dialog.hDateEnd = NULL;
            logs_dialog.hTimeStart = NULL;
            logs_dialog.hTimeEnd = NULL;
            break;
            
        default:
            return DefWindowProc(hwnd, msg, wp, lp);
    }
    return 0;
}

/*******************************************************************************
 * Entry Point
 *******************************************************************************/
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
                  LPSTR lpCmdLine, int nCmdShow) {
    
    // Initialize COM
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    
    // Initialize application
    if (!init_app(hInstance)) {
        CoUninitialize();
        return 0;
    }
    
    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    CoUninitialize();
    return msg.wParam;
}
