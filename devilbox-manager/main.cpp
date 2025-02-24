#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <memory>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <map>
#include <regex>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

class DevilboxManager {
    static constexpr const char* APP_NAME = "DevilboxManager";
    static constexpr const char* REG_KEY = "Software\\DevilboxManager";
    enum { WM_TRAYICON = WM_USER + 1 };
    enum { 
        IDM_START = 1001, IDM_STOP, IDM_RESTART, 
        IDM_HOSTS, IDM_ENV, IDM_EXIT, IDM_WWW, IDM_CHANGEDIR,
        IDM_CONTROL_PANEL,
        IDM_PHP_VERSION_BASE = 2000,
        IDM_HTTPD_VERSION_BASE = 3000,
        IDM_MYSQL_VERSION_BASE = 4000,
        IDM_WEBSITES_SUBMENU = 1500,
        IDM_WEBSITE_OPEN = 5000,    // Open website in browser
        IDM_WEBSITE_FOLDER = 5500,  // Open website folder
        IDM_WEBSITES_START = 6000   // Starting ID for dynamic website menu items
    };

    struct Website {
        std::string name;
        std::string path;
        std::string url;
    };

    struct ServerVersions {
        std::string php;
        std::string httpd;
        std::string mysql;
    };

    // Predefined version options
    const std::vector<std::string> PHP_VERSIONS = {
        "5.6", "7.0", "7.1", "7.2", "7.3", "7.4", "8.0", "8.1", "8.2"
    };

    const std::vector<std::string> HTTPD_VERSIONS = {
        "apache-2.4", "nginx-stable", "nginx-mainline"
    };

    const std::vector<std::string> MYSQL_VERSIONS = {
        "mysql-5.7", "mysql-8.0", "mariadb-10.5", "mariadb-10.6"
    };

private:
    HWND hwnd = nullptr;
    HMENU menu = nullptr;
    HMENU websitesMenu = nullptr;
    HMENU phpVersionMenu = nullptr;
    HMENU httpdVersionMenu = nullptr;
    HMENU mysqlVersionMenu = nullptr;
    NOTIFYICONDATA nid = {0};
    std::string devilboxPath;
    std::vector<Website> websites;
    ServerVersions versions = {"7.4", "nginx-stable", "mysql-8.0"}; // Default versions
    std::unique_ptr<void, decltype(&CloseHandle)> m_mutex{nullptr, CloseHandle};

public:
    static DevilboxManager& instance() {
        static DevilboxManager inst;
        return inst;
    }

    bool init(HINSTANCE hInst) {
        m_mutex.reset(CreateMutex(nullptr, TRUE, "DevilboxManagerSingleInstance"));
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            if (HWND hw = FindWindow(APP_NAME, nullptr)) 
                SendMessage(hw, WM_USER, 0, 0);
            return false;
        }

        return (loadPath() || selectPath()) && createWindow(hInst) && initUI();
    }

    void updateEnvFile(const std::string& section, const std::string& version) {
        std::string envPath = devilboxPath + "\\.env";
        std::ifstream input(envPath);
        std::stringstream buffer;
        std::string line;
        bool updated = false;

        // Read and modify the file
        while (std::getline(input, line)) {
            if (section == "PHP" && line.substr(0, 11) == "PHP_SERVER=") {
                line = "PHP_SERVER=" + version;
                updated = true;
                versions.php = version;
            }
            else if (section == "HTTPD" && line.substr(0, 13) == "HTTPD_SERVER=") {
                line = "HTTPD_SERVER=" + version;
                updated = true;
                versions.httpd = version;
            }
            else if (section == "MYSQL" && line.substr(0, 13) == "MYSQL_SERVER=") {
                line = "MYSQL_SERVER=" + version;
                updated = true;
                versions.mysql = version;
            }
            buffer << line << "\n";
        }

        // Write back to file
        std::ofstream output(envPath);
        output << buffer.str();

        // Update tray tooltip and menu
        updateTrayTooltip();
        updateMenu();
    }

    void executeCmd(const std::string& cmd) {
        SetCurrentDirectory(devilboxPath.c_str());
        system(cmd.c_str());
    }

    void updateMenu() {
        if (!menu) return;

        // Modify version selection submenus to mark current versions
        for (size_t i = 0; i < PHP_VERSIONS.size(); ++i) {
            MENUITEMINFO mii = {sizeof(MENUITEMINFO)};
            mii.fMask = MIIM_STATE;
            mii.fState = (PHP_VERSIONS[i] == versions.php) ? MFS_CHECKED : MFS_UNCHECKED;
            SetMenuItemInfo(phpVersionMenu, IDM_PHP_VERSION_BASE + i, FALSE, &mii);
        }

        for (size_t i = 0; i < HTTPD_VERSIONS.size(); ++i) {
            MENUITEMINFO mii = {sizeof(MENUITEMINFO)};
            mii.fMask = MIIM_STATE;
            mii.fState = (HTTPD_VERSIONS[i] == versions.httpd) ? MFS_CHECKED : MFS_UNCHECKED;
            SetMenuItemInfo(httpdVersionMenu, IDM_HTTPD_VERSION_BASE + i, FALSE, &mii);
        }

        for (size_t i = 0; i < MYSQL_VERSIONS.size(); ++i) {
            MENUITEMINFO mii = {sizeof(MENUITEMINFO)};
            mii.fMask = MIIM_STATE;
            mii.fState = (MYSQL_VERSIONS[i] == versions.mysql) ? MFS_CHECKED : MFS_UNCHECKED;
            SetMenuItemInfo(mysqlVersionMenu, IDM_MYSQL_VERSION_BASE + i, FALSE, &mii);
        }

        // Update first menu item text with current versions
        std::string versionMenuText = "PHP:" + versions.php + " | Web:" + versions.httpd + " | DB:" + versions.mysql;
        MENUITEMINFO mii = {sizeof(MENUITEMINFO)};
        mii.fMask = MIIM_STRING;
        mii.dwTypeData = (LPSTR)versionMenuText.c_str();
        SetMenuItemInfo(menu, 0, TRUE, &mii);

        // Force menu redraw
        DrawMenuBar(hwnd);
    }

    const std::string& path() const { 
        return devilboxPath; 
    }
    bool loadPath() {
        HKEY hKey;
        if (RegOpenKeyEx(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS) 
            return false;
        
        char buffer[MAX_PATH];
        DWORD size = sizeof(buffer);
        bool success = RegQueryValueEx(hKey, "Path", nullptr, nullptr, (BYTE*)buffer, &size) == ERROR_SUCCESS;
        RegCloseKey(hKey);
        
        if (success) {
            devilboxPath = buffer;
            if (isValidPath(devilboxPath)) {
                updateServerVersions();
                return true;
            }
        }
        return false;
    }

    bool savePath() const {
        HKEY hKey;
        if (RegCreateKeyEx(HKEY_CURRENT_USER, REG_KEY, 0, nullptr, REG_OPTION_NON_VOLATILE, 
            KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS) return false;
        
        bool success = RegSetValueEx(hKey, "Path", 0, REG_SZ, (BYTE*)devilboxPath.c_str(), 
            devilboxPath.length() + 1) == ERROR_SUCCESS;
        RegCloseKey(hKey);
        return success;
    }

    bool selectPath() {
        BROWSEINFO bi = {0};
        bi.hwndOwner = hwnd;
        bi.lpszTitle = "Select Devilbox Directory";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        
        if (LPITEMIDLIST pidl = SHBrowseForFolder(&bi)) {
            char buffer[MAX_PATH];
            if (SHGetPathFromIDList(pidl, buffer)) {
                std::string path = buffer;
                if (isValidPath(path)) {
                    devilboxPath = path;
                    updateServerVersions();
                    return savePath();
                }
                MessageBox(hwnd, "Selected directory is not a valid Devilbox installation.", 
                    "Error", MB_ICONERROR);
            }
            CoTaskMemFree(pidl);
        }
        return false;
    }

    bool isValidPath(const std::string& path) const {
        return GetFileAttributes((path + "\\.env").c_str()) != INVALID_FILE_ATTRIBUTES &&
               GetFileAttributes((path + "\\docker-compose.yml").c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    bool createWindow(HINSTANCE hInst) {
        WNDCLASSEX wc = {sizeof(WNDCLASSEX)};
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hInst;
        wc.lpszClassName = APP_NAME;
        
        if (!RegisterClassEx(&wc)) return false;

        hwnd = CreateWindowEx(0, APP_NAME, "Devilbox Manager", WS_OVERLAPPED,
            CW_USEDEFAULT, CW_USEDEFAULT, 1, 1, nullptr, nullptr, hInst, nullptr);
        return hwnd != nullptr;
    }

    bool initUI() {
        // Create tray icon menu
        createMenu();
        
        // Create system tray icon
        nid = {sizeof(NOTIFYICONDATA)};
        nid.hWnd = hwnd;
        nid.uID = 1;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = WM_TRAYICON;
        
        // Load tray icon
        HMODULE hIcon = LoadLibrary("imageres.dll");
        nid.hIcon = (HICON)LoadImage(hIcon, MAKEINTRESOURCE(109), 
            IMAGE_ICON, 16, 16, LR_SHARED);
        FreeLibrary(hIcon);

        // Set initial tooltip with version info
        std::string tooltip = "Devilbox Manager\n"
            "PHP: " + versions.php + "\n"
            "Web: " + versions.httpd + "\n"
            "DB: " + versions.mysql;
        strncpy(nid.szTip, tooltip.c_str(), sizeof(nid.szTip) - 1);
        
        return Shell_NotifyIcon(NIM_ADD, &nid);
    }

    void createVersionSubmenus() {
        // PHP Versions Submenu
        phpVersionMenu = CreatePopupMenu();
        for (size_t i = 0; i < PHP_VERSIONS.size(); ++i) {
            AppendMenu(phpVersionMenu, MF_STRING, 
                IDM_PHP_VERSION_BASE + i, PHP_VERSIONS[i].c_str());
        }

        // HTTPD Versions Submenu
        httpdVersionMenu = CreatePopupMenu();
        for (size_t i = 0; i < HTTPD_VERSIONS.size(); ++i) {
            AppendMenu(httpdVersionMenu, MF_STRING, 
                IDM_HTTPD_VERSION_BASE + i, HTTPD_VERSIONS[i].c_str());
        }

        // MySQL Versions Submenu
        mysqlVersionMenu = CreatePopupMenu();
        for (size_t i = 0; i < MYSQL_VERSIONS.size(); ++i) {
            AppendMenu(mysqlVersionMenu, MF_STRING, 
                IDM_MYSQL_VERSION_BASE + i, MYSQL_VERSIONS[i].c_str());
        }
    }

    void createMenu() {
        menu = CreatePopupMenu();
        
        // Create version submenus first
        createVersionSubmenus();

        // Create a main versions submenu with cascading version selection
        HMENU versionsSubmenu = CreatePopupMenu();
        
        // Add version selection submenus to main versions submenu
        AppendMenu(versionsSubmenu, MF_POPUP, (UINT_PTR)phpVersionMenu, "PHP Version");
        AppendMenu(versionsSubmenu, MF_POPUP, (UINT_PTR)httpdVersionMenu, "Web Server");
        AppendMenu(versionsSubmenu, MF_POPUP, (UINT_PTR)mysqlVersionMenu, "Database");

        // Create Docker container submenu
        HMENU dockerMenu = CreatePopupMenu();
        AppendMenu(dockerMenu, MF_STRING, IDM_START, "Start All Containers");
        AppendMenu(dockerMenu, MF_STRING, IDM_STOP, "Stop All Containers");
        AppendMenu(dockerMenu, MF_STRING, IDM_RESTART, "Restart All Containers");
        
        // Main menu structure with dynamic version text
        std::string versionMenuText = "PHP:" + versions.php + " | Web:" + versions.httpd + " | DB:" + versions.mysql;
        AppendMenu(menu, MF_POPUP, (UINT_PTR)versionsSubmenu, versionMenuText.c_str());
        AppendMenu(menu, MF_POPUP, (UINT_PTR)dockerMenu, "Docker Containers");
        AppendMenu(menu, MF_SEPARATOR, 0, nullptr);

        // Additional utility menus
        AppendMenu(menu, MF_STRING, IDM_CONTROL_PANEL, "Open Control Panel");
        AppendMenu(menu, MF_STRING, IDM_WWW, "Open Projects Folder");
        AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenu(menu, MF_STRING, IDM_HOSTS, "Edit Hosts File");
        AppendMenu(menu, MF_STRING, IDM_ENV, "Edit .env Configuration");
        AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenu(menu, MF_STRING, IDM_CHANGEDIR, "Change Devilbox Directory");
        AppendMenu(menu, MF_STRING, IDM_EXIT, "Exit Application");

        // Initialize websites submenu
        websitesMenu = CreatePopupMenu();
    }

    void showContextMenu(HWND hw) {
        // Update websites menu before showing
        updateWebsitesMenu();
        
        POINT pt;
        GetCursorPos(&pt);
        SetForegroundWindow(hw);
        
        // Add TPM_RECURSE flag to enable nested menus
        TrackPopupMenu(menu, TPM_BOTTOMALIGN | TPM_LEFTALIGN | TPM_RECURSE, 
            pt.x, pt.y, 0, hw, nullptr);
    }

    void updateServerVersions() {
        std::ifstream envFile(devilboxPath + "\\.env");
        std::string line;
        while (std::getline(envFile, line)) {
            if (line.empty() || line[0] == '#') continue;
            
            if (line.substr(0, 11) == "PHP_SERVER=")
                versions.php = line.substr(11);
            else if (line.substr(0, 13) == "HTTPD_SERVER=")
                versions.httpd = line.substr(13);
            else if (line.substr(0, 13) == "MYSQL_SERVER=")
                versions.mysql = line.substr(13);
        }
    }

    void updateTrayTooltip() {
        std::string tooltip = "Devilbox Manager\n"
            "PHP: " + versions.php + "\n"
            "Web: " + versions.httpd + "\n"
            "DB: " + versions.mysql;
        strncpy(nid.szTip, tooltip.c_str(), sizeof(nid.szTip) - 1);
        Shell_NotifyIcon(NIM_MODIFY, &nid);
    }

    std::string readTldSuffix() {
        std::string tldSuffix = "local";
        std::ifstream envFile(devilboxPath + "\\.env");
        std::string line;
        while (std::getline(envFile, line)) {
            if (line.substr(0, 12) == "TLD_SUFFIX=") {
                tldSuffix = line.substr(12);
                break;
            }
        }
        return tldSuffix;
    }

    void discoverWebsites() {
        websites.clear();
        std::string wwwPath = devilboxPath + "\\data\\www";
        std::string searchPath = wwwPath + "\\*";
        std::string tldSuffix = readTldSuffix();

        // Check directory existence
        if (GetFileAttributes(wwwPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            return;
        }

        WIN32_FIND_DATA findFileData;
        HANDLE hFind = FindFirstFile(searchPath.c_str(), &findFileData);
        if (hFind == INVALID_HANDLE_VALUE) {
            return;
        }

        do {
            // Skip system directories
            if (strcmp(findFileData.cFileName, ".") == 0 || 
                strcmp(findFileData.cFileName, "..") == 0) {
                continue;
            }

            // Check if it's a directory
            if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                Website site;
                site.name = findFileData.cFileName;
                site.path = wwwPath + "\\" + site.name;
                site.url = "http://" + site.name + "." + tldSuffix;
                
                // Check if directory contains files
                WIN32_FIND_DATA projectFiles;
                HANDLE hProjectFind = FindFirstFile((site.path + "\\*").c_str(), &projectFiles);
                if (hProjectFind != INVALID_HANDLE_VALUE) {
                    websites.push_back(site);
                    FindClose(hProjectFind);
                }
            }
        } while (FindNextFile(hFind, &findFileData));

        FindClose(hFind);

        // Sort projects alphabetically
        std::sort(websites.begin(), websites.end(), 
            [](const Website& a, const Website& b) { return a.name < b.name; });
    }

    void updateWebsitesMenu() {
        // Clear existing menu items
        while (GetMenuItemCount(websitesMenu) > 0) {
            DeleteMenu(websitesMenu, 0, MF_BYPOSITION);
        }
        
        discoverWebsites();
        
        if (!websites.empty()) {
            for (size_t i = 0; i < websites.size(); ++i) {
                const auto& site = websites[i];
                
                // Create submenu for each website
                HMENU siteMenu = CreatePopupMenu();
                AppendMenu(siteMenu, MF_STRING, IDM_WEBSITE_OPEN + i, "Open in Browser");
                AppendMenu(siteMenu, MF_STRING, IDM_WEBSITE_FOLDER + i, "Open Folder");
                
                // Add website submenu to websites menu
                AppendMenu(websitesMenu, MF_POPUP, (UINT_PTR)siteMenu, site.name.c_str());
            }
        } else {
            AppendMenu(websitesMenu, MF_STRING | MF_DISABLED, 0, "No projects found");
        }

        // Force menu to redraw
        DrawMenuBar(hwnd);
    }

    static LRESULT CALLBACK WndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
        auto& app = instance();
        switch (msg) {
            case WM_TRAYICON:
                switch (lp) {
                    case WM_LBUTTONUP:
                    case WM_RBUTTONUP:
                        app.showContextMenu(hw);
                        break;
                }
                break;

            case WM_COMMAND: {
                int wmId = LOWORD(wp);

                // Version selection handling
                if (wmId >= IDM_PHP_VERSION_BASE && wmId < IDM_PHP_VERSION_BASE + 100) {
                    int index = wmId - IDM_PHP_VERSION_BASE;
                    if (index < app.PHP_VERSIONS.size()) {
                        app.updateEnvFile("PHP", app.PHP_VERSIONS[index]);
                    }
                    return 0;
                }
                else if (wmId >= IDM_HTTPD_VERSION_BASE && wmId < IDM_HTTPD_VERSION_BASE + 100) {
                    int index = wmId - IDM_HTTPD_VERSION_BASE;
                    if (index < app.HTTPD_VERSIONS.size()) {
                        app.updateEnvFile("HTTPD", app.HTTPD_VERSIONS[index]);
                    }
                    return 0;
                }
                else if (wmId >= IDM_MYSQL_VERSION_BASE && wmId < IDM_MYSQL_VERSION_BASE + 100) {
                    int index = wmId - IDM_MYSQL_VERSION_BASE;
                    if (index < app.MYSQL_VERSIONS.size()) {
                        app.updateEnvFile("MYSQL", app.MYSQL_VERSIONS[index]);
                    }
                    return 0;
                }
                
                // Website menu handling
                if (wmId >= IDM_WEBSITE_OPEN && wmId < IDM_WEBSITE_OPEN + 1000) {
                    int index = wmId - IDM_WEBSITE_OPEN;
                    if (index < app.websites.size()) {
                        ShellExecute(nullptr, "open", app.websites[index].url.c_str(), 
                            nullptr, nullptr, SW_SHOW);
                    }
                    return 0;
                }
                else if (wmId >= IDM_WEBSITE_FOLDER && wmId < IDM_WEBSITE_FOLDER + 1000) {
                    int index = wmId - IDM_WEBSITE_FOLDER;
                    if (index < app.websites.size()) {
                        ShellExecute(nullptr, "explore", app.websites[index].path.c_str(), 
                            nullptr, nullptr, SW_SHOW);
                    }
                    return 0;
                }

                // Existing command handling
                switch (wmId) {
                    case IDM_START:    
                        app.executeCmd("docker-compose up -d"); 
                        break;
                    case IDM_STOP:     
                        app.executeCmd("docker-compose stop"); 
                        break;
                    case IDM_RESTART:  
                        app.executeCmd("docker-compose stop && docker-compose rm -f && docker-compose up -d"); 
                        break;
                    case IDM_CONTROL_PANEL:
                        ShellExecute(nullptr, "open", "http://localhost", nullptr, nullptr, SW_SHOW);
                        break;
                    case IDM_HOSTS:
                        ShellExecute(nullptr, "open", "C:\\Windows\\System32\\drivers\\etc\\hosts", 
                            nullptr, nullptr, SW_SHOW);
                        break;
                    case IDM_ENV:
                        ShellExecute(nullptr, "open", (app.path() + "\\.env").c_str(), 
                            nullptr, nullptr, SW_SHOW);
                        break;
                    case IDM_WWW:
                        ShellExecute(nullptr, "explore", (app.path() + "\\data\\www").c_str(), 
                            nullptr, nullptr, SW_SHOW);
                        break;
                    case IDM_CHANGEDIR:
                        if (app.selectPath()) {
                            SetCurrentDirectory(app.path().c_str());
                            app.updateServerVersions();
                            app.updateTrayTooltip();
                            app.updateMenu();
                        }
                        break;
                    case IDM_EXIT:
                        DestroyWindow(hw);
                        break;
                }
                break;
            }

            case WM_DESTROY:
                Shell_NotifyIcon(NIM_DELETE, &app.nid);
                PostQuitMessage(0);
                break;

            default:
                return DefWindowProc(hw, msg, wp, lp);
        }
        return 0;
    }

    ~DevilboxManager() {
        // Cleanup menus and tray icon
        if (menu) DestroyMenu(menu);
        if (websitesMenu) DestroyMenu(websitesMenu);
        if (phpVersionMenu) DestroyMenu(phpVersionMenu);
        if (httpdVersionMenu) DestroyMenu(httpdVersionMenu);
        if (mysqlVersionMenu) DestroyMenu(mysqlVersionMenu);
    }
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Initialize COM
    struct COMInit {
        COMInit() { CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); }
        ~COMInit() { CoUninitialize(); }
    } comInit;

    // Initialize and run application
    auto& app = DevilboxManager::instance();
    if (!app.init(hInstance)) return 0;

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return msg.wParam;
}