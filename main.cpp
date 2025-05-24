#include "common.h"

#include "vjoyInc/public.h"
#include "vjoyInc/vjoyinterface.h"

/* Default window */
HWND hWnd;

/* Images */
HBITMAP bannerBMP;

/* Edit */
HWND Edit;
#define EDIT_MAXLINES 8

#define TITLE "THOTD Feeder v1.0\r\n"

/* vJoy device */
#define VJDINTERFACE_1 1
#define VJDINTERFACE_2 2

#define AXIS_MAX 0x8000
#define AXIS_MIN 0x1

#define BTN_ID_1 1
#define BTN_ID_2 2

const int AXIS_MID = AXIS_MAX - (AXIS_MAX - AXIS_MIN) / 2;

/* RawInput */
#define MAX_MICE 2
HANDLE mouseHID[MAX_MICE] = {0};
int    mouseDPI[MAX_MICE] = {32, 32};
int    mouseID            = 0; // Selected mouse ID
int    progMode           = 0; // 0 - Setup, 1 - Operational
int    interfaceID[2]     = {VJDINTERFACE_1, VJDINTERFACE_2};

bool hJoyThreadRunning = true;
HANDLE hJoyThread;

/* Program settings */
bool debugMode = false;

/* Global buffer */
char gbuf[1024];

/* Edit control text append */
void appendText(char* data) {
    int len = GetWindowTextLength(Edit);
    SendMessage(Edit, EM_SETSEL, len, len);

    int lineNum = SendMessage(Edit, EM_GETLINECOUNT, 0, 0);
    if (lineNum > EDIT_MAXLINES) {
        SetWindowTextA(Edit, data);
    }
    else {
        SendMessage(Edit, EM_REPLACESEL, 1, (LPARAM)"\r\n");
        SendMessage(Edit, EM_REPLACESEL, 1, (LPARAM)data);
    }
}

/* Create configuration file */
void createConfig(int sensiv_1, int sensiv_2) {
    FILE *fp;
    fp = fopen("config.bin", "wb");
    if (!fp) fatalError(hWnd, "Unable to create configuration file!");

    const int confSize = 4;
    int confData[confSize] = {0x52554853, 0x4E454641, sensiv_1, sensiv_2}; // Why not? :)

    fwrite(confData, sizeof(confData[0]), confSize, fp);

    fclose(fp);
}

/* Load configuration file */
void loadConfig() {
    FILE *fp;
    while (!(fp = fopen("config.bin", "rb"))) {
        warnUser(hWnd, "Configuration file was not found.\r\nDefault one will be created.");
        createConfig(32, 32);
    }

    const int confSize = 4;
    int confData[confSize] = {0};

    fread(confData, sizeof(confData[0]), confSize, fp);

    fclose(fp);

    /* Process loaded configuration */
    mouseDPI[0] = confData[2];
    mouseDPI[1] = confData[3];
}

/* Raw Input setup */
void setupRawInput() {
    /* Create RawInput device */
    UINT nDevices = 0;
    PRAWINPUTDEVICELIST pRawInputDeviceList = 0;

    GetRawInputDeviceList(0, &nDevices, sizeof(RAWINPUTDEVICELIST));
    if (!nDevices) fatalError(hWnd, "No devices available!");
    pRawInputDeviceList = (PRAWINPUTDEVICELIST)malloc(sizeof(RAWINPUTDEVICELIST) * nDevices);
    nDevices = GetRawInputDeviceList(pRawInputDeviceList, &nDevices, sizeof(RAWINPUTDEVICELIST));
    if (nDevices == (UINT)-1) fatalError(hWnd, "No devices available!");

    /* Register devices */
    UINT   mouseDevNum = 0;
    for (unsigned int i = 0; i < nDevices; i++) {
        if (pRawInputDeviceList[i].dwType == RIM_TYPEMOUSE) {
            mouseDevNum++;
        }
    }
    if (!mouseDevNum) warnUser(hWnd, "No mice available!");
    else if (mouseDevNum < 2) warnUser(hWnd, "At least 2 mice must be connected!");

    /* Free */
    free(pRawInputDeviceList);

    /* Register Raw Input */
    RAWINPUTDEVICE rdev;
    rdev.usUsagePage = 0x01;
    rdev.usUsage     = 0x02;
    rdev.dwFlags     = RIDEV_INPUTSINK;
    rdev.hwndTarget  = hWnd;
    if (!RegisterRawInputDevices(&rdev, 1, sizeof(RAWINPUTDEVICE))) fatalError(hWnd, "Failed to register devices!");

    sprintf(gbuf, "Available devices: %d\r\nAvailable mice: %d\r\nClick to register...", nDevices, mouseDevNum);
    appendText(gbuf);
}

/* Accurate sleep */
void usleep(int usec)
{
    HANDLE timer;
    LARGE_INTEGER ft;

    ft.QuadPart = -(10 * usec);

    timer = CreateWaitableTimer(NULL, TRUE, NULL);
    SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
    WaitForSingleObject(timer, INFINITE);
    CloseHandle(timer);
}

/* Check if mouse is registered */
bool checkMouse(HANDLE hDev) {
    bool result = false;

    for (int i = 0; i < MAX_MICE; i++) {
        result |= (hDev == mouseHID[i]);

        if (result) {
            mouseID = i;
            break;
        }
    }

    return result;
}

/* Threaded device movement */
int forceX[MAX_MICE] = {0}, forceY[MAX_MICE] = {0};
static DWORD WINAPI joyThread(LPVOID lpParam) {
    while (hJoyThreadRunning) {
        /* Process movement */
        for (int i = 0; i < MAX_MICE; i++) {
            SetAxis(AXIS_MID + forceX[i], interfaceID[i], HID_USAGE_X);
            SetAxis(AXIS_MID + forceY[i], interfaceID[i], HID_USAGE_Y);

            /* Decrease force */
            if (forceX[i] != 0) forceX[i] = forceX[i] / 2;
            if (forceY[i] != 0) forceY[i] = forceY[i] / 2;
        }

        usleep(10000);
    }

    return 0;
}

void joyApplyForce(int Xval, int Yval, int ID) {
    if (Xval >= AXIS_MID)       Xval =  AXIS_MID - 1;
    else if (Xval <= -AXIS_MID) Xval = -AXIS_MID + 1;

    if (Yval >= AXIS_MID)       Yval =  AXIS_MID - 1;
    else if (Yval <= -AXIS_MID) Yval = -AXIS_MID + 1;

    forceX[ID] = Xval;
    forceY[ID] = Yval;
}

/* Normalize integers */
double normalize(int num, int maxValue) {
    if (abs(num) >= maxValue) {
        num = num / abs(num) * maxValue;
    }

    /* Return in range [-1.0f, +1.0f] */
    return (double)num / maxValue;
}

void processArgs() {
    /* Break line into arguments */
    char **argv  = __argv;
    int    argc  = __argc;

    /* Process */
    for (int i = 1; i < argc; i++) {
        /* Convert to lowercase */
        for (int j = 0; argv[i][j]; j++) argv[i][j] = tolower(argv[i][j]);

        if (!strcmp(argv[i], "debug")) debugMode = true;
        else if (!strcmp(argv[i], "set")) {
            if (i + 2 >= argc) break;

            /* Create config */
            createConfig(atoi(argv[i + 1]), atoi(argv[i + 2]));
            i += 2;
        }
    }

    /* Load config file */
    loadConfig();
}

/* Entry point */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
{
    WNDCLASSEX WinClass;
    MSG msg;

    /* WNDCLASSEX Structure */
    WinClass.cbSize        = sizeof(WNDCLASSEX);
    WinClass.style         = 0;
    WinClass.lpfnWndProc   = WinProc;
    WinClass.cbClsExtra    = 0;
    WinClass.cbWndExtra    = 0;
    WinClass.hInstance     = hInstance;
    WinClass.hIcon         = LoadIcon(hInstance, MAKEINTRESOURCE(icon));
    WinClass.hCursor       = LoadCursor(0, IDC_ARROW);
    WinClass.hbrBackground = (HBRUSH)GetStockObject(LTGRAY_BRUSH);
    WinClass.lpszMenuName  = 0;
    WinClass.lpszClassName = className;
    WinClass.hIconSm       = LoadIcon(hInstance, MAKEINTRESOURCE(icon));

    /* Register window class */
    if (!RegisterClassExA(&WinClass)) fatalError(0, "Can't register class!");

    /* Create window */
    hWnd = CreateWindowExA(WS_EX_TOPMOST, className, winName, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, (GetSystemMetrics(SM_CXSCREEN) - winW) / 2, (GetSystemMetrics(SM_CYSCREEN) - winH) / 2, winW, winH, HWND_DESKTOP, 0, hInstance, 0);
    if (!hWnd) fatalError(0, "Can't create window!");

    /* Window controls */
    Edit = CreateWindowA(WC_EDITA, TITLE, WS_VISIBLE | WS_CHILD | WS_BORDER | ES_LEFT | ES_MULTILINE | ES_READONLY, 10, 10, 294, 160, hWnd, 0, hInstance, 0);
           CreateWindowA(WC_BUTTONA, "ABOUT", WS_VISIBLE | WS_CHILD | WS_TABSTOP, 10, 178, 90, 24, hWnd, (HMENU)1, hInstance, 0);
           CreateWindowA(WC_BUTTONA, "QUIT", WS_VISIBLE | WS_CHILD | WS_TABSTOP, 110, 178, 90, 24, hWnd, (HMENU)2, hInstance, 0);

    /* Load images */
    bannerBMP = LoadBitmapA(hInstance, MAKEINTRESOURCE(banner));
    if (!bannerBMP) warnUser(0, "Can't load bitmap!");

    /* Lock vJoy device */
    if (!vJoyEnabled()) fatalError(hWnd, "vJoy is disabled!");
    if (!AcquireVJD(VJDINTERFACE_1)) fatalError(hWnd, "Failed to acquire VJD #1 device!");
    if (!AcquireVJD(VJDINTERFACE_2)) fatalError(hWnd, "Failed to acquire VJD #2 device!");

    /* Process arguments */
    processArgs();

    /* Setup Raw Input */
    setupRawInput();

    /* Create joystick thread */
    hJoyThread = CreateThread(0, 0, &joyThread, 0, 0, 0);

    /* Show window */
    ShowWindow(hWnd, TRUE);

    /* Messages loop */
    while (GetMessage(&msg, 0, 0, 0) != 0) {
        if (!IsDialogMessage(hWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    /* Close joystick thread */
    hJoyThreadRunning = false;
    WaitForSingleObject(hJoyThread, INFINITE);
    CloseHandle(hJoyThread);

    /* Unlock vJoy device */
    RelinquishVJD(VJDINTERFACE_1);
    RelinquishVJD(VJDINTERFACE_2);

    /* Close mice handles */
    for (int i = 0; i < MAX_MICE; i++) CloseHandle(mouseHID[i]);

    /* Exit from application */
    return 0;
}

/* WinProc */
LRESULT CALLBACK WinProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    /* Process messages */
    switch (msg) {
	    /* Commands */
        case WM_COMMAND:
        {
            int btnID   = LOWORD(wParam);
            switch (btnID) {
            case 1:
                MessageBoxA(hWnd, "Written in May 2025\n(c) Alexander Olovyanishnikov (Shurafen)\nMoscow, Russian Federation", "About", MB_ICONINFORMATION | MB_OK);
                break;
            case 2:
                PostQuitMessage(0);
                break;
            }
            break;
        }

        /* Input */
        case WM_INPUT:
        {
            UINT pcbSize = 0;
            GetRawInputData((HRAWINPUT)lParam, RID_INPUT, 0, &pcbSize, sizeof(RAWINPUTHEADER));
            if (!pcbSize) break;

            LPVOID pData = malloc(pcbSize);
            if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, pData, &pcbSize, sizeof(RAWINPUTHEADER)) != pcbSize) fatalError(hWnd, "Unknown input error!");

            /* Is captured device mouse? */
            RAWINPUT* rdev = (RAWINPUT*)pData;
            if (rdev->header.dwType == RIM_TYPEMOUSE) {
                /* Handle mouse input */
                switch (progMode) {
                    case 0:
                    {
                        /* Button 1 trigger */
                        if (rdev->data.mouse.usButtonFlags == RI_MOUSE_BUTTON_1_DOWN) {
                            mouseHID[mouseID] = rdev->header.hDevice;

                            /* Check collision */
                            if (mouseID) {
                                if (mouseHID[mouseID] == mouseHID[mouseID - 1]) {
                                    appendText("Same mouse can't be registered twice!");
                                    break;
                                }
                            }

                            sprintf(gbuf, "MOUSE %d: OK! [0x%08x]", mouseID + 1, (unsigned int)mouseHID[mouseID]);
                            appendText(gbuf);

                            mouseID++;
                            if (mouseID >= MAX_MICE) {
                                appendText("DONE! You can minimize this window now.");

                                progMode = 1;
                            }
                        }
                        break;
                    }

                    case 1:
                    {
                        /* Accept input only from registered devices */
                        if (checkMouse(rdev->header.hDevice)) {

                            /* Button 1 trigger */
                            if (rdev->data.mouse.usButtonFlags == RI_MOUSE_BUTTON_1_DOWN) {
                                SetBtn(1, interfaceID[mouseID], 1);
                            }

                            if (rdev->data.mouse.usButtonFlags == RI_MOUSE_BUTTON_1_UP) {
                                SetBtn(0, interfaceID[mouseID], 1);
                            }

                            /* Button 2 trigger */
                            if (rdev->data.mouse.usButtonFlags == RI_MOUSE_BUTTON_2_DOWN) {
                                SetBtn(1, interfaceID[mouseID], 2);
                            }

                            if (rdev->data.mouse.usButtonFlags == RI_MOUSE_BUTTON_2_UP) {
                                SetBtn(0, interfaceID[mouseID], 2);
                            }

                            /* Movement handling */
                            /* Normalize dx and dy */
                            int dxVal = (int)(normalize((int)rdev->data.mouse.lLastX, mouseDPI[mouseID]) * AXIS_MID);
                            int dyVal = (int)(normalize((int)rdev->data.mouse.lLastY, mouseDPI[mouseID]) * AXIS_MID);

                            if (debugMode) {
                                sprintf(gbuf, "[%d] dx: %d | dy: %d", mouseID, (int)rdev->data.mouse.lLastX, (int)rdev->data.mouse.lLastY);
                                appendText(gbuf);
                            }

                            joyApplyForce(dxVal, dyVal, mouseID);
                        }
                        break;
                    }

                    default:
                        progMode = 0;
                        warnUser(hWnd, "Unknown program mode...");
                        break;
                }
            }

            free(pData);
            break;
        }

	    /* Draw window */
        case WM_PAINT:
        {
            PAINTSTRUCT ps;

            HDC hDC    = BeginPaint(hWnd, &ps);
            HDC srcDC  = CreateCompatibleDC(hDC);

            SelectObject(srcDC, bannerBMP);
            BitBlt(hDC, 235, 178, 125, 85, srcDC, 0, 0, SRCCOPY);

            DeleteDC(srcDC);
            DeleteDC(hDC);
            EndPaint(hWnd, &ps);
            break;
        }

        /* UI appearance */
        case WM_CTLCOLORSTATIC:
        {
            char buf[256];
            if (GetClassNameA((HWND)lParam, buf, 256)) {
                if (strcmp(buf, WC_EDITA) == 0) {
                    HDC hDC = (HDC)wParam;
                    SetBkMode(hDC, TRANSPARENT);
                    SetTextColor(hDC, 0x00FFFFFF);
                    SetDCBrushColor(hDC, 0x00929292);
                    return (LRESULT)GetStockObject(DC_BRUSH);
                }
            }
            break;
        }

        /* Close button */
        case WM_CLOSE:
            DestroyWindow(hWnd);
        break;

        /* Quit */
        case WM_DESTROY:
            PostQuitMessage(0);
        break;
	}

	/* Return from WinProc */
	return DefWindowProc(hWnd, msg, wParam, lParam);
}
