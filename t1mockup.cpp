#include <windows.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <imm.h>
#include <assert.h>
#include "immdev.h"
#include "resource.h"

#define T1_CLASSNAMEW L"SoftKBDClsT1"
#define C1_CLASSNAMEW L"SoftKBDClsC1"

#ifdef MOCKUP
    typedef struct
    { 
        UINT uCount; 
        WORD wCode[1][256];
    } SOFTKBDDATA;

    #define IMC_GETSOFTKBDFONT 0x0011
    #define IMC_SETSOFTKBDFONT 0x0012
    #define IMC_GETSOFTKBDPOS 0x0013
    #define IMC_SETSOFTKBDPOS 0x0014
    #define IMC_GETSOFTKBDSUBTYPE 0x0015
    #define IMC_SETSOFTKBDSUBTYPE 0x0016
    #define IMC_SETSOFTKBDDATA 0x0018

    #ifndef min
        #define min(x1, x2) (((x1) < (x2)) ? (x1) : (x2))
    #endif

    #define ImmCreateSoftKeyboard T1_CreateSoftKeyboard
    #define ImmShowSoftKeyboard T1_ShowSoftKeyboard
    #define ImmDestroySoftKeyboard T1_DestroySoftKeyboard
#endif

// Define internal codes
#undef DEFINE_T1_KEY
#define DEFINE_T1_KEY(internal_code, virtual_key_code, internal_code_name, virtual_key_name, is_special_code) \
    internal_code_name = internal_code,
typedef enum INTERNAL_CODE
{
#include "t1keys.h"
} INTERNAL_CODE;

// Define mapping: Internal Code --> Virtual Key
#undef DEFINE_T1_KEY
#define DEFINE_T1_KEY(internal_code, virtual_key_code, internal_code_name, virtual_key_name, is_special_code) \
    virtual_key_code,

// Win: bSKT1VirtKey
const UINT gT1VirtKey[60] =
{
#include "t1keys.h"
};

HINSTANCE ghInst = NULL;
BOOL g_bWantSoftKBDMetrics = TRUE;
UINT guScanCode[256];
INT g_cxWork = 0, g_cyWork = 0;
POINT gptRaiseEdge;

typedef struct T1WINDOW
{
    INT cxKeyWidthDefault; // Normal button width
    INT cxWidth47;            // [BackSpace] width
    INT cxWidth48;            // [Tab] width
    INT cxWidth49;            // [Caps] width
    INT cxWidth50;            // [Enter] width
    INT cxWidth51or52;        // [Shift] width
    INT cxWidth53or54;        // [Ctrl] width
    INT cxWidth55or56;        // [Alt] width
    INT cxWidth57;            // [Esc] width
    INT cxWidth58;            // [Space] width
    INT cyKeyHeight;          // Normal button height
    INT cyHeight50;           // [Enter] height
    POINT KeyPos[60];         // Internal Code --> POINT
    WCHAR chKeyChar[48];      // Internal Code --> WCHAR
    HBITMAP hbmKeyboard;      // The keyboard image
    DWORD CharSet;            // LOGFONT.lfCharSet
    UINT PressedKey;          // Currently pressed button
    POINT pt0, pt1;           // The soft keyboard window position
    DWORD KeyboardSubType;    // See IMC_GETSOFTKBDSUBTYPE/IMC_SETSOFTKBDSUBTYPE
} T1WINDOW, *PT1WINDOW;

LOGFONTW g_lfSKT1Font;

// Win: GetSKT1TextMetric
void T1_GetTextMetric(LPTEXTMETRICW ptm)
{
    WCHAR szText[2];
    SIZE textSize;
    HDC hDC;
    HFONT hFont;
    HGDIOBJ hFontOld;

    szText[0] = 0x4E11; // U+4E11
    szText[1] = UNICODE_NULL;

    hDC = GetDC(NULL);

    ZeroMemory(&g_lfSKT1Font, sizeof(g_lfSKT1Font));
    g_lfSKT1Font.lfHeight = -12;
    g_lfSKT1Font.lfWeight = FW_NORMAL;
    g_lfSKT1Font.lfCharSet = CHINESEBIG5_CHARSET;
    g_lfSKT1Font.lfOutPrecision = OUT_TT_ONLY_PRECIS;
    g_lfSKT1Font.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    g_lfSKT1Font.lfQuality = PROOF_QUALITY;
    g_lfSKT1Font.lfPitchAndFamily = FF_MODERN | FIXED_PITCH;
    hFont = CreateFontIndirectW(&g_lfSKT1Font);
    hFontOld = SelectObject(hDC, hFont);
    GetTextMetricsW(hDC, ptm);

    if (GetTextExtentPoint32W(hDC, szText, 1, &textSize) && ptm->tmMaxCharWidth < textSize.cx)
        ptm->tmMaxCharWidth = textSize.cx;

    DeleteObject(SelectObject(hDC, hFontOld));
    ReleaseDC(NULL, hDC);
}

// Win: InitSKT1ButtonPos
void T1_InitButtonPos(PT1WINDOW pT1)
{
    LONG cxLarge, cyLarge, tmMaxCharWidth;
    TEXTMETRICW tm;
    LONG xKey1, yKey1, xKey2, yKey2, xKey3, yKey3;
    LONG yKey4, xKey4, xKey5, yKey5, xKey6, xKey7;
    INT iKey;

    T1_GetTextMetric(&tm);
    tmMaxCharWidth = tm.tmMaxCharWidth;

    pT1->cxKeyWidthDefault = (2 * tmMaxCharWidth + 12) / 2;
    pT1->cxWidth47 = (2 * tmMaxCharWidth + 12) / 2 + 1;
    pT1->cxWidth49 = (4 * tmMaxCharWidth + 24) / 2 + 3;
    pT1->cxWidth51or52 = (5 * tmMaxCharWidth + 30) / 2 + 5;
    pT1->cxWidth58 = 4 * (3 * tmMaxCharWidth + 18) / 2 + 15;

    cxLarge = (3 * tmMaxCharWidth + 18) / 2;
    cyLarge = tm.tmHeight + 8;

    // widths and heights
    pT1->cxWidth48 = cxLarge + 2;
    pT1->cxWidth50 = cxLarge + 2;
    pT1->cxWidth53or54 = cxLarge + 2;
    pT1->cxWidth55or56 = cxLarge + 2;
    pT1->cyHeight50 = 2 * (tm.tmHeight + 8) + 3;
    pT1->cxWidth57 = cxLarge + 1;
    pT1->cyKeyHeight = cyLarge;

    // First line
    xKey1 = gptRaiseEdge.x + 3;
    yKey1 = gptRaiseEdge.y + 3;
    for (iKey = 0; iKey < T1IC_Q; ++iKey)
    {
        pT1->KeyPos[iKey].x = xKey1;
        pT1->KeyPos[iKey].y = yKey1;
        xKey1 += pT1->cxKeyWidthDefault + 3;
    }
    pT1->KeyPos[T1IC_BACKSPACE].y = yKey1;
    pT1->KeyPos[T1IC_BACKSPACE].x = xKey1;

    // 2nd line
    xKey2 = 3 + gptRaiseEdge.x + pT1->cxWidth48 + 3;
    yKey2 = 3 + yKey1 + cyLarge;
    pT1->KeyPos[T1IC_TAB].x = gptRaiseEdge.x + 3;
    pT1->KeyPos[T1IC_TAB].y = yKey2;
    for (iKey = T1IC_Q; iKey < T1IC_A; ++iKey)
    {
        pT1->KeyPos[iKey].x = xKey2;
        pT1->KeyPos[iKey].y = yKey2;
        xKey2 += pT1->cxKeyWidthDefault + 3;
    }
    pT1->KeyPos[T1IC_ENTER].x = xKey2;
    pT1->KeyPos[T1IC_ENTER].y = yKey2;

    // 3rd line
    xKey3 = gptRaiseEdge.x + 3 + pT1->cxWidth49 + 3;
    yKey3 = yKey2 + cyLarge + 3;
    pT1->KeyPos[T1IC_CAPS].x = gptRaiseEdge.x + 3;
    pT1->KeyPos[T1IC_CAPS].y = yKey3;
    for (iKey = T1IC_A; iKey < T1IC_Z; ++iKey)
    {
        pT1->KeyPos[iKey].x = xKey3;
        pT1->KeyPos[iKey].y = yKey3;
        xKey3 += pT1->cxKeyWidthDefault + 3;
    }

    // 4th line
    xKey4 = gptRaiseEdge.x + pT1->cxWidth51or52 + 3 + 3;
    yKey4 = yKey3 + cyLarge + 3;
    pT1->KeyPos[T1IC_L_SHIFT].x = gptRaiseEdge.x + 3;
    pT1->KeyPos[T1IC_L_SHIFT].y = yKey4;
    for (iKey = T1IC_Z; iKey < T1IC_BACKSPACE; ++iKey)
    {
        pT1->KeyPos[iKey].x = xKey4;
        pT1->KeyPos[iKey].y = yKey4;
        xKey4 += pT1->cxKeyWidthDefault + 3;
    }
    pT1->KeyPos[T1IC_R_SHIFT].y = yKey4;
    pT1->KeyPos[T1IC_R_SHIFT].x = xKey4;

    // 5th line
    xKey5 = gptRaiseEdge.x + 3 + pT1->cxWidth53or54 + 3;
    yKey5 = yKey4 + cyLarge + 3;
    pT1->KeyPos[T1IC_L_CTRL].x = gptRaiseEdge.x + 3;
    pT1->KeyPos[T1IC_L_CTRL].y = yKey5;
    pT1->KeyPos[T1IC_ESCAPE].x = xKey5;
    pT1->KeyPos[T1IC_ESCAPE].y = yKey5;
    pT1->KeyPos[T1IC_L_ALT].x = xKey5 + pT1->cxWidth57 + 3;
    pT1->KeyPos[T1IC_L_ALT].y = yKey5;

    xKey6 = xKey5 + pT1->cxWidth57 + 3 + pT1->cxWidth55or56 + 3;
    xKey7 = xKey6 + pT1->cxWidth58 + 3;

    pT1->KeyPos[T1IC_R_ALT].x = xKey7;
    pT1->KeyPos[T1IC_R_ALT].y = yKey5;

    pT1->KeyPos[T1IC_SPACE].x = xKey6;
    pT1->KeyPos[T1IC_SPACE].y = yKey5;

    pT1->KeyPos[T1IC_R_CTRL].x = xKey7 + pT1->cxWidth57 + pT1->cxWidth55or56 + 6;
    pT1->KeyPos[T1IC_R_CTRL].y = yKey5;
}

// Win: SKT1DrawConvexRect
void T1_DrawConvexRect(HDC hDC, INT x, INT y, INT width, INT height)
{
    HGDIOBJ hPen = GetStockObject(BLACK_PEN);
    HGDIOBJ hLtGrayBrush = GetStockObject(LTGRAY_BRUSH);
    HGDIOBJ hGrayBrush = GetStockObject(GRAY_BRUSH);
    INT x0, y0, y1, y2, dx, dy1, dy2;

    dx = width + 4;
    dy2 = height + 4;
    x0 = x - 2;
    y1 = y - 2 + dy2;

    SelectObject(hDC, hPen);
    SelectObject(hDC, hLtGrayBrush);
    Rectangle(hDC, x0, y - 2, x0 + dx, y1);

    PatBlt(hDC, x0, y - 2, 1, 1, PATCOPY);
    PatBlt(hDC, x0 + dx, y - 2, -1, 1, PATCOPY);
    PatBlt(hDC, x0, y1, 1, -1, PATCOPY);
    PatBlt(hDC, x0 + dx, y1, -1, -1, PATCOPY);

    dx -= 2;
    ++x0;

    dy1 = y - 1;
    y0 = dy2 - 2;
    dy2 = 2 - dy2;
    y2 = y0 + dy1;

    PatBlt(hDC, x0, y0 + dy1, 1, dy2, WHITENESS);
    PatBlt(hDC, x0, dy1, dx, 1, WHITENESS);

    SelectObject(hDC, hGrayBrush);
    PatBlt(hDC, x0, y2, dx, -1, PATCOPY);
    PatBlt(hDC, x0 + dx, y2, -1, dy2, PATCOPY);
}

// Win: SKT1DrawBitmap
void T1_DrawBitmap(HDC hDC, INT x, INT y, INT cx, INT cy, LPCWSTR pszBmpName)
{
    HBITMAP hBitmap = LoadBitmapW(ghInst, pszBmpName);
    HDC hMemDC = CreateCompatibleDC(hDC);
    HGDIOBJ hbmOld = SelectObject(hMemDC, hBitmap);
    BitBlt(hDC, x, y, cx, cy, hMemDC, 0, 0, SRCCOPY);
    SelectObject(hMemDC, hbmOld);
    DeleteObject(hBitmap);
    DeleteDC(hMemDC);
}

// Win: SKT1DrawLabel
void T1_DrawLabels(HDC hDC, PT1WINDOW pT1, LPCWSTR pszBmpName)
{
    HBITMAP hBitmap = LoadBitmapW(ghInst, pszBmpName);
    HDC hdcMem = CreateCompatibleDC(hDC);
    HGDIOBJ hbmOld = SelectObject(hdcMem, hBitmap);
    INT iKey;
    for (iKey = 0; iKey < T1IC_BACKSPACE; ++iKey)
    {
        LPPOINT ppt = &pT1->KeyPos[iKey];
        BitBlt(hDC, ppt->x, ppt->y, 8, 8, hdcMem, iKey * 8, 0, SRCCOPY);
    }
    SelectObject(hdcMem, hbmOld);
    DeleteDC(hdcMem);
    DeleteObject(hBitmap);
}

// Win: InitSKT1Bitmap
void T1_InitBitmap(HWND hWnd, PT1WINDOW pT1)
{
    HDC hDC, hMemDC;
    HGDIOBJ hNullPen = GetStockObject(NULL_PEN);
    HGDIOBJ hbrLtGray = GetStockObject(LTGRAY_BRUSH);
    RECT rc;
    INT iKey;

    hDC = GetDC(hWnd);
    hMemDC = CreateCompatibleDC(hDC);
    GetClientRect(hWnd, &rc);
    pT1->hbmKeyboard = CreateCompatibleBitmap(hDC, rc.right - rc.left, rc.bottom - rc.top);
    ReleaseDC(hWnd, hDC);

    // Draw keyboard frame
    SelectObject(hMemDC, pT1->hbmKeyboard);
    SelectObject(hMemDC, hNullPen);
    SelectObject(hMemDC, hbrLtGray);
    Rectangle(hMemDC, rc.left, rc.top, rc.right + 1, rc.bottom + 1);
    DrawEdge(hMemDC, &rc, EDGE_RAISED, BF_RECT);

    // 53 --> Left [Ctrl]
    T1_DrawConvexRect(
        hMemDC,
        pT1->KeyPos[T1IC_L_CTRL].x, pT1->KeyPos[T1IC_L_CTRL].y,
        pT1->cxWidth53or54, pT1->cyKeyHeight);
    T1_DrawBitmap(
        hMemDC,
        pT1->cxWidth53or54 / 2 + pT1->KeyPos[T1IC_L_CTRL].x - 8,
        pT1->cyKeyHeight   / 2 + pT1->KeyPos[T1IC_L_CTRL].y - 4,
        16, 9, MAKEINTRESOURCEW(IDB_T1_CTRL));

    // 54 --> Right [Ctrl]
    T1_DrawConvexRect(
        hMemDC,
        pT1->KeyPos[T1IC_R_CTRL].x, pT1->KeyPos[T1IC_R_CTRL].y,
        pT1->cxWidth53or54, pT1->cyKeyHeight);
    T1_DrawBitmap(
        hMemDC,
        pT1->cxWidth53or54 / 2 + pT1->KeyPos[T1IC_R_CTRL].x - 8,
        pT1->cyKeyHeight   / 2 + pT1->KeyPos[T1IC_R_CTRL].y - 4,
        16, 9, MAKEINTRESOURCEW(IDB_T1_CTRL));

    // 57 --> [Esc]
    T1_DrawConvexRect(
        hMemDC,
        pT1->KeyPos[T1IC_ESCAPE].x, pT1->KeyPos[T1IC_ESCAPE].y,
        pT1->cxWidth57, pT1->cyKeyHeight);
    T1_DrawBitmap(
        hMemDC,
        pT1->cxWidth57   / 2 + pT1->KeyPos[T1IC_ESCAPE].x - 9,
        pT1->cyKeyHeight / 2 + pT1->KeyPos[T1IC_ESCAPE].y - 4,
        18, 9, MAKEINTRESOURCEW(IDB_T1_ESCAPE));

    // 55 --> Left [Alt]
    T1_DrawConvexRect(
        hMemDC,
        pT1->KeyPos[T1IC_L_ALT].x, pT1->KeyPos[T1IC_L_ALT].y,
        pT1->cxWidth55or56, pT1->cyKeyHeight);
    T1_DrawBitmap(
        hMemDC,
        pT1->cxWidth55or56 / 2 + pT1->KeyPos[T1IC_L_ALT].x - 8,
        pT1->cyKeyHeight   / 2 + pT1->KeyPos[T1IC_L_ALT].y - 4,
        16, 9, MAKEINTRESOURCEW(IDB_T1_ALT));

    // 56 --> Right [Alt]
    T1_DrawConvexRect(
        hMemDC,
        pT1->KeyPos[T1IC_R_ALT].x, pT1->KeyPos[T1IC_R_ALT].y,
        pT1->cxWidth55or56, pT1->cyKeyHeight);
    T1_DrawBitmap(
        hMemDC,
        pT1->cxWidth55or56 / 2 + pT1->KeyPos[T1IC_R_ALT].x - 8,
        pT1->cyKeyHeight   / 2 + pT1->KeyPos[T1IC_R_ALT].y - 4,
        16, 9, MAKEINTRESOURCEW(IDB_T1_ALT));

    // 58 --> [Space]
    T1_DrawConvexRect(
        hMemDC,
        pT1->KeyPos[T1IC_SPACE].x, pT1->KeyPos[T1IC_SPACE].y,
        pT1->cxWidth58, pT1->cyKeyHeight);

    // 51 --> Left [Shift]
    T1_DrawConvexRect(
        hMemDC,
        pT1->KeyPos[T1IC_L_SHIFT].x, pT1->KeyPos[T1IC_L_SHIFT].y,
        pT1->cxWidth51or52, pT1->cyKeyHeight);
    T1_DrawBitmap(
        hMemDC,
        pT1->cxWidth51or52 / 2 + pT1->KeyPos[T1IC_L_SHIFT].x - 11,
        pT1->cyKeyHeight   / 2 + pT1->KeyPos[T1IC_L_SHIFT].y - 4,
        23, 9, MAKEINTRESOURCEW(IDB_T1_SHIFT));

    // 52 --> Right [Shift]
    T1_DrawConvexRect(
        hMemDC,
        pT1->KeyPos[T1IC_R_SHIFT].x, pT1->KeyPos[T1IC_R_SHIFT].y,
        pT1->cxWidth51or52, pT1->cyKeyHeight);
    T1_DrawBitmap(
        hMemDC,
        pT1->cxWidth51or52 / 2 + pT1->KeyPos[T1IC_R_SHIFT].x - 11,
        pT1->cyKeyHeight   / 2 + pT1->KeyPos[T1IC_R_SHIFT].y - 4,
        23, 9, MAKEINTRESOURCEW(IDB_T1_SHIFT));

    // 49 --> [Caps]
    T1_DrawConvexRect(
        hMemDC,
        pT1->KeyPos[T1IC_CAPS].x, pT1->KeyPos[T1IC_CAPS].y,
        pT1->cxWidth49, pT1->cyKeyHeight);
    T1_DrawBitmap(
        hMemDC,
        pT1->cxWidth49   / 2 + pT1->KeyPos[T1IC_CAPS].x - 11,
        pT1->cyKeyHeight / 2 + pT1->KeyPos[T1IC_CAPS].y - 4,
        22, 9, MAKEINTRESOURCEW(IDB_T1_CAPS));

    // 48 --> [Tab]
    T1_DrawConvexRect(
        hMemDC,
        pT1->KeyPos[T1IC_TAB].x, pT1->KeyPos[T1IC_TAB].y,
        pT1->cxWidth48, pT1->cyKeyHeight);
    T1_DrawBitmap(
        hMemDC,
        pT1->cxWidth48   / 2 + pT1->KeyPos[T1IC_TAB].x - 8,
        pT1->cyKeyHeight / 2 + pT1->KeyPos[T1IC_TAB].y - 4,
        16, 9, MAKEINTRESOURCEW(IDB_T1_TAB));

    // 50 --> [Enter]
    T1_DrawConvexRect(
        hMemDC,
        pT1->KeyPos[T1IC_ENTER].x, pT1->KeyPos[T1IC_ENTER].y,
        pT1->cxWidth50, pT1->cyHeight50);
    T1_DrawBitmap(
        hMemDC,
        pT1->cxWidth50  / 2 + pT1->KeyPos[T1IC_ENTER].x - 13,
        pT1->cyHeight50 / 2 + pT1->KeyPos[T1IC_ENTER].y - 4,
        26, 9, MAKEINTRESOURCEW(IDB_T1_ENTER));

    // 47 --> [BackSpace]
    T1_DrawConvexRect(
        hMemDC,
        pT1->KeyPos[T1IC_BACKSPACE].x, pT1->KeyPos[T1IC_BACKSPACE].y,
        pT1->cxWidth47, pT1->cyKeyHeight);
    T1_DrawBitmap(
        hMemDC,
        pT1->cxWidth47   / 2 + pT1->KeyPos[T1IC_BACKSPACE].x - 8,
        pT1->cyKeyHeight / 2 + pT1->KeyPos[T1IC_BACKSPACE].y - 4,
        16, 9, MAKEINTRESOURCEW(IDB_T1_BACKSPACE));

    // Normal keys
    for (iKey = 0; iKey < T1IC_BACKSPACE; ++iKey)
    {
        LPPOINT ppt = &pT1->KeyPos[iKey];
        T1_DrawConvexRect(hMemDC, ppt->x, ppt->y,
                          pT1->cxKeyWidthDefault, pT1->cyKeyHeight);
    }

    T1_DrawLabels(hMemDC, pT1, MAKEINTRESOURCEW(IDB_T1_CHARS));
    DeleteDC(hMemDC);
}

// Win: CreateT1Window
INT T1_OnCreate(HWND hWnd)
{
    PT1WINDOW pT1;
    HGLOBAL hGlobal = GlobalAlloc(GHND, sizeof(T1WINDOW));
    if (!hGlobal)
        return -1;

    pT1 = (PT1WINDOW)GlobalLock(hGlobal);
    if (!pT1)
    {
        GlobalFree(hGlobal);
        return -1;
    }

    SetWindowLongPtrW(hWnd, 0, (LONG_PTR)hGlobal);
    pT1->pt1.x = -1;
    pT1->pt1.y = -1;
    pT1->PressedKey = T1IC_NONE;
    pT1->CharSet = CHINESEBIG5_CHARSET;

    T1_InitButtonPos(pT1);
    T1_InitBitmap(hWnd, pT1);
    GlobalUnlock(hGlobal);

    return 0;
}

// Win: SKT1DrawDragBorder
void T1_DrawDragBorder(HWND hWnd, LPPOINT ppt1, LPPOINT ppt2)
{
    INT cxBorder = GetSystemMetrics(SM_CXBORDER);
    INT cyBorder = GetSystemMetrics(SM_CYBORDER);
    INT x = ppt1->x - ppt2->x, y = ppt1->y - ppt2->y;
    HGDIOBJ hGrayBrush = GetStockObject(GRAY_BRUSH);
    RECT rc;
    HDC hDisplayDC;

    GetWindowRect(hWnd, &rc);
    hDisplayDC = CreateDCW(L"DISPLAY", NULL, NULL, NULL);
    SelectObject(hDisplayDC, hGrayBrush);
    PatBlt(hDisplayDC, x, y, rc.right - rc.left - cxBorder, cyBorder, PATINVERT);
    PatBlt(hDisplayDC, x, cyBorder + y, cxBorder, rc.bottom - rc.top - cyBorder, PATINVERT);
    PatBlt(hDisplayDC, x + cxBorder, y + rc.bottom - rc.top, rc.right - rc.left - cxBorder, -cyBorder, PATINVERT);
    PatBlt(hDisplayDC, x + rc.right - rc.left, y, -cxBorder, rc.bottom - rc.top - cyBorder, PATINVERT);
    DeleteDC(hDisplayDC);
}

// Win: DestroyT1Window
void T1_OnDestroy(HWND hWnd)
{
    HGLOBAL hGlobal;
    PT1WINDOW pT1;
#ifndef MOCKUP
    HWND hwndOwner;
#endif

    hGlobal = (HGLOBAL)GetWindowLongPtrW(hWnd, 0);
    if (!hGlobal)
        return;

    pT1 = (PT1WINDOW)GlobalLock(hGlobal);
    if (!pT1)
        return;

    if (pT1->pt1.x != -1 && pT1->pt1.y != -1)
        T1_DrawDragBorder(hWnd, &pT1->pt0, &pT1->pt1);

    DeleteObject(pT1->hbmKeyboard);
    GlobalUnlock(hGlobal);
    GlobalFree(hGlobal);

#ifdef MOCKUP
    PostQuitMessage(0);
#else
    hwndOwner = GetWindow(hWnd, GW_OWNER);
    if (hwndOwner)
        SendMessageW(hwndOwner, WM_IME_NOTIFY, IMN_SOFTKBDDESTROYED, 0);
#endif
}

// Win: SKT1InvertButton
void T1_InvertButton(HWND hWnd, HDC hDC, PT1WINDOW pT1, UINT iPressed)
{
    INT cxWidth = 0, cyHeight = pT1->cyKeyHeight;
    HDC hChoiceDC;

    if (iPressed >= T1IC_NONE)
        return;

    if (hDC)
        hChoiceDC = hDC;
    else
        hChoiceDC = GetDC(hWnd);

    if (iPressed >= T1IC_BACKSPACE)
    {
        switch (iPressed)
        {
            case T1IC_BACKSPACE:
                cxWidth = pT1->cxWidth47;
                break;
            case T1IC_TAB:
                cxWidth = pT1->cxWidth48;
                break;
            case T1IC_ENTER:
                pT1 = pT1;
                cxWidth = pT1->cxWidth50;
                cyHeight = pT1->cyHeight50;
                break;
            case T1IC_ESCAPE:
                cxWidth = pT1->cxWidth57;
                break;
            case T1IC_SPACE:
                cxWidth = pT1->cxWidth58;
                break;
            case T1IC_L_SHIFT:
            case T1IC_R_SHIFT:
            case T1IC_L_CTRL:
            case T1IC_R_CTRL:
            case T1IC_L_ALT:
            case T1IC_R_ALT:
            default:
                cxWidth = 0;
                MessageBeep(0xFFFFFFFF);
                break;
        }
    }
    else
    {
        cxWidth = pT1->cxKeyWidthDefault;
    }

    if (cxWidth > 0)
    {
        PatBlt(hChoiceDC,
               pT1->KeyPos[iPressed].x - 1,
               pT1->KeyPos[iPressed].y - 1,
               cxWidth + 2, cyHeight + 2,
               DSTINVERT);
    }

    if (!hDC)
        ReleaseDC(hWnd, hChoiceDC);
}

// Win: UpdateSKT1Window
void T1_OnDraw(HDC hDC, HWND hWnd)
{
    HGLOBAL hGlobal;
    PT1WINDOW pT1;
    HDC hMemDC;
    RECT rc;

    hGlobal = (HGLOBAL)GetWindowLongPtrW(hWnd, 0);
    if (!hGlobal)
        return;

    pT1 = (PT1WINDOW)GlobalLock(hGlobal);
    if (!pT1)
        return;

    hMemDC = CreateCompatibleDC(hDC);
    SelectObject(hMemDC, pT1->hbmKeyboard);
    GetClientRect(hWnd, &rc);
    BitBlt(hDC, 0, 0, rc.right - rc.left, rc.bottom - rc.top, hMemDC, 0, 0, SRCCOPY);
    DeleteDC(hMemDC);

    if (pT1->PressedKey < T1IC_NONE)
        T1_InvertButton(hWnd, hDC, pT1, pT1->PressedKey);

    GlobalUnlock(hGlobal);
}

// Win: ImmPtInRect
static inline BOOL ImmPtInRect(LONG x, LONG y, LONG cx, LONG cy, const POINT *ppt)
{
    return (x <= ppt->x) && (ppt->x < x + cx) && (y <= ppt->y) && (ppt->y < y + cy);
}

// Win: SKT1MousePosition
UINT T1_HitTest(PT1WINDOW pT1, const POINT *ppt)
{
    INT iKey;

    for (iKey = 0; iKey < T1IC_BACKSPACE; ++iKey)
    {
        LPPOINT pptKey = &pT1->KeyPos[iKey];
        if (ImmPtInRect(pptKey->x, pptKey->y, pT1->cxKeyWidthDefault, pT1->cyKeyHeight, ppt))
            return iKey;
    }

    if (ImmPtInRect(pT1->KeyPos[T1IC_BACKSPACE].x, pT1->KeyPos[T1IC_BACKSPACE].y, pT1->cxWidth47, pT1->cyKeyHeight, ppt))
        return T1IC_BACKSPACE;
    if (ImmPtInRect(pT1->KeyPos[T1IC_TAB].x, pT1->KeyPos[T1IC_TAB].y, pT1->cxWidth48, pT1->cyKeyHeight, ppt))
        return T1IC_TAB;
    if (ImmPtInRect(pT1->KeyPos[T1IC_CAPS].x, pT1->KeyPos[T1IC_CAPS].y, pT1->cxWidth49, pT1->cyKeyHeight, ppt))
        return T1IC_CAPS;
    if (ImmPtInRect(pT1->KeyPos[T1IC_ENTER].x, pT1->KeyPos[T1IC_ENTER].y, pT1->cxWidth50, pT1->cyHeight50, ppt))
        return T1IC_ENTER;

    if (ImmPtInRect(pT1->KeyPos[T1IC_L_SHIFT].x, pT1->KeyPos[T1IC_L_SHIFT].y, pT1->cxWidth51or52, pT1->cyKeyHeight, ppt) ||
        ImmPtInRect(pT1->KeyPos[T1IC_R_SHIFT].x, pT1->KeyPos[T1IC_R_SHIFT].y, pT1->cxWidth51or52, pT1->cyKeyHeight, ppt))
    {
        return T1IC_L_SHIFT;
    }

    if (ImmPtInRect(pT1->KeyPos[T1IC_L_CTRL].x, pT1->KeyPos[T1IC_L_CTRL].y, pT1->cxWidth53or54, pT1->cyKeyHeight, ppt) ||
        ImmPtInRect(pT1->KeyPos[T1IC_R_CTRL].x, pT1->KeyPos[T1IC_R_CTRL].y, pT1->cxWidth53or54, pT1->cyKeyHeight, ppt))
    {
        return T1IC_L_CTRL;
    }

    if (ImmPtInRect(pT1->KeyPos[T1IC_L_ALT].x, pT1->KeyPos[T1IC_L_ALT].y, pT1->cxWidth55or56, pT1->cyKeyHeight, ppt) ||
        ImmPtInRect(pT1->KeyPos[T1IC_R_ALT].x, pT1->KeyPos[T1IC_R_ALT].y, pT1->cxWidth55or56, pT1->cyKeyHeight, ppt))
    {
        return T1IC_L_ALT;
    }

    if (ImmPtInRect(pT1->KeyPos[T1IC_ESCAPE].x, pT1->KeyPos[T1IC_ESCAPE].y, pT1->cxWidth57, pT1->cyKeyHeight, ppt))
        return T1IC_ESCAPE;

    if (ImmPtInRect(pT1->KeyPos[T1IC_SPACE].x, pT1->KeyPos[T1IC_SPACE].y, pT1->cxWidth58, pT1->cyKeyHeight, ppt))
        return T1IC_SPACE;

    return T1IC_NONE;
}

// Win: SKT1IsValidButton
BOOL T1_IsValidButton(UINT iKey, PT1WINDOW pT1)
{
    if (iKey < T1IC_BACKSPACE)
        return !!pT1->chKeyChar[iKey];
    return iKey <= T1IC_TAB || iKey == T1IC_ENTER || (T1IC_ESCAPE <= iKey && iKey <= T1IC_SPACE);
}

// Win: GetAllMonitorSize
void Imm32GetAllMonitorSize(LPRECT prcWork)
{
    if (GetSystemMetrics(SM_CMONITORS) == 1)
    {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, prcWork, 0);
        return;
    }

    prcWork->left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    prcWork->top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    prcWork->right = prcWork->left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    prcWork->bottom = prcWork->top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
}

// Win: GetNearestMonitorSize
BOOL Imm32GetNearestMonitorSize(HWND hwnd, LPRECT prcWork)
{
    HMONITOR hMonitor;
    MONITORINFO mi;

    if (GetSystemMetrics(SM_CMONITORS) == 1)
    {
        Imm32GetAllMonitorSize(prcWork);
        return TRUE;
    }

    hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (!hMonitor)
        return FALSE;

    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(hMonitor, &mi);
    *prcWork = mi.rcWork;
    return TRUE;
}

// Win: SKT1SetCursor
BOOL T1_OnSetCursor(HWND hWnd, LPARAM lParam)
{
    HGLOBAL hGlobal;
    PT1WINDOW pT1;
    HCURSOR hCursor;
    UINT iPressed;
    RECT rcWork, rc;
    UINT iKey;

    hGlobal = (HGLOBAL)GetWindowLongPtrW(hWnd, 0);
    if (!hGlobal)
        return FALSE;

    pT1 = (PT1WINDOW)GlobalLock(hGlobal);
    if (!pT1)
        return FALSE;

    if (pT1->pt1.x != -1 && pT1->pt1.y != -1)
    {
        SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_SIZEALL));
        GlobalUnlock(hGlobal);
        return TRUE;
    }

    GetCursorPos(&pT1->pt0);
    ScreenToClient(hWnd, &pT1->pt0);

    iKey = T1_HitTest(pT1, &pT1->pt0);
    if (iKey >= T1IC_NONE)
        hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_SIZEALL);
    else
        hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_HAND);
    SetCursor(hCursor);

    if (HIWORD(lParam) == WM_LBUTTONDOWN)
    {
        SetCapture(hWnd);
        iPressed = pT1->PressedKey;
        if (iPressed < T1IC_NONE)
        {
            keybd_event(gT1VirtKey[iPressed],
                        guScanCode[(BYTE)gT1VirtKey[iPressed]],
                        KEYEVENTF_KEYUP,
                        0);
            T1_InvertButton(hWnd, NULL, pT1, pT1->PressedKey);
            pT1->PressedKey = T1IC_NONE;
        }

        if (iKey >= T1IC_NONE)
        {
            Imm32GetAllMonitorSize(&rcWork);
            GetCursorPos(&pT1->pt0);
            GetWindowRect(hWnd, &rc);
            pT1->pt1.x = pT1->pt0.x - rc.left;
            pT1->pt1.y = pT1->pt0.y - rc.top;
            T1_DrawDragBorder(hWnd, &pT1->pt0, &pT1->pt1);
        }
        else if (T1_IsValidButton(iKey, pT1))
        {
            keybd_event(gT1VirtKey[iKey],
                        guScanCode[(BYTE)gT1VirtKey[iKey]],
                        0, 0);
            pT1->PressedKey = iKey;
            T1_InvertButton(hWnd, 0, pT1, iKey);
        }
        else
        {
            MessageBeep(0xFFFFFFFF);
        }
    }

    return TRUE;
}

// Win: SKT1MouseMove
BOOL T1_OnMouseMove(HWND hWnd)
{
    BOOL ret = FALSE;
    HGLOBAL hGlobal;
    PT1WINDOW pT1;

    hGlobal = (HGLOBAL)GetWindowLongPtrW(hWnd, 0);
    if (!hGlobal)
        return FALSE;

    pT1 = (PT1WINDOW)GlobalLock(hGlobal);
    if (!pT1)
        return FALSE;

    if (pT1->pt1.x != -1 && pT1->pt1.y != -1)
    {
        T1_DrawDragBorder(hWnd, &pT1->pt0, &pT1->pt1);
        GetCursorPos(&pT1->pt0);
        T1_DrawDragBorder(hWnd, &pT1->pt0, &pT1->pt1);
        ret = TRUE;
    }

    GlobalUnlock(hGlobal);
    return ret;
}

// Win: SKT1ButtonUp
BOOL T1_OnButtonUp(HWND hWnd)
{
    BOOL ret = FALSE;
    HGLOBAL hGlobal;
    PT1WINDOW pT1;
    INT x, y;
    HWND hwndCapture = GetCapture();
    INT iPressed;
#ifndef MOCKUP
    HIMC hIMC;
    LPINPUTCONTEXT pIC;
    HWND hwndOwner;
#endif

    if (hwndCapture == hWnd)
        ReleaseCapture();

    hGlobal = (HGLOBAL)GetWindowLongPtrW(hWnd, 0);
    if (!hGlobal)
        return FALSE;

    pT1 = (PT1WINDOW)GlobalLock(hGlobal);
    if (!pT1)
        return FALSE;

    iPressed = pT1->PressedKey;
    if (iPressed >= T1IC_NONE)
    {
        if (pT1->pt1.x != -1 && pT1->pt1.y != -1 )
        {
            T1_DrawDragBorder(hWnd, &pT1->pt0, &pT1->pt1);
            x = pT1->pt0.x - pT1->pt1.x;
            y = pT1->pt0.y - pT1->pt1.y;
            SetWindowPos(hWnd, NULL, x, y, 0, 0, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE);
            pT1->pt1.x = pT1->pt1.y = -1;
            pT1->PressedKey = T1IC_NONE;
            ret = TRUE;

#ifndef MOCKUP
            hwndOwner = GetWindow(hWnd, GW_OWNER);
            hIMC = (HIMC)GetWindowLongPtrW(hwndOwner, 0);
            if (hIMC)
            {
                pIC = ImmLockIMC(hIMC);
                if (pIC)
                {
                    pIC->fdwInit |= INIT_SOFTKBDPOS;
                    pIC->ptSoftKbdPos.x = x;
                    pIC->ptSoftKbdPos.y = y;
                    ImmUnlockIMC(hIMC);
                }
            }
#endif
        }
    }
    else
    {
        keybd_event(gT1VirtKey[iPressed],
                    guScanCode[(BYTE)gT1VirtKey[iPressed]],
                    KEYEVENTF_KEYUP,
                    0);

        T1_InvertButton(hWnd, 0, pT1, pT1->PressedKey);
        pT1->PressedKey = T1IC_NONE;
        ret = TRUE;
    }

    GlobalUnlock(hGlobal);
    return ret;
}

// Win: SetSKT1Data
HRESULT T1_SetData(HWND hWnd, SOFTKBDDATA *pData)
{
    HGLOBAL hGlobal;
    PT1WINDOW pT1; // ebx
    HDC hDC, hMemDC;
    HFONT hFont;
    HGDIOBJ hFontOld;
    RECT rc;
    LOGFONTW lf;
    INT iKey;

    hGlobal = (HGLOBAL)GetWindowLongPtrW(hWnd, 0);
    if (!hGlobal)
        return E_FAIL;

    pT1 = (PT1WINDOW)GlobalLock(hGlobal);
    if (!pT1)
        return E_FAIL;

    hDC = GetDC(hWnd);
    hMemDC = CreateCompatibleDC(hDC);
    ReleaseDC(hWnd, hDC);

    SelectObject(hMemDC, pT1->hbmKeyboard);
    SetBkColor(hMemDC, RGB(192, 192, 192));

    if (pT1->CharSet == DEFAULT_CHARSET)
    {
        hFont = CreateFontIndirectW(&g_lfSKT1Font);
    }
    else
    {
        lf = g_lfSKT1Font;
        lf.lfCharSet = (BYTE)pT1->CharSet;
        hFont = CreateFontIndirectW(&lf);
    }
    hFontOld = SelectObject(hMemDC, hFont);

    for (iKey = 0; iKey < T1IC_BACKSPACE; ++iKey)
    {
        WCHAR& wch = pT1->chKeyChar[iKey];
        INT x0 = pT1->KeyPos[iKey].x, y0 = pT1->KeyPos[iKey].y;
        INT x = x0 + 6, y = y0 + 8;
        wch = pData->wCode[0][(BYTE)gT1VirtKey[iKey]];
        rc.left = x;
        rc.top = y;
        rc.right = x0 + pT1->cxKeyWidthDefault;
        rc.bottom = y0 + pT1->cyKeyHeight;
        ExtTextOutW(hDC, x, y, ETO_OPAQUE, &rc, &wch, !!wch, NULL);
    }

    DeleteObject(SelectObject(hMemDC, hFontOld));
    DeleteDC(hMemDC);
    GlobalUnlock(hGlobal);
    return S_OK;
}

// Win: (None)
LRESULT T1_OnImeControl(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    LRESULT ret = 1;
    PT1WINDOW pT1;
    HGLOBAL hGlobal;

    switch (wParam)
    {
        case IMC_GETSOFTKBDFONT:
        {
            hGlobal = (HGLOBAL)GetWindowLongPtrW(hWnd, 0);
            if (hGlobal)
            {
                pT1 = (PT1WINDOW)GlobalLock(hGlobal);
                if (pT1)
                {
                    LPLOGFONTW plf = (LPLOGFONTW)lParam;
                    DWORD CharSet = pT1->CharSet;
                    GlobalUnlock(hGlobal);

                    *plf = g_lfSKT1Font;
                    if (CharSet != DEFAULT_CHARSET)
                        plf->lfCharSet = (BYTE)CharSet;

                    ret = 0;
                }
            }
            break;
        }
        case IMC_SETSOFTKBDFONT:
        {
            const LOGFONTW *plf = (LPLOGFONTW)lParam;
            if (g_lfSKT1Font.lfCharSet == plf->lfCharSet)
                return 0;

            hGlobal = (HGLOBAL)GetWindowLongPtrW(hWnd, 0);
            pT1 = (PT1WINDOW)GlobalLock(hGlobal);
            if (pT1)
            {
                pT1->CharSet = plf->lfCharSet;
                GlobalUnlock(hGlobal);
                return 0;
            }

            break;
        }
        case IMC_GETSOFTKBDPOS:
        {
            RECT rc;
            GetWindowRect(hWnd, &rc);
            return MAKELRESULT(rc.left, rc.top);
        }
        case IMC_SETSOFTKBDPOS:
        {
            POINT pt;
#ifndef MOCKUP
            HWND hwndParent;
#endif

            POINTSTOPOINT(pt, lParam);
            SetWindowPos(hWnd, NULL, pt.x, pt.y, 0, 0,
                         SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE);

#ifndef MOCKUP
            hwndParent = GetParent(hWnd);
            if (hwndParent)
            {
                HIMC hIMC = (HIMC)GetWindowLongPtrW(hwndParent, 0);
                if (hIMC)
                {
                    LPINPUTCONTEXT pIC = ImmLockIMC(hIMC);
                    if (pIC)
                    {
                        pIC->ptSoftKbdPos.x = pt.x;
                        pIC->ptSoftKbdPos.y = pt.y;
                        ImmUnlockIMC(hIMC);
                        return 0;
                    }
                }
            }
#endif
            break;
        }
        case IMC_GETSOFTKBDSUBTYPE:
        case IMC_SETSOFTKBDSUBTYPE:
        {
            hGlobal = (HGLOBAL)GetWindowLongPtrW(hWnd, 0);
            if (!hGlobal)
                return -1;

            pT1 = (PT1WINDOW)GlobalLock(hGlobal);
            if (!pT1)
                return -1;

            if (wParam == IMC_GETSOFTKBDSUBTYPE)
            {
                ret = pT1->KeyboardSubType;
            }
            else
            {
                DWORD *pSubType = &pT1->KeyboardSubType;
                ret = *pSubType;
                *pSubType = (DWORD)lParam;
            }
            GlobalUnlock(hGlobal);
            break;
        }
        case IMC_SETSOFTKBDDATA:
        {
            ret = T1_SetData(hWnd, (SOFTKBDDATA*)lParam);
            if (!ret)
            {
                InvalidateRect(hWnd, NULL, FALSE);
                PostMessageW(hWnd, WM_PAINT, 0, 0);
            }
            break;
        }
    }

    return ret;
}

// Win: SKWndProcT1
LRESULT CALLBACK
T1_WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_CREATE:
        {
            return T1_OnCreate(hWnd);
        }
        case WM_DESTROY:
        {
            T1_OnDestroy(hWnd);
            break;
        }
        case WM_SETCURSOR:
        {
            if (T1_OnSetCursor(hWnd, lParam))
                break;
            return DefWindowProcW(hWnd, uMsg, wParam, lParam);
        }
        case WM_MOUSEMOVE:
        {
            if (T1_OnMouseMove(hWnd))
                break;
            return DefWindowProcW(hWnd, uMsg, wParam, lParam);
        }
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hDC = BeginPaint(hWnd, &ps);
            T1_OnDraw(hDC, hWnd);
            EndPaint(hWnd, &ps);
            break;
        }
        case WM_SHOWWINDOW:
        {
            if (!lParam && wParam != SW_SHOWNORMAL)
                T1_OnButtonUp(hWnd);
            return DefWindowProcW(hWnd, uMsg, wParam, lParam);
        }
        case WM_MOUSEACTIVATE:
        {
            return MA_NOACTIVATE;
        }
        case WM_LBUTTONUP:
        {
            if (T1_OnButtonUp(hWnd))
                break;
            return DefWindowProcW(hWnd, uMsg, wParam, lParam);
        }
        case WM_IME_CONTROL:
        {
            return T1_OnImeControl(hWnd, wParam, lParam);
        }
        default:
        {
            return DefWindowProcW(hWnd, uMsg, wParam, lParam);
        }
    }

    return 0;
}

BOOL RegisterSoftKeyboard(UINT uType)
{
    LPCWSTR pszClass;
    WNDCLASSEXW wcx;

    if (uType == 1)
    {
        pszClass = T1_CLASSNAMEW;
    }
    else if (uType == 2)
    {
        pszClass = C1_CLASSNAMEW;
    }
    else
    {
        assert(0);
        return FALSE;
    }

    if (GetClassInfoExW(ghInst, pszClass, &wcx))
        return TRUE;

    ZeroMemory(&wcx, sizeof(wcx));
    wcx.cbSize = sizeof(wcx);
#ifndef MOCKUP
    wcx.style = CS_IME;
#endif
    wcx.cbClsExtra = 0;
    wcx.cbWndExtra = sizeof(PT1WINDOW);
    wcx.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    wcx.hInstance = ghInst;
    wcx.hCursor = LoadCursorW(0, (LPCWSTR)IDC_SIZEALL);
    wcx.lpszClassName = pszClass;

    if (uType == 1)
    {
        wcx.lpfnWndProc = T1_WindowProc;
        wcx.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    }
    else
    {
        wcx.lpfnWndProc = NULL; //SKWndProcC1;
        wcx.hbrBackground = (HBRUSH)GetStockObject(LTGRAY_BRUSH);
    }

    return !!RegisterClassExW(&wcx);
}

void GetSoftKeyboardDimension(UINT uType, LPINT pcx, LPINT pcy)
{
    TEXTMETRICW tm;
    INT cxEdge, cyEdge;

    if (uType == 1)
    {
        T1_GetTextMetric(&tm);
        *pcx = 15 * tm.tmMaxCharWidth + 2 * gptRaiseEdge.x + 139;
        *pcy = 5 * tm.tmHeight + 2 * gptRaiseEdge.y + 58;
    }
    else if (uType == 2)
    {
        cxEdge = GetSystemMetrics(SM_CXEDGE);
        cyEdge = GetSystemMetrics(SM_CXEDGE);
        *pcx = 2 * (GetSystemMetrics(SM_CXBORDER) + cxEdge) + 348;
        *pcy = 2 * (GetSystemMetrics(SM_CXBORDER) + cyEdge) + 136;
    }
}

HWND WINAPI ImmCreateSoftKeyboard(UINT uType, HWND hwndParent, INT x, INT y)
{
#ifndef MOCKUP
    HKL hKL;
    PIMEDPI pImeDpi;
    DWORD UICaps;
#endif
    UINT iVK;
    INT xSoftKBD, ySoftKBD, cxSoftKBD, cySoftKBD, cxEdge, cyEdge;
    HWND hwndSoftKBD;
    RECT rcWork;;
    DWORD Style, ExStyle;
    LPCWSTR pszClass;

    // Sanity check
    if (uType != 1 && uType != 2)
    {
        assert(0);
        return NULL;
    }

#ifndef MOCKUP
    // Check IME for soft keyboard
    hKL = GetKeyboardLayout(0);
    pImeDpi = ImmLockImeDpi(hKL);
    if (!pImeDpi)
    {
        assert(0);
        return NULL;
    }

    UICaps = pImeDpi->ImeInfo.fdwUICaps;
    ImmUnlockImeDpi(pImeDpi);

    if (!(UICaps & UI_CAP_SOFTKBD))
    {
        assert(0);
        return NULL;
    }
#endif

    // Want metrics?
    if (g_bWantSoftKBDMetrics)
    {
        if (!Imm32GetNearestMonitorSize(hwndParent, &rcWork))
        {
            assert(0);
            return NULL;
        }

        for (iVK = 0; iVK < 0xFF; ++iVK)
        {
            guScanCode[iVK] = MapVirtualKeyW(iVK, 0);
        }

        g_cxWork = rcWork.right - rcWork.left;
        g_cyWork = rcWork.bottom - rcWork.top;
        cxEdge = GetSystemMetrics(SM_CXEDGE);
        cyEdge = GetSystemMetrics(SM_CYEDGE);
        gptRaiseEdge.x = GetSystemMetrics(SM_CXBORDER) + cxEdge;
        gptRaiseEdge.y = GetSystemMetrics(SM_CYBORDER) + cyEdge;
        g_bWantSoftKBDMetrics = FALSE;
    }

    if (!RegisterSoftKeyboard(uType))
    {
        assert(0);
        return NULL;
    }

    // Calculate keyboard size
    GetSoftKeyboardDimension(uType, &cxSoftKBD, &cySoftKBD);

    // Calculate keyboard position
    xSoftKBD = ((x >= 0) ? min(x, g_cxWork - cxSoftKBD) : 0);
    ySoftKBD = ((y >= 0) ? min(y, g_cyWork - cySoftKBD) : 0);

    if (uType == 1)
    {
        Style = (WS_POPUP | WS_DISABLED);
        ExStyle = 0;
        pszClass = T1_CLASSNAMEW;
    }
    else
    {
        Style = (WS_POPUP | WS_DISABLED | WS_BORDER);
        ExStyle = WS_EX_WINDOWEDGE | WS_EX_DLGMODALFRAME;
        pszClass = C1_CLASSNAMEW;
    }

    // Create keyboard window
    hwndSoftKBD = CreateWindowExW(ExStyle, pszClass, NULL, Style,
                                  xSoftKBD, ySoftKBD, cxSoftKBD, cySoftKBD,
                                  hwndParent, NULL, ghInst, NULL);

    // Initial is hidden
    ShowWindow(hwndSoftKBD, SW_HIDE);
    UpdateWindow(hwndSoftKBD);

    return hwndSoftKBD;
}

BOOL WINAPI ImmShowSoftKeyboard(HWND hwndSoftKBD, INT nCmdShow)
{
    return hwndSoftKBD && ShowWindow(hwndSoftKBD, nCmdShow);
}

BOOL WINAPI ImmDestroySoftKeyboard(HWND hwndSoftKBD)
{
    return DestroyWindow(hwndSoftKBD);
}

INT WINAPI
WinMain(HINSTANCE   hInstance,
        HINSTANCE   hPrevInstance,
        LPSTR       lpCmdLine,
        INT         nCmdShow)
{
    ghInst = hInstance;
    HWND hwndSoftKBD = ImmCreateSoftKeyboard(1, NULL, 0, 0);
    assert(hwndSoftKBD);

    ImmShowSoftKeyboard(hwndSoftKBD, SW_SHOWNOACTIVATE);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (INT)msg.wParam;
}
