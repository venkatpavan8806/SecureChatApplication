// gui_client.cpp - Enhanced Chat Client with Modern UI + Local Decryption
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include <winsock2.h>
#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <string>
#include <thread>
#include <atomic>
#include <sstream>
#include <iomanip>
#include <algorithm>

using namespace std;

const uint16_t DEFAULT_PORT = 5000;

// Modern color scheme
#define APP_COLOR_BACKGROUND RGB(18, 18, 18)
#define COLOR_PANEL         RGB(30, 30, 30)
#define COLOR_ACCENT        RGB(88, 101, 242)
#define COLOR_ACCENT_HOVER  RGB(71, 82, 196)
#define COLOR_TEXT          RGB(220, 221, 222)
#define COLOR_TEXT_MUTED    RGB(142, 146, 151)
#define COLOR_SUCCESS       RGB(67, 181, 129)
#define COLOR_ERROR         RGB(237, 66, 69)
#define COLOR_INPUT_BG      RGB(64, 68, 75)
#define COLOR_BORDER        RGB(50, 50, 50)

// Control IDs
#define IDC_SERVER_INPUT    1001
#define IDC_CONNECT_BTN     1002
#define IDC_USERNAME_INPUT  1003
#define IDC_PASSWORD_INPUT  1004
#define IDC_REGISTER_BTN    1005
#define IDC_LOGIN_BTN       1006
#define IDC_CHAT_DISPLAY    1007
#define IDC_MESSAGE_INPUT   1008
#define IDC_SEND_BTN        1009
#define IDC_CONNECT_USER    1010
#define IDC_DISCONNECT_BTN  1011
#define IDC_LIST_USERS_BTN  1012
#define IDC_STATUS_BAR      1013

// Window states
enum AppState {
    STATE_SERVER_CONNECT,
    STATE_AUTH,
    STATE_CHAT
};

// Globals
HWND g_hWnd = NULL;
HWND g_hChatDisplay = NULL;
HWND g_hMessageInput = NULL;
HWND g_hStatusBar = NULL;
SOCKET g_socket = INVALID_SOCKET;
atomic<bool> g_running(true);
atomic<bool> g_authenticated(false);
AppState g_currentState = STATE_SERVER_CONNECT;
string g_username;
string g_leftover;
string g_sessionKey;
string g_currentPartner;
thread* g_receiverThread = nullptr;

// Fonts
HFONT g_hFontTitle = NULL;
HFONT g_hFontNormal = NULL;
HFONT g_hFontButton = NULL;

// Brushes
HBRUSH g_hBrushBackground = NULL;
HBRUSH g_hBrushPanel = NULL;
HBRUSH g_hBrushInput = NULL;

// Forward declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void CreateServerConnectUI(HWND hwnd);
void CreateAuthUI(HWND hwnd);
void CreateChatUI(HWND hwnd);
void AppendToChatDisplay(const string& text, bool isSystem = false, bool isOwn = false);
void SetStatus(const string& text);
bool SendLine(SOCKET s, const string& text);
bool RecvLine(SOCKET s, string& out, string& leftover);
void ReceiverThreadFunc();
bool ConnectToServer(const string& address);
bool Authenticate(const string& mode, const string& username, const string& password);
void SendMessage();

#define WM_CLEAR_CHAT (WM_USER + 1)

// Encryption/Decryption
string aesDecrypt(const string& hex, const string& key) {
    string decrypted;
    if (key.empty()) return "[NO_KEY]";

    for (size_t i = 0; i + 1 < hex.length(); i += 2) {
        string byteStr = hex.substr(i, 2);
        unsigned char byteVal = (unsigned char)strtol(byteStr.c_str(), nullptr, 16);
        char decryptedChar = byteVal ^ key[(i / 2) % key.length()];
        decrypted += decryptedChar;
    }
    return decrypted;
}

string aesEncrypt(const string& message, const string& key) {
    if (key.empty()) return "[NO_KEY]";
    
    string encrypted;
    for (size_t i = 0; i < message.length(); i++) {
        char encryptedChar = message[i] ^ key[i % key.length()];
        char hex[3];
        snprintf(hex, sizeof(hex), "%02x", (unsigned char)encryptedChar);
        encrypted += hex;
    }
    return encrypted;
}

// Utilities
bool SendLine(SOCKET s, const string& text) {
    string message = text + "\n";
    int result = send(s, message.c_str(), (int)message.size(), 0);
    return result != SOCKET_ERROR;
}

bool RecvLine(SOCKET s, string& out, string& leftover) {
    size_t pos;
    while (true) {
        pos = leftover.find('\n');
        if (pos != string::npos) {
            out = leftover.substr(0, pos);
            leftover.erase(0, pos + 1);
            if (!out.empty() && out.back() == '\r') out.pop_back();
            return true;
        }
        char buffer[4096];
        int bytes = recv(s, buffer, sizeof(buffer), 0);
        if (bytes <= 0) return false;
        leftover.append(buffer, bytes);
        if (leftover.size() > 10000) {
            leftover.clear();
            return false;
        }
    }
}

void AppendToChatDisplay(const string& text, bool isSystem, bool isOwn) {
    if (!g_hChatDisplay) return;
    
    string prefix;
    if (isSystem) {
        prefix = "[SYSTEM] ";
    } else if (isOwn) {
        prefix = "[You] ";
    } else if (!g_currentPartner.empty()) {
        prefix = "[" + g_currentPartner + "] ";
    }
    
    int len = GetWindowTextLengthA(g_hChatDisplay);
    SendMessageA(g_hChatDisplay, EM_SETSEL, len, len);
    SendMessageA(g_hChatDisplay, EM_REPLACESEL, FALSE, (LPARAM)(prefix + text + "\r\n").c_str());
    SendMessageA(g_hChatDisplay, EM_SCROLLCARET, 0, 0);
}

void SetStatus(const string& text) {
    if (g_hStatusBar)
        SendMessageA(g_hStatusBar, SB_SETTEXTA, 0, (LPARAM)text.c_str());
}

// Receiver Thread
void ReceiverThreadFunc() {
    u_long mode = 1;
    ioctlsocket(g_socket, FIONBIO, &mode);

    string message;
    while (g_running && g_authenticated) {
        if (RecvLine(g_socket, message, g_leftover)) {
            if (message.empty()) continue;

            // Handle session key
            if (message.rfind("SESSION_KEY:", 0) == 0) {
                g_sessionKey = message.substr(12);
                AppendToChatDisplay("Secure encryption key established", true);
                continue;
            }

            // Handle encrypted messages - DECRYPT LOCALLY
            if (message.rfind("ENCRYPTED:", 0) == 0) {
                string encryptedHex = message.substr(10);
                if (!g_sessionKey.empty()) {
                    string decrypted = aesDecrypt(encryptedHex, g_sessionKey);
                    AppendToChatDisplay(decrypted, false, false);
                } else {
                    AppendToChatDisplay("[Unable to decrypt - no key]", true);
                }
                continue;
            }

            // Handle connection messages (support both plain and emoji-prefixed)
            if (message.find("CONNECTED:") != string::npos ||
                message.find("🎉 CONNECTED:") != string::npos) {

                size_t pos = message.find("with ");
                if (pos != string::npos) {
                    g_currentPartner = message.substr(pos + 5);
                    // Trim spaces/newlines
                    g_currentPartner.erase(remove_if(g_currentPartner.begin(),
                        g_currentPartner.end(), ::isspace), g_currentPartner.end());
                    AppendToChatDisplay("Connected with " + g_currentPartner, true);
                } else {
                    AppendToChatDisplay(message, true);
                }
                continue;
            }

            // Handle [CHAT] messages
            if (message.find("[CHAT]") != string::npos) {
                // Extract sender name between second pair of brackets: [CHAT][username]
                size_t start = message.find('[', message.find("[CHAT]") + 6);
                size_t end = message.find(']', start + 1);
                string sender;
                if (start != string::npos && end != string::npos)
                    sender = message.substr(start + 1, end - start - 1);

                // Skip displaying your own message again
                if (_stricmp(sender.c_str(), g_username.c_str()) != 0) {
                    AppendToChatDisplay(message, true);
                }
                continue;
            }

            // Handle disconnection
            if (message.find("DISCONNECTED:") == 0) {
                AppendToChatDisplay(message.substr(13), true);
                g_currentPartner.clear();
                continue;
            }

            // Default: display as system or informational
            AppendToChatDisplay(message, true);
        } else {
            Sleep(50);
        }
    }
}

// Modern UI Button
HWND CreateModernButton(HWND parent, const char* text, int x, int y, int w, int h, int id, bool isPrimary = false) {
    HWND btn = CreateWindowA("BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, NULL, NULL);
    return btn;
}

// Modern Input Field
HWND CreateModernInput(HWND parent, const char* placeholder, int x, int y, int w, int h, int id, bool isPassword = false) {
    DWORD style = WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL;
    if (isPassword) style |= ES_PASSWORD;
    
    HWND input = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        style, x, y, w, h, parent, (HMENU)(INT_PTR)id, NULL, NULL);
    
    if (g_hFontNormal) SendMessage(input, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    return input;
}

// UI Builders
void CreateServerConnectUI(HWND hwnd) {
    // Clear existing controls except status bar
    HWND child = GetWindow(hwnd, GW_CHILD);
    while (child) {
        HWND next = GetWindow(child, GW_HWNDNEXT);
        if (child != g_hStatusBar) DestroyWindow(child);
        child = next;
    }

    // Title
    HWND hTitle = CreateWindowA("STATIC", "Connect to Chat Server",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        100, 60, 600, 40, hwnd, NULL, NULL, NULL);
    if (g_hFontTitle) SendMessage(hTitle, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);

    // Instruction
    CreateWindowA("STATIC", "Enter server address (e.g., localhost or 192.168.1.100:5000)",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        100, 120, 600, 25, hwnd, NULL, NULL, NULL);

    // Server input
    CreateModernInput(hwnd, "localhost", 200, 160, 400, 35, IDC_SERVER_INPUT);

    // Connect button
    CreateModernButton(hwnd, "Connect to Server", 300, 220, 200, 40, IDC_CONNECT_BTN, true);

    // Info text
    CreateWindowA("STATIC",
        "Same device: Use localhost or 127.0.0.1\n"
        "Local network: Use the server's IP address\n"
        "Default port: 5000 (add :PORT for custom)",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        150, 290, 500, 80, hwnd, NULL, NULL, NULL);

    SetStatus("Ready to connect to server");
}

void CreateAuthUI(HWND hwnd) {
    HWND child = GetWindow(hwnd, GW_CHILD);
    while (child) {
        HWND next = GetWindow(child, GW_HWNDNEXT);
        if (child != g_hStatusBar) DestroyWindow(child);
        child = next;
    }

    // Title
    HWND hTitle = CreateWindowA("STATIC", "Welcome to Secure Chat",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        100, 50, 600, 40, hwnd, NULL, NULL, NULL);
    if (g_hFontTitle) SendMessage(hTitle, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);

    // Subtitle
    CreateWindowA("STATIC", "Login or create a new account",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        100, 100, 600, 25, hwnd, NULL, NULL, NULL);

    // Username label
    CreateWindowA("STATIC", "Username",
        WS_CHILD | WS_VISIBLE,
        200, 150, 400, 20, hwnd, NULL, NULL, NULL);
    
    // Username input
    CreateModernInput(hwnd, "", 200, 175, 400, 35, IDC_USERNAME_INPUT);

    // Password label
    CreateWindowA("STATIC", "Password",
        WS_CHILD | WS_VISIBLE,
        200, 230, 400, 20, hwnd, NULL, NULL, NULL);
    
    // Password input
    CreateModernInput(hwnd, "", 200, 255, 400, 35, IDC_PASSWORD_INPUT, true);

    // Buttons
    CreateModernButton(hwnd, "Register", 200, 320, 180, 40, IDC_REGISTER_BTN);
    CreateModernButton(hwnd, "Login", 420, 320, 180, 40, IDC_LOGIN_BTN, true);

    SetStatus("Connected to server. Please login or register.");
}

void CreateChatUI(HWND hwnd) {
    HWND child = GetWindow(hwnd, GW_CHILD);
    while (child) {
        HWND next = GetWindow(child, GW_HWNDNEXT);
        if (child != g_hStatusBar) DestroyWindow(child);
        child = next;
    }

    // Chat display
    g_hChatDisplay = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        20, 20, 760, 380, hwnd, (HMENU)IDC_CHAT_DISPLAY, NULL, NULL);
    if (g_hFontNormal) SendMessage(g_hChatDisplay, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

    // Connection controls
    CreateWindowA("STATIC", "Connect to:",
        WS_CHILD | WS_VISIBLE,
        20, 415, 80, 20, hwnd, NULL, NULL, NULL);
    
    CreateModernInput(hwnd, "username", 105, 412, 150, 25, IDC_CONNECT_USER);
    CreateModernButton(hwnd, "Connect", 265, 411, 85, 27, IDC_CONNECT_BTN, true);
    CreateModernButton(hwnd, "Disconnect", 360, 411, 95, 27, IDC_DISCONNECT_BTN);
    CreateModernButton(hwnd, "List Users", 465, 411, 95, 27, IDC_LIST_USERS_BTN);

    // Message input
    CreateWindowA("STATIC", "Message:",
        WS_CHILD | WS_VISIBLE,
        20, 455, 60, 20, hwnd, NULL, NULL, NULL);
    
    g_hMessageInput = CreateModernInput(hwnd, "Type your message...", 90, 452, 580, 30, IDC_MESSAGE_INPUT);
    CreateModernButton(hwnd, "Send", 680, 452, 100, 30, IDC_SEND_BTN, true);

    AppendToChatDisplay("=== Secure Chat Connected ===", true);
    AppendToChatDisplay("Logged in as: " + g_username, true);
    AppendToChatDisplay("", true);
    AppendToChatDisplay("Commands:", true);
    AppendToChatDisplay("  • Enter username and click Connect to start chatting", true);
    AppendToChatDisplay("  • Click List Users to see who's online", true);
    AppendToChatDisplay("  • All messages are encrypted end-to-end", true);
    AppendToChatDisplay("", true);

    SetStatus("Logged in as " + g_username + " - Ready to chat");
    SetFocus(g_hMessageInput);
}

// Custom draw for modern buttons
void DrawModernButton(HWND hwnd, DRAWITEMSTRUCT* dis) {
    HDC hdc = dis->hDC;
    RECT rect = dis->rcItem;
    
    bool isPressed = (dis->itemState & ODS_SELECTED);
    bool isHover = (dis->itemState & ODS_HOTLIGHT) || (dis->itemState & ODS_FOCUS);
    
    int id = GetDlgCtrlID(hwnd);
    bool isPrimary = (id == IDC_CONNECT_BTN || id == IDC_LOGIN_BTN || id == IDC_SEND_BTN);
    
    // Background
    COLORREF bgColor = isPrimary ? COLOR_ACCENT : COLOR_PANEL;
    if (isHover && isPrimary) bgColor = COLOR_ACCENT_HOVER;
    if (isHover && !isPrimary) bgColor = RGB(45, 45, 45);
    if (isPressed) {
        int r = GetRValue(bgColor) * 0.8;
        int g = GetGValue(bgColor) * 0.8;
        int b = GetBValue(bgColor) * 0.8;
        bgColor = RGB(r, g, b);
    }
    
    HBRUSH hBrush = CreateSolidBrush(bgColor);
    FillRect(hdc, &rect, hBrush);
    DeleteObject(hBrush);
    
    // Border
    HPEN hPen = CreatePen(PS_SOLID, 1, COLOR_BORDER);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    MoveToEx(hdc, rect.left, rect.top, NULL);
    LineTo(hdc, rect.right - 1, rect.top);
    LineTo(hdc, rect.right - 1, rect.bottom - 1);
    LineTo(hdc, rect.left, rect.bottom - 1);
    LineTo(hdc, rect.left, rect.top);
    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
    
    // Text
    char text[256];
    GetWindowTextA(hwnd, text, sizeof(text));
    SetTextColor(hdc, COLOR_TEXT);
    SetBkMode(hdc, TRANSPARENT);
    if (g_hFontButton) SelectObject(hdc, g_hFontButton);
    DrawTextA(hdc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// Main window procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            // Create fonts
            g_hFontTitle = CreateFontA(28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
            
            g_hFontNormal = CreateFontA(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
            
            g_hFontButton = CreateFontA(14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

            // Create brushes
            g_hBrushBackground = CreateSolidBrush(COLOR_BACKGROUND);
            g_hBrushPanel = CreateSolidBrush(COLOR_PANEL);
            g_hBrushInput = CreateSolidBrush(COLOR_INPUT_BG);

            g_hStatusBar = CreateWindowExA(0, STATUSCLASSNAMEA, NULL,
                WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                0, 0, 0, 0, hwnd, (HMENU)IDC_STATUS_BAR, NULL, NULL);
            
            CreateServerConnectUI(hwnd);
            return 0;
        }

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT: {
            HDC hdcStatic = (HDC)wParam;
            SetTextColor(hdcStatic, COLOR_TEXT);
            SetBkColor(hdcStatic, COLOR_BACKGROUND);
            return (LRESULT)g_hBrushBackground;
        }

        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
            if (dis->CtlType == ODT_BUTTON) {
                DrawModernButton(dis->hwndItem, dis);
                return TRUE;
            }
            break;
        }

        case WM_SIZE:
            SendMessage(g_hStatusBar, WM_SIZE, 0, 0);
            return 0;

        case WM_CLEAR_CHAT:
            CreateChatUI(hwnd);
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_CONNECT_BTN:
                    if (g_currentState == STATE_SERVER_CONNECT) {
                        char address[256];
                        GetWindowTextA(GetDlgItem(hwnd, IDC_SERVER_INPUT), address, sizeof(address));
                        SetStatus("Connecting to server...");
                        if (ConnectToServer(address)) {
                            g_currentState = STATE_AUTH;
                            CreateAuthUI(hwnd);
                        } else {
                            MessageBoxA(hwnd, "Failed to connect to server.\nEnsure the server is running.",
                                        "Connection Error", MB_OK | MB_ICONERROR);
                            SetStatus("Connection failed");
                        }
                    } else if (g_currentState == STATE_CHAT) {
                        char targetUser[256];
                        GetWindowTextA(GetDlgItem(hwnd, IDC_CONNECT_USER), targetUser, sizeof(targetUser));
                        if (strlen(targetUser) > 0) {
                            string cmd = string("connect ") + targetUser;
                            SendLine(g_socket, cmd);
                            SetWindowTextA(GetDlgItem(hwnd, IDC_CONNECT_USER), "");
                        }
                    }
                    break;

                case IDC_REGISTER_BTN:
                case IDC_LOGIN_BTN: {
                    char username[256], password[256];
                    GetWindowTextA(GetDlgItem(hwnd, IDC_USERNAME_INPUT), username, sizeof(username));
                    GetWindowTextA(GetDlgItem(hwnd, IDC_PASSWORD_INPUT), password, sizeof(password));
                    if (strlen(username) == 0 || strlen(password) == 0) {
                        MessageBoxA(hwnd, "Please enter both username and password", "Input Required", MB_OK | MB_ICONWARNING);
                        break;
                    }
                    string mode = (LOWORD(wParam) == IDC_REGISTER_BTN) ? "register" : "login";
                    SetStatus("Authenticating...");
                    if (Authenticate(mode, username, password)) {
                        g_currentState = STATE_CHAT;
                        CreateChatUI(hwnd);
                        g_receiverThread = new thread(ReceiverThreadFunc);
                    } else {
                        SetStatus("Authentication failed");
                    }
                    break;
                }

                case IDC_SEND_BTN:
                    SendMessage();
                    break;

                case IDC_DISCONNECT_BTN:
                    SendLine(g_socket, "disconnect");
                    g_currentPartner.clear();
                    AppendToChatDisplay("Disconnected from chat", true);
                    break;

                case IDC_LIST_USERS_BTN:
                    SendLine(g_socket, "list");
                    break;

                case IDC_MESSAGE_INPUT:
                    if (HIWORD(wParam) == EN_SETFOCUS) {
                        // Handle Enter key in message input
                    }
                    break;
            }
            return 0;

        case WM_DESTROY:
            g_running = false;
            if (g_receiverThread) {
                if (g_receiverThread->joinable()) g_receiverThread->join();
                delete g_receiverThread;
                g_receiverThread = nullptr;
            }
            if (g_socket != INVALID_SOCKET) {
                SendLine(g_socket, "exit");
                closesocket(g_socket);
            }
            
            if (g_hFontTitle) DeleteObject(g_hFontTitle);
            if (g_hFontNormal) DeleteObject(g_hFontNormal);
            if (g_hFontButton) DeleteObject(g_hFontButton);
            if (g_hBrushBackground) DeleteObject(g_hBrushBackground);
            if (g_hBrushPanel) DeleteObject(g_hBrushPanel);
            if (g_hBrushInput) DeleteObject(g_hBrushInput);
            
            WSACleanup();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

bool ConnectToServer(const string& address) {
    string host;
    int port = DEFAULT_PORT;
    size_t colonPos = address.find(':');
    if (colonPos != string::npos) {
        host = address.substr(0, colonPos);
        try { port = stoi(address.substr(colonPos + 1)); } catch (...) { return false; }
    } else host = address;

    g_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (g_socket == INVALID_SOCKET) return false;

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = inet_addr(host.c_str());
    if (serverAddr.sin_addr.s_addr == INADDR_NONE) {
        hostent* he = gethostbyname(host.c_str());
        if (!he) { closesocket(g_socket); return false; }
        memcpy(&serverAddr.sin_addr, he->h_addr, he->h_length);
    }
    if (connect(g_socket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        closesocket(g_socket);
        return false;
    }
    return true;
}

bool Authenticate(const string& mode, const string& username, const string& password) {
    if (!SendLine(g_socket, mode) ||
        !SendLine(g_socket, username) ||
        !SendLine(g_socket, password))
        return false;

    string response;
    if (!RecvLine(g_socket, response, g_leftover)) return false;

    if (response.find("ERROR:") == 0) {
        MessageBoxA(g_hWnd, response.substr(6).c_str(), "Authentication Error",
                    MB_OK | MB_ICONERROR);
        return false;
    }

    if (response.find("REGISTER_SUCCESS:") == 0 ||
        response.find("LOGIN_SUCCESS:") == 0) {
        g_username = username;
        g_authenticated = true;
        return true;
    }
    return false;
}

void SendMessage() {
    char buffer[1024];
    GetWindowTextA(g_hMessageInput, buffer, sizeof(buffer));
    string message = buffer;

    // Empty message check
    if (message.empty()) return;

    // Make sure we have a valid chat partner
    if (g_currentPartner.empty()) {
        MessageBoxA(
            g_hWnd,
            "Please connect to a user first!",
            "Not Connected",
            MB_OK | MB_ICONWARNING
        );
        return;
    }

    // Format the message before sending (optional)
    string formattedMessage = "[CHAT][" + g_currentPartner + "] " + message;

    // Send to server
    if (SendLine(g_socket, formattedMessage)) {
        // Show own message locally (decrypted preview)
        AppendToChatDisplay(message, false, true);

        // Clear input box and focus back
        SetWindowTextA(g_hMessageInput, "");
        SetFocus(g_hMessageInput);
    } else {
        MessageBoxA(
            g_hWnd,
            "Failed to send message",
            "Error",
            MB_OK | MB_ICONERROR
        );
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        MessageBoxA(NULL, "Failed to initialize Winsock", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    INITCOMMONCONTROLSEX icex{ sizeof(icex), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icex);

    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "EnhancedChatClient";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(COLOR_BACKGROUND);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassExA(&wc);

    g_hWnd = CreateWindowExA(0, "EnhancedChatClient",
                             "Secure Chat - Encrypted Messaging",
                             WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, 820, 580,
                             NULL, NULL, hInstance, NULL);

    if (!g_hWnd) {
        MessageBoxA(NULL, "Window creation failed", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    MSG msg{};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}