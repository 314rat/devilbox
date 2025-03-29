/*******************************************************************************
 * Logs Viewer Module Implementation
 * Provides functionality for viewing and filtering PHP error logs
 *******************************************************************************/

 #include "logs_viewer.h"
 #include <stdio.h>
 #include <string.h>
 #include <commctrl.h> // For date time picker control
 
 // Logs dialog state structure
 typedef struct
 {
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
 
 // Global state for the logs dialog
 static LogsDialogState logs_dialog = {0};
 
 // Forward declarations of internal functions
 static LRESULT CALLBACK LogsDialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
 static void refresh_log_content(void);
 static void apply_log_filter(void);
 static void reset_log_filter(void);
 static void clear_log_file(void);
 static BOOL parse_log_date(const char *log_line, SYSTEMTIME *datetime);
 
 /**
  * Helper function to create a Windows control
  */
 HWND create_control(HWND parent, const char *class_name, const char *text,
                    DWORD style, int x, int y, int width, int height,
                    HMENU id, DWORD ex_style)
 {
     return CreateWindowEx(ex_style, class_name, text, style,
                           x, y, width, height, parent, id,
                           GetModuleHandle(NULL), NULL);
 }
 
 /**
  * Display PHP error logs (simplified UI creation)
  */
 void show_php_logs(const char *app_path, const char *php_version)
 {
     // Check if dialog is already open
     if (logs_dialog.hDlg && IsWindow(logs_dialog.hDlg))
     {
         SetForegroundWindow(logs_dialog.hDlg);
         return;
     }
 
     // Reset filter state
     logs_dialog.filtering = FALSE;
 
     // Extract PHP version number
     char version[10] = {0};
     const char *ver_start = strstr(php_version, "php-");
     ver_start = ver_start ? ver_start + 4 : php_version;
 
     if (!ver_start[0])
     {
         // Try to extract numbers if format is different
         for (int i = 0, j = 0; php_version[i] && j < 9; i++)
         {
             if ((php_version[i] >= '0' && php_version[i] <= '9') || php_version[i] == '.')
             {
                 version[j++] = php_version[i];
             }
         }
         ver_start = version;
     }
 
     // Format the log path
     snprintf(logs_dialog.log_path, sizeof(logs_dialog.log_path),
              "%s\\log\\php-fpm-%s\\php-fpm.error", app_path, ver_start);
 
     // Check if file exists, try alternative path if needed
     if (GetFileAttributes(logs_dialog.log_path) == INVALID_FILE_ATTRIBUTES)
     {
         snprintf(logs_dialog.log_path, sizeof(logs_dialog.log_path),
                  "%s\\log\\php-%s\\error.log", app_path, php_version);
 
         if (GetFileAttributes(logs_dialog.log_path) == INVALID_FILE_ATTRIBUTES)
         {
             char message[512];
             snprintf(message, sizeof(message),
                      "PHP error log file not found at:\n%s\n\nDevilbox may not have generated log files yet.",
                      logs_dialog.log_path);
             MessageBox(NULL, message, "Error", MB_ICONWARNING);
             return;
         }
     }
 
     // Register dialog class
     WNDCLASSEX wcDialog;
     memset(&wcDialog, 0, sizeof(WNDCLASSEX));
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
         NULL, NULL, GetModuleHandle(NULL), NULL);
 
     if (!logs_dialog.hDlg)
     {
         MessageBox(NULL, "Failed to create log viewer window.", "Error", MB_ICONERROR);
         return;
     }
 
     // Create controls using helper function
     DWORD editStyle = WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
                       ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY | ES_WANTRETURN;
 
     // Create text area for logs
     logs_dialog.hEdit = create_control(logs_dialog.hDlg, "EDIT", "",
                                        editStyle, 10, 70, 870, 540,
                                        (HMENU)ID_LOGS_EDIT, WS_EX_CLIENTEDGE);
 
     // Set a monospaced font for better log readability
     HFONT hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
     SendMessage(logs_dialog.hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
 
     // Create filter controls
     create_control(logs_dialog.hDlg, "STATIC", "Start Date:",
                    WS_CHILD | WS_VISIBLE, 10, 10, 80, 20, NULL, 0);
 
     logs_dialog.hDateStart = create_control(logs_dialog.hDlg, DATETIMEPICK_CLASS, "",
                                             WS_CHILD | WS_VISIBLE | DTS_SHORTDATEFORMAT,
                                             90, 10, 120, 24, (HMENU)ID_DATE_FILTER_START, 0);
 
     create_control(logs_dialog.hDlg, "STATIC", "Start Time:",
                    WS_CHILD | WS_VISIBLE, 220, 10, 80, 20, NULL, 0);
 
     logs_dialog.hTimeStart = create_control(logs_dialog.hDlg, "EDIT", "00:00:00",
                                             WS_CHILD | WS_VISIBLE,
                                             300, 10, 80, 24, (HMENU)ID_TIME_FILTER_START, WS_EX_CLIENTEDGE);
 
     create_control(logs_dialog.hDlg, "STATIC", "End Date:",
                    WS_CHILD | WS_VISIBLE, 400, 10, 80, 20, NULL, 0);
 
     logs_dialog.hDateEnd = create_control(logs_dialog.hDlg, DATETIMEPICK_CLASS, "",
                                           WS_CHILD | WS_VISIBLE | DTS_SHORTDATEFORMAT,
                                           480, 10, 120, 24, (HMENU)ID_DATE_FILTER_END, 0);
 
     create_control(logs_dialog.hDlg, "STATIC", "End Time:",
                    WS_CHILD | WS_VISIBLE, 610, 10, 80, 20, NULL, 0);
 
     logs_dialog.hTimeEnd = create_control(logs_dialog.hDlg, "EDIT", "23:59:59",
                                           WS_CHILD | WS_VISIBLE,
                                           690, 10, 80, 24, (HMENU)ID_TIME_FILTER_END, WS_EX_CLIENTEDGE);
 
     create_control(logs_dialog.hDlg, "STATIC",
                    "Filter logs by date/time range in the format: YYYY-MM-DD and HH:MM:SS",
                    WS_CHILD | WS_VISIBLE, 10, 40, 500, 20, NULL, 0);
 
     create_control(logs_dialog.hDlg, "BUTTON", "Apply Filter",
                    WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
                    790, 10, 100, 24, (HMENU)ID_APPLY_FILTER_BTN, 0);
 
     create_control(logs_dialog.hDlg, "BUTTON", "Reset Filter",
                    WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
                    790, 40, 100, 24, (HMENU)ID_RESET_FILTER_BTN, 0);
 
     // Action buttons
     create_control(logs_dialog.hDlg, "BUTTON", "Refresh",
                    WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
                    10, 620, 100, 30, (HMENU)ID_REFRESH_BTN, 0);
 
     create_control(logs_dialog.hDlg, "BUTTON", "Clear Logs",
                    WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
                    120, 620, 100, 30, (HMENU)ID_CLEAR_BTN, 0);
 
     create_control(logs_dialog.hDlg, "BUTTON", "Close",
                    WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
                    230, 620, 100, 30, (HMENU)ID_CLOSE_BTN, 0);
 
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
  * Parse date from a log line
  */
 static BOOL parse_log_date(const char *log_line, SYSTEMTIME *datetime)
 {
     // Example log format: [25-Feb-2025 07:16:59 UTC] PHP Warning: ...
     int day, hour, minute, second;
     char month[4];
     int year;
 
     if (sscanf(log_line, "[%d-%3[^-]-%d %d:%d:%d", &day, month, &year, &hour, &minute, &second) != 6)
     {
         return FALSE;
     }
 
     // Convert month name to month number
     int month_num = 0;
     const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                             "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
 
     for (int i = 0; i < 12; i++)
     {
         if (strcmp(month, months[i]) == 0)
         {
             month_num = i + 1;
             break;
         }
     }
 
     if (month_num == 0)
         return FALSE;
 
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
  * Function to refresh log content with filtering capability
  */
 static void refresh_log_content(void)
 {
     if (!logs_dialog.hEdit || !IsWindow(logs_dialog.hEdit))
         return;
 
     // Clear current text
     SetWindowText(logs_dialog.hEdit, "");
 
     // Read log file
     FILE *f = fopen(logs_dialog.log_path, "r");
     if (!f)
     {
         SetWindowText(logs_dialog.hEdit, "Failed to open log file.");
         return;
     }
 
     fseek(f, 0, SEEK_END);
     long size = ftell(f);
     fseek(f, 0, SEEK_SET);
 
     if (size <= 0)
     {
         SetWindowText(logs_dialog.hEdit, "Log file is empty.");
         fclose(f);
         return;
     }
 
     // Allocate buffer with room for conversion
     char *buffer = (char *)malloc(size + 1);
     if (!buffer)
     {
         SetWindowText(logs_dialog.hEdit, "Memory allocation failed.");
         fclose(f);
         return;
     }
 
     size_t read = fread(buffer, 1, size, f);
     buffer[read] = '\0';
     fclose(f);
 
     if (logs_dialog.filtering)
     {
         // Apply date/time filtering
         char *filtered = (char *)malloc(size + 1);
         if (!filtered)
         {
             SetWindowText(logs_dialog.hEdit, "Memory allocation failed for filtering.");
             free(buffer);
             return;
         }
 
         char *filtered_ptr = filtered;
         filtered[0] = '\0'; // Initialize as empty string
 
         // Process line by line
         char *line = strtok(buffer, "\n");
         while (line)
         {
             // Parse date from log line
             SYSTEMTIME lineDate;
             if (parse_log_date(line, &lineDate))
             {
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
                     CompareFileTime(&ftLine, &ftEnd) <= 0)
                 {
                     // Line is in date range
                     filtered_ptr += sprintf(filtered_ptr, "%s\r\n", line);
                 }
             }
 
             line = strtok(NULL, "\n");
         }
 
         if (strlen(filtered) > 0)
         {
             SetWindowText(logs_dialog.hEdit, filtered);
         }
         else
         {
             SetWindowText(logs_dialog.hEdit, "No log entries found in the specified date range.");
         }
 
         free(filtered);
     }
     else
     {
         // Process the buffer to ensure proper line breaks without filtering
         char *processed = (char *)malloc(size * 2 + 1); // Extra space for \r insertions
         if (processed)
         {
             size_t j = 0;
             for (size_t i = 0; i < read; i++)
             {
                 // Ensure proper Windows line endings (\r\n)
                 if (buffer[i] == '\n' && (i == 0 || buffer[i - 1] != '\r'))
                 {
                     processed[j++] = '\r';
                     processed[j++] = '\n';
                 }
                 else
                 {
                     processed[j++] = buffer[i];
                 }
             }
             processed[j] = '\0';
 
             SetWindowText(logs_dialog.hEdit, processed);
             free(processed);
         }
         else
         {
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
  * Function to clear the log file
  */
 static void clear_log_file(void)
 {
     if (MessageBox(logs_dialog.hDlg, "Are you sure you want to clear the log file?\nThis action cannot be undone.",
                    "Clear Log File", MB_YESNO | MB_ICONWARNING) != IDYES)
     {
         return;
     }
 
     FILE *f = fopen(logs_dialog.log_path, "w");
     if (f)
     {
         fclose(f);
         SetWindowText(logs_dialog.hEdit, "Log file has been cleared.");
     }
     else
     {
         MessageBox(logs_dialog.hDlg, "Failed to clear log file. The file may be in use or you may not have permission.",
                    "Error", MB_ICONERROR);
     }
 }
 
 /**
  * Function to apply date/time filter
  */
 static void apply_log_filter(void)
 {
     // Get the start date
     if (SendMessage(logs_dialog.hDateStart, DTM_GETSYSTEMTIME, 0, (LPARAM)&logs_dialog.filterStartDate) != GDT_VALID)
     {
         MessageBox(logs_dialog.hDlg, "Invalid start date selected.", "Error", MB_ICONERROR);
         return;
     }
 
     // Get the end date
     if (SendMessage(logs_dialog.hDateEnd, DTM_GETSYSTEMTIME, 0, (LPARAM)&logs_dialog.filterEndDate) != GDT_VALID)
     {
         MessageBox(logs_dialog.hDlg, "Invalid end date selected.", "Error", MB_ICONERROR);
         return;
     }
 
     // Get the start time
     char startTimeStr[20] = {0};
     GetWindowText(logs_dialog.hTimeStart, startTimeStr, sizeof(startTimeStr));
     int startHour, startMin, startSec;
     if (sscanf(startTimeStr, "%d:%d:%d", &startHour, &startMin, &startSec) != 3)
     {
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
     if (sscanf(endTimeStr, "%d:%d:%d", &endHour, &endMin, &endSec) != 3)
     {
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
 static void reset_log_filter(void)
 {
     logs_dialog.filtering = FALSE;
     refresh_log_content();
 }
 
 /**
  * Dialog procedure for logs window
  */
 static LRESULT CALLBACK LogsDialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
 {
     switch (msg)
     {
     case WM_COMMAND:
         switch (LOWORD(wp))
         {
         case ID_REFRESH_BTN:
             refresh_log_content();
             break;
         case ID_CLEAR_BTN:
             clear_log_file();
             break;
         case ID_CLOSE_BTN:
             DestroyWindow(hwnd);
             break;
         case ID_APPLY_FILTER_BTN:
             apply_log_filter();
             break;
         case ID_RESET_FILTER_BTN:
             reset_log_filter();
             break;
         }
         break;
 
     case WM_SIZE:
         if (logs_dialog.hEdit)
         {
             RECT rcClient;
             GetClientRect(hwnd, &rcClient);
 
             // Resize edit control to fit window
             SetWindowPos(logs_dialog.hEdit, NULL,
                          10, 70,
                          rcClient.right - 20, rcClient.bottom - 160,
                          SWP_NOZORDER);
 
             // Reposition buttons at the bottom
             SetWindowPos(GetDlgItem(hwnd, ID_REFRESH_BTN), NULL,
                          10, rcClient.bottom - 40, 100, 30, SWP_NOZORDER);
             SetWindowPos(GetDlgItem(hwnd, ID_CLEAR_BTN), NULL,
                          120, rcClient.bottom - 40, 100, 30, SWP_NOZORDER);
             SetWindowPos(GetDlgItem(hwnd, ID_CLOSE_BTN), NULL,
                          230, rcClient.bottom - 40, 100, 30, SWP_NOZORDER);
         }
         break;
 
     case WM_CLOSE:
         DestroyWindow(hwnd);
         break;
 
     case WM_DESTROY:
         logs_dialog.hDlg = logs_dialog.hEdit = logs_dialog.hDateStart = NULL;
         logs_dialog.hDateEnd = logs_dialog.hTimeStart = logs_dialog.hTimeEnd = NULL;
         break;
 
     default:
         return DefWindowProc(hwnd, msg, wp, lp);
     }
     return 0;
 }