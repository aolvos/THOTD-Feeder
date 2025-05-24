#include "common.h"

void fatalError(HWND hWnd, char* msg) {
    char buf[256];

    sprintf(buf, "%s\nApplication will be terminated.", msg);
    MessageBoxA(hWnd, buf, "FATAL ERROR!", MB_ICONERROR | MB_OK);

    /* Kill Process */
    ExitProcess(0);
}

void warnUser(HWND hWnd, char* msg) {
    char buf[256];

    sprintf(buf, "%s\nApplication will continue execution.", msg);
    MessageBoxA(hWnd, buf, "WARNING!", MB_ICONEXCLAMATION | MB_OK);
}
