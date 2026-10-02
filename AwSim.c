/*
 * AwSim.c  –  v3.1
 * ═══════════════════════════════════════════════════════════════════
static int sectionY[4] = { 0, SH0, SH0 + SH1, SH0 + SH1 + SH2 };
 *   windres AwSim.rc -O coff -o AwSim.res
 *   gcc -O2 -o AwSim.exe AwSim.c AwSim.res -luser32 -lgdi32 -mwindows
 *
 * Config : <EXE dir>\_AwSim.ini
 * Assets (same directory as AwSim.exe):
 *   AwSimUI-1.bmp  580×70    Header
 *   AwSimUI-2.bmp  580×138   Engines
 *   AwSimUI-3.bmp  580×138   Boxgrid
 *   AwSimUI-4.bmp  580×138   Targets
 *
 * ── View states (< > buttons cycle circularly) ───────────────────
 *   0  Hdr + Eng + Box + Tgt   486 px
 *   1  Hdr + Eng + Box         348 px
 *   2  Hdr + Eng               210 px
 *   3  Hdr + Box               210 px
 *   4  Hdr + Tgt               210 px
 *   5  Hdr only                 72 px
 *
 * ── Engine-vars string (12 chars + NUL) ──────────────────────────
 *   [0]     = '.' or '#'   dot-mode vs number-mode
 *   [1..2]  = CR engines (mutually exclusive):
 *               [1]=AwSim  [2]=Decoy        0=off, else=run-order rank
 *   [3..6]  = FS engines (ranked by selection order):
 *               [3]=Col.Ops [4]=Row Ops [5]=Permute [6]=Scout
 *   [7..10] = Advanced flags: [7]=Permute [8]=Col.Ops
 *               [9]=Row Ops [10]=Scout
 *   [11]    = MPC '1'..'5'
 *   Default: ".10000000003"
 *
 * ── Stop conditions (low-level hook, during automation) ──────────
 *   Right-click anywhere  →  stop automation
 *   Cursor x ≤ 3          →  exit program
 *
 * ── Target buttons ───────────────────────────────────────────────
 *   ×1 click  →  select as active (highlighted)
 *   ×3 clicks →  recapture screen coordinate via crosshair overlay
 */

#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>
#include <string.h>
#include <time.h>

#define APP_NAME   "AwSim"
#define APP_VER    "3.1"
#define APP_SCHEMA 1
#define WIN_W      580
#define EDGE_X       3       /* cursor x ≤ this  →  exit             */
#define INI_UNSET  -32768
#define INI_SEC    "Settings"
#define IDI_APP      1

/* ── Section client heights ───────────────────────────────────── */
#define SH0   72    /* Header  (BMP=70 px; 2 px black gap below)    */
#define SH1  138    /* Engines                                       */
#define SH2  138    /* Boxgrid                                       */
#define SH3  138    /* Targets                                       */

/* ── Grid ─────────────────────────────────────────────────────── */
#define GCOLS   9
#define GROWS   4
#define NCELLS  36   /* GCOLS × GROWS                               */
#define NTGTS    5
#define BOX_RESERVE  0
#define BOX_ACTIVE   1
#define BOX_DISABLED 2

/* ── Window / class names ─────────────────────────────────────── */
#define CLS_MAIN  "AwSimMain"
#define CLS_CAP   "AwSimCap"
#define CLS_HELP  "AwSimHelp"

/* ── Custom messages ─────────────────────────────────────────── */
#define WM_STOPAUTO    (WM_APP + 1)
#define WM_EXITAPP     (WM_APP + 2)
#define WM_STARTDEFER  (WM_APP + 3)
#define WM_XEROXDEFER  (WM_APP + 4)
#define WM_TASKBARPROMPT (WM_APP + 5)

/* ── Button flash IDs (bit positions in flashMask) ──────────── */
#define FLASH_MPC    (1u<<5)   /* Sec2 MPC + button                */
#define FLASH_DN     (1u<<6)   /* Sec2 #/. toggle                  */
#define FLASH_RST    (1u<<7)   /* Sec2 Reset button                */
#define FLASH_S3B3   (1u<<8)   /* Sec3 Toggle Presets button      */
#define FLASH_S3B4   (1u<<9)   /* Sec3 XEROX          button      */
#define IDT_FLASH    1         /* WM_TIMER ID for flash clear      */
#define IDT_STOP_POLL 2        /* poll worker completion while stopping */

/* ═══════════════════════════════════════════════════════════════
   HOTSPOT COORDINATES  — measured from click-overlay images.
   All y-values are RELATIVE to the section's top edge (y=0).
═══════════════════════════════════════════════════════════════ */

/* Section 1 – Header ──────────────────────────────────────── */
/* 6 buttons; all share y = 27..64                            */
#define S1_BTN_Y1  27
#define S1_BTN_Y2  64

/* Section 2 – Engines ─────────────────────────────────────── */
/* CR: 2 buttons at fixed y-centres, shared x column          */
#define S2_CR_CX     30   /* indicator draw x                  */
#define S2_CR1_Y     41   /* AwSim centre y                    */
#define S2_CR2_Y    109   /* Decoy  centre y                   */
#define S2_CR_XA      8   /* click x left  bound               */
#define S2_CR_XB    177   /* click x right bound               */
/* FS: 4 buttons at shared x, row centres                     */
#define S2_FS_CX    235   /* indicator draw x (was 234, +9)   was 243  */
static const int S2_CY[4] = { 38, 64, 90, 116 };
#define S2_FS_XA    202   /* left half of each engine row       */
#define S2_FS_XB    352
/* Advanced buttons: right half of each engine row              */
#define S2_OD_CX    455   /* indicator draw x (was 459, +7)  was 466   */
#define S2_OD_XA    353   /* click x left bound               */
#define S2_OD_XB    503   /* click x right bound              */
#define S2_ROW_YTOL  12   /* ±px tolerance from row centre   was  12   */
/* number/dot toggle button (far-right top box)              */
#define S2_DN_X1    519
#define S2_DN_X2    557
#define S2_DN_Y1     29
#define S2_DN_Y2     67
/* Reset button (far-right bottom box)                         */
#define S2_RST_X1   519    /* was 522 */
#define S2_RST_X2   557    /* was 560 */
#define S2_RST_Y1    84    /* was  81 */
#define S2_RST_Y2   122    /* was 119 */
/* MPC+ button — diamond flash, half-diag = S2_MPC_R.
   The + button and the digit display swapped sides: the + button (and its
   click hotspot) is now on the right; the digit display is on the left.  */
#define S2_MPC_CX    160   /* + button centre  (x+1)       _OK_  was 153   */
#define S2_MPCDIG_CX 53 /* digit-display diamond centre (x+79) x pos of diamond _OK_ was 112  */
#define S2_MPC_CY    76   /* both diamonds (y+1)         y pos of diamonds _OK_    was 88  */
#define S2_MPC_R     17   /*  size of diamond flash _OK_ was 15  */
#define S2_MPC_X1   (S2_MPC_CX - S2_MPC_R)
#define S2_MPC_X2   (S2_MPC_CX + S2_MPC_R)
#define S2_MPC_Y1   (S2_MPC_CY - S2_MPC_R)
#define S2_MPC_Y2   (S2_MPC_CY + S2_MPC_R)
/* MPC digit display position (DrawDSeg 5x7 at this origin)   */
#define S2_DIGX     140    /*  was 140  */
#define S2_DIGY      81    /*  was 81  */

/* Section 3 – Boxgrid ─────────────────────────────────────── */
/* Cell grid — column x bounds unchanged, row y bounds (centres at y=14,37,60,83) */
static const int S3_CX1[9] = {  12,  74, 136, 198, 260, 322, 384, 446, 508 };
static const int S3_CX2[9] = {  71, 133, 195, 257, 319, 381, 443, 505, 567 };
static const int S3_RY1[4] = {   3,  26,  49,  72 };
static const int S3_RY2[4] = {  25,  48,  71,  94 };
/* Column-toggle click strip (no overlay drawn)                 */
/* Column-toggle strip: thin band along the very top, above the boxgrid.
   Click above column c to toggle that whole column.                */
#define S3_TOG_Y1    0
#define S3_TOG_Y2    6
/* Remaining action buttons below the boxgrid: Toggle Presets and XEROX. */
static const int S3_BTN_X1[2] = {  47, 462 };
static const int S3_BTN_X2[2] = { 135, 550 };
static const int S3_BTN_Y1[2] = { 109, 106 };
static const int S3_BTN_Y2[2] = { 133, 134 };

/* Section 4 – Targets ─────────────────────────────────────── */
static const int S4_X1[5] = {   8, 122, 236, 350, 464 };
static const int S4_X2[5] = { 116, 230, 344, 458, 572 };
#define S4_Y1   5
#define S4_Y2  113

/* ═══════════════════════════════════════════════════════════════
   GLOBAL STATE
═══════════════════════════════════════════════════════════════ */
static HINSTANCE hI;
static HBITMAP   hWork = NULL;        /* "WORKING" overlay bitmap (from resource) */
static HBITMAP   hLetters = NULL;     /* red AWSIM letters overlay (from resource) */
static HWND      wMain = NULL;
static HWND      gameWindow = NULL;
static HWND      wCap  = NULL;
static HWND      wHelp = NULL;   /* Help topics window (Option 1)        */
static HBITMAP   hBmp[4];
static BOOL      debugMode = FALSE;

static const int  SECT_H[4]        = { SH0, SH1, SH2, SH3 };
static const BOOL VIEW_VIS[6][4]   = {
    {1,1,1,1}, {1,1,1,0}, {1,1,0,0},
    {1,0,1,0}, {1,0,0,1}, {1,0,0,0}
};

typedef struct {
    int viewState;
    int sectionY[4];
    char engVars[13];
    BYTE boxState[NCELLS];
    int tgtX[NTGTS], tgtY[NTGTS];
    BOOL tgtSet[NTGTS];
    int activeTgt;
    DWORD tgtLastTick[NTGTS];
    int tgtClickCnt[NTGTS];
    int rb1x, rb1y, rb2x, rb2y;
    BOOL rb1Set, rb2Set;
    int rs12x, rs12y, rs34x, rs34y;
    BOOL rs12Set, rs34Set;
    int gx, gy, gw, gh;
    BOOL gridSet, running, exitPending;
    DWORD flashMask;
    int capMode, capStep;
    POINT capP1;
    BOOL pendingStart, pendingXerox;
    BOOL taskbarPromptPending;
    char iniPath[MAX_PATH];
    int winSX, winSY;
    int presetIdx;
    int helpSel;
} AppState;

static AppState app = {
    .sectionY = { 0, SH0, SH0 + SH1, SH0 + SH1 + SH2 },
    .engVars = ".10000000003",
    .winSX = INI_UNSET,
    .winSY = INI_UNSET,
    .presetIdx = -1
};

#define viewState app.viewState
#define sectionY app.sectionY
#define engVars app.engVars
#define boxState app.boxState
#define tgtX app.tgtX
#define tgtY app.tgtY
#define tgtSet app.tgtSet
#define activeTgt app.activeTgt
#define tgtLastTick app.tgtLastTick
#define tgtClickCnt app.tgtClickCnt
#define rb1x app.rb1x
#define rb1y app.rb1y
#define rb2x app.rb2x
#define rb2y app.rb2y
#define rb1Set app.rb1Set
#define rb2Set app.rb2Set
#define rs12x app.rs12x
#define rs12y app.rs12y
#define rs34x app.rs34x
#define rs34y app.rs34y
#define rs12Set app.rs12Set
#define rs34Set app.rs34Set
#define gx app.gx
#define gy app.gy
#define gw app.gw
#define gh app.gh
#define gridSet app.gridSet
#define running app.running
#define flashMask app.flashMask
#define capMode app.capMode
#define capStep app.capStep
#define capP1 app.capP1
#define pendingStart app.pendingStart
#define pendingXerox app.pendingXerox
#define iniPath app.iniPath
#define winSX app.winSX
#define winSY app.winSY
#define presetIdx app.presetIdx
#define helpSel app.helpSel

/* Targets ───────────────────────────────────────────────────── */
static const char *tgtLabels[NTGTS] = { "CY","DF","Deff","CC","CY*" };

/* Row button calibration (Engine 6 — Row Ops) ──────────────── */
/* Permute Rows (swap) calibration (Engine 3 = FS1) ──────────── */

/* Game screen grid ──────────────────────────────────────────── */

/* Automation ────────────────────────────────────────────────── */
static HANDLE hThread  = NULL;
static HANDLE hStop    = NULL;
static HHOOK  hHook    = NULL;
static BOOL edgeExitArmed = FALSE;

/* Flash ─────────────────────────────────────────────────────── */

/* Capture ───────────────────────────────────────────────────── */

/* INI ───────────────────────────────────────────────────────── */

/* ═══════════════════════════════════════════════════════════════
   FORWARD DECLARATIONS
═══════════════════════════════════════════════════════════════ */
static void SetView(int v);
static void EngineClick(int btn);
static void InvalidateSect(int n);
static void SaveIni(void);
static void LoadIni(void);
static void StartAuto(void);
static void StartXerox(void);
static BOOL StopAuto(void);
static void StartCap(int mode);
static void EndCap(void);
static void FlashBtn(DWORD bits, int sect);
static void Center(int idx, int *ox, int *oy);
static void DoClick(int x, int y);
static void DoDbl(int x, int y);
static void SendKey(WORD vk);
static BOOL FocusGameTab(void);
static BOOL IsTaskbarPinned(void);
static void PromptTaskbarPin(void);
LRESULT CALLBACK ProcMain(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ProcCap (HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ProcHelp(HWND, UINT, WPARAM, LPARAM);
static void OpenHelp(void);
static void CloseHelp(void);
LRESULT CALLBACK ProcHook(int, WPARAM, LPARAM);
DWORD   WINAPI   AutoRun (LPVOID);

/* ═══════════════════════════════════════════════════════════════
   INI HELPERS
═══════════════════════════════════════════════════════════════ */
static void LogApp(const char *message);
static void IniSetInt(const char *k, int v);
static int IniGetInt(const char *k, int def);

static void MakeIniPath(void) {
    char modulePath[MAX_PATH];
    DWORD length = GetModuleFileNameA(NULL, modulePath, sizeof modulePath);
    if (!length || length >= sizeof modulePath) { iniPath[0] = '\0'; return; }
    char *sl = strrchr(modulePath, '\\');
    if (sl) *(sl + 1) = '\0'; else modulePath[0] = '\0';
    if (snprintf(iniPath, sizeof iniPath, "%s_AwSim.ini", modulePath) >= (int)sizeof iniPath)
        iniPath[0] = '\0';
}

static BOOL SameFullPath(const char *left, const char *right) {
    char fullLeft[MAX_PATH], fullRight[MAX_PATH];
    DWORD lenLeft = GetFullPathNameA(left, MAX_PATH, fullLeft, NULL);
    DWORD lenRight = GetFullPathNameA(right, MAX_PATH, fullRight, NULL);
    if (!lenLeft || lenLeft >= MAX_PATH || !lenRight || lenRight >= MAX_PATH)
        return _stricmp(left, right) == 0;
    return _stricmp(fullLeft, fullRight) == 0;
}

static BOOL HasDebugArgument(void) {
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return FALSE;
    BOOL found = FALSE;
    for (int i = 1; i < argc; i++) {
        if (_wcsicmp(argv[i], L"-debug") == 0) { found = TRUE; break; }
    }
    LocalFree(argv);
    return found;
}

static BOOL IsTaskbarPinned(void) {
    char appData[MAX_PATH], folder[MAX_PATH], pattern[MAX_PATH], exePath[MAX_PATH];
    if (FAILED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appData)))
        return FALSE;
    if (snprintf(folder, sizeof folder,
                 "%s\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar",
                 appData) >= (int)sizeof folder ||
        snprintf(pattern, sizeof pattern, "%s\\*.lnk", folder) >= (int)sizeof pattern)
        return FALSE;
    DWORD exeLen = GetModuleFileNameA(NULL, exePath, sizeof exePath);
    if (!exeLen || exeLen >= sizeof exePath) return FALSE;

    HRESULT initResult = CoInitialize(NULL);
    if (FAILED(initResult) && initResult != RPC_E_CHANGED_MODE) return FALSE;
    BOOL uninitialize = SUCCEEDED(initResult);
    BOOL found = FALSE;
    WIN32_FIND_DATAA data;
    HANDLE search = FindFirstFileA(pattern, &data);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            char shortcut[MAX_PATH];
            if (snprintf(shortcut, sizeof shortcut, "%s\\%s", folder, data.cFileName)
                >= (int)sizeof shortcut) continue;
            WCHAR wideShortcut[MAX_PATH];
            if (!MultiByteToWideChar(CP_ACP, 0, shortcut, -1,
                                     wideShortcut, MAX_PATH)) continue;

            IShellLinkA *link = NULL;
            HRESULT hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                          &IID_IShellLinkA, (void **)&link);
            if (FAILED(hr) || !link) continue;
            IPersistFile *persist = NULL;
            hr = IShellLinkA_QueryInterface(link, &IID_IPersistFile, (void **)&persist);
            if (SUCCEEDED(hr) && persist) {
                if (SUCCEEDED(IPersistFile_Load(persist, wideShortcut, STGM_READ))) {
                    char target[MAX_PATH];
                    if (SUCCEEDED(IShellLinkA_GetPath(link, target, MAX_PATH, NULL,
                                                      SLGP_RAWPATH)) &&
                        SameFullPath(target, exePath))
                        found = TRUE;
                }
                IPersistFile_Release(persist);
            }
            IShellLinkA_Release(link);
            if (found) break;
        } while (FindNextFileA(search, &data));
        FindClose(search);
    }
    if (uninitialize) CoUninitialize();
    return found;
}

static void PromptTaskbarPin(void) {
    if (IsTaskbarPinned()) return;
    int answer = MessageBoxA(wMain,
        "Would you like quick access by placing AwSim on your taskbar?",
        APP_NAME, MB_YESNO | MB_ICONQUESTION | MB_TOPMOST | MB_SETFOREGROUND);
    if (answer != IDYES) return;
    LogApp("User accepted taskbar access prompt; Windows requires manual pin confirmation.");
    MessageBoxA(wMain,
        "Windows requires you to confirm taskbar pins yourself. Right-click the AwSim icon on the taskbar and choose 'Pin to taskbar'.",
        APP_NAME, MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
}

static void LogApp(const char *message) {
    if (!debugMode) return;
    char path[MAX_PATH];
    if (snprintf(path, sizeof path, "%s.log", iniPath) >= (int)sizeof path) return;
    HANDLE file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME now;
    GetLocalTime(&now);
    char line[512];
    int len = snprintf(line, sizeof line, "%04u-%02u-%02u %02u:%02u:%02u %s\r\n",
                       now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
                       now.wSecond, message);
    DWORD written;
    if (len > 0 && len < (int)sizeof line)
        WriteFile(file, line, (DWORD)len, &written, NULL);
    CloseHandle(file);
}

static BOOL PointOnVirtualDesktop(int x, int y) {
    int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int right = left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int bottom = top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return x >= left && x < right && y >= top && y < bottom;
}

static void NormalizeEngineSettings(char *eng) {
    int rank[4], seen[5] = {0};
    for (int i = 0; i < 10; i++)
        if (eng[i] < '0' || eng[i] > '9') eng[i] = '0';

    for (int i = 0; i < 4; i++) {
        rank[i] = eng[i + 2] - '0';
        if (rank[i] < 1 || rank[i] > 4 || seen[rank[i]]) rank[i] = 0;
        else seen[rank[i]] = 1;
        eng[i + 2] = '0';
        if (eng[i + 6] != '1') eng[i + 6] = '0';
    }
    int nextRank = 0;
    for (int order = 1; order <= 4; order++)
        for (int i = 0; i < 4; i++)
            if (rank[i] == order) eng[i + 2] = (char)('0' + ++nextRank);

    for (int i = 0; i < 2; i++)
        if (eng[i] > '5') eng[i] = '0';
    if (eng[0] != '0' && eng[1] != '0') {
        if (eng[1] < eng[0]) eng[0] = '0';
        else eng[1] = '0';
    }
    if (eng[0] != '0') eng[0] = (char)('0' + nextRank + 1);
    if (eng[1] != '0') eng[1] = (char)('0' + nextRank + 1);
    for (int i = 6; i < 10; i++)
        if (eng[i] != '1') eng[i] = '0';
    for (int i = 0; i < 4; i++)
        if (eng[i + 6] == '1' && eng[i + 2] == '0') eng[i + 6] = '0';
}

static void IniSetInt(const char *k, int v) {
    char b[32]; sprintf(b, "%d", v);
    WritePrivateProfileStringA(INI_SEC, k, b, iniPath);
}
static int IniGetInt(const char *k, int def) {
    return GetPrivateProfileIntA(INI_SEC, k, def, iniPath);
}
static void IniSetStr(const char *k, const char *v) {
    WritePrivateProfileStringA(INI_SEC, k, v, iniPath);
}
static void IniGetStr(const char *k, const char *def, char *out, int sz) {
    GetPrivateProfileStringA(INI_SEC, k, def, out, sz, iniPath);
}

static void SaveIni(void) {
    if (!wMain) return;
    RECT wr; GetWindowRect(wMain, &wr);

    IniSetStr("_Author",       "crackfed");
    IniSetStr("_AwSimVersion", APP_VER);
    IniSetInt("_SchemaVersion", APP_SCHEMA);
    IniSetInt("_Window-X",     wr.left);
    IniSetInt("_Window-Y",     wr.top);
    IniSetInt("_View",         viewState);

     /* engVars is split across three keys:
         [0]=selection style, [1..10]=engine settings, [11]=moves-per-cycle */
    char sel[2] = { engVars[0], '\0' };
    IniSetStr("_SelectionStyle", sel);
    char eng[11];
    memcpy(eng, engVars + 1, 10);  eng[10] = '\0';
    IniSetStr("_Engines", eng);
    char mpc[2] = { engVars[11], '\0' };
    IniSetStr("_MovesPerCycle", mpc);

    /* Current grid formation */
    char f[NCELLS + 1];
    for (int i = 0; i < NCELLS; i++) f[i] = (char)('0' + boxState[i]);
    f[NCELLS] = '\0';
    IniSetStr("_Formation", f);

    /* Active target stored as its label (CY/DF/Deff/CC/CY*) */
    IniSetStr("_Target", tgtLabels[activeTgt]);

    /* Target coordinates (0 = unconfigured; no separate Set flag) */
    static const char *TGTKEY[NTGTS] = {
        "_TargetCY", "_TargetDF", "_TargetDeff", "_TargetCC", "_TargetCY*"
    };
    char key[40];
    for (int i = 0; i < NTGTS; i++) {
        sprintf(key, "%s-X", TGTKEY[i]); IniSetInt(key, tgtX[i]);
        sprintf(key, "%s-Y", TGTKEY[i]); IniSetInt(key, tgtY[i]);
    }

    /* Boxgrid + button calibration (0 = unconfigured; no separate Set flag) */
    IniSetInt("_BoxGrid-X",       gx);
    IniSetInt("_BoxGrid-Y",       gy);
    IniSetInt("_BoxGridWidth",    gw);
    IniSetInt("_BoxGridHeight",   gh);
    IniSetInt("_BtnRow1Mirror-X", rb1x);
    IniSetInt("_BtnRow1Mirror-Y", rb1y);
    IniSetInt("_BtnRow4RShift-X", rb2x);
    IniSetInt("_BtnRow4RShift-Y", rb2y);
    IniSetInt("_BtnSwapRows12-X", rs12x);
    IniSetInt("_BtnSwapRows12-Y", rs12y);
    IniSetInt("_BtnSwapRows34-X", rs34x);
    IniSetInt("_BtnSwapRows34-Y", rs34y);
}

static void LoadIni(void) {
    int schema = IniGetInt("_SchemaVersion", 0);
    if (schema > APP_SCHEMA)
        LogApp("Settings were written by a newer schema; unsupported values may be ignored.");
    else if (schema < 0)
        LogApp("Invalid settings schema version; validating known settings.");
    winSX     = IniGetInt("_Window-X", INI_UNSET);
    winSY     = IniGetInt("_Window-Y", INI_UNSET);
    viewState = IniGetInt("_View", 0);
    if (viewState < 0 || viewState > 5) viewState = 0;

    /* Reassemble engVars[0..11] from the three split keys */
    char sel[8], eng[16], mpc[8];
    IniGetStr("_SelectionStyle", ".",          sel, sizeof sel);
    IniGetStr("_Engines",        "1000000000", eng, sizeof eng);
    IniGetStr("_MovesPerCycle",  "3",          mpc, sizeof mpc);
    if (sel[0] != '.' && sel[0] != '#') sel[0] = '.';
    if (strlen(eng) < 10)               strcpy(eng, "1000000000");
    {
        char savedVersion[16];
        IniGetStr("_AwSimVersion", "3.0", savedVersion, sizeof savedVersion);
        if (strcmp(savedVersion, "3.0") == 0) {
            char old[11];
            memcpy(old, eng, 10);  old[10] = '\0';
            eng[2] = old[4];  /* Column Ops rank */
            eng[3] = old[5];  /* Row Ops rank */
            eng[4] = old[2];  /* Permute rank */
            eng[5] = old[3];  /* Scout rank */
            eng[6] = '0';     /* no legacy Permute Advanced flag */
            eng[7] = (old[8] != '0') ? '1' : '0'; /* Column Advanced */
            eng[8] = (old[9] != '0') ? '1' : '0'; /* Row Advanced */
            eng[9] = (old[7] != '0') ? '1' : '0'; /* Scout Advanced */
            if (old[6] != '0') {
                for (int i = 0; i < 6; i++)
                    if (eng[i] > old[6]) eng[i]--;
            }
        }
    }
    NormalizeEngineSettings(eng);
    if (mpc[0] < '1' || mpc[0] > '5')   mpc[0] = '3';
    engVars[0] = sel[0];
    memcpy(engVars + 1, eng, 10);
    engVars[11] = mpc[0];
    engVars[12] = '\0';

    /* Restore the current grid formation */
    char f[NCELLS + 2] = "";
    IniGetStr("_Formation", "", f, sizeof f);
    int fl = (int)strlen(f);
    for (int i = 0; i < NCELLS; i++) {
        int state = fl == 0 ? BOX_ACTIVE
                            : (i < fl ? f[i] - '0' : BOX_RESERVE);
        boxState[i] = (BYTE)(state >= BOX_RESERVE && state <= BOX_DISABLED
                             ? state : BOX_RESERVE);
    }

    /* Active target — match the saved label back to its index */
    char tg[16];
    IniGetStr("_Target", "CY", tg, sizeof tg);
    activeTgt = 0;
    for (int i = 0; i < NTGTS; i++)
        if (strcmp(tg, tgtLabels[i]) == 0) { activeTgt = i; break; }

    /* Target coordinates (0 = unconfigured); Set flag derived from coords */
    static const char *TGTKEY[NTGTS] = {
        "_TargetCY", "_TargetDF", "_TargetDeff", "_TargetCC", "_TargetCY*"
    };
    char key[40];
    for (int i = 0; i < NTGTS; i++) {
        sprintf(key, "%s-X", TGTKEY[i]); tgtX[i] = IniGetInt(key, 0);
        sprintf(key, "%s-Y", TGTKEY[i]); tgtY[i] = IniGetInt(key, 0);
        if ((tgtX[i] != 0 || tgtY[i] != 0) &&
            !PointOnVirtualDesktop(tgtX[i], tgtY[i])) {
            char warning[96];
            snprintf(warning, sizeof warning, "Target %s calibration is off-screen; cleared.",
                     tgtLabels[i]);
            LogApp(warning);
            tgtX[i] = tgtY[i] = 0;
        }
        tgtSet[i] = (tgtX[i] != 0 || tgtY[i] != 0);
    }

    /* Boxgrid + button calibration; Set flags derived from nonzero coords */
    gx = IniGetInt("_BoxGrid-X", 0);     gy = IniGetInt("_BoxGrid-Y", 0);
    gw = IniGetInt("_BoxGridWidth", 0);  gh = IniGetInt("_BoxGridHeight", 0);
    gridSet = (gw >= 90 && gh >= 40 &&
               (long long)gx + gw - 1 <= INT_MAX &&
               (long long)gy + gh - 1 <= INT_MAX &&
               PointOnVirtualDesktop(gx, gy) &&
               PointOnVirtualDesktop(gx + gw - 1, gy + gh - 1));
    if ((gx != 0 || gy != 0 || gw != 0 || gh != 0) && !gridSet)
        LogApp("Saved boxgrid calibration is invalid or off-screen; setup required.");
    rb1x = IniGetInt("_BtnRow1Mirror-X", 0);  rb1y = IniGetInt("_BtnRow1Mirror-Y", 0);
    rb1Set = (rb1x != 0 || rb1y != 0) && PointOnVirtualDesktop(rb1x, rb1y);
    rb2x = IniGetInt("_BtnRow4RShift-X", 0);  rb2y = IniGetInt("_BtnRow4RShift-Y", 0);
    rb2Set = (rb2x != 0 || rb2y != 0) && PointOnVirtualDesktop(rb2x, rb2y);
    rs12x = IniGetInt("_BtnSwapRows12-X", 0); rs12y = IniGetInt("_BtnSwapRows12-Y", 0);
    rs12Set = (rs12x != 0 || rs12y != 0) && PointOnVirtualDesktop(rs12x, rs12y);
    rs34x = IniGetInt("_BtnSwapRows34-X", 0); rs34y = IniGetInt("_BtnSwapRows34-Y", 0);
    rs34Set = (rs34x != 0 || rs34y != 0) && PointOnVirtualDesktop(rs34x, rs34y);
}

/* ═══════════════════════════════════════════════════════════════
   BMP ASSETS
═══════════════════════════════════════════════════════════════ */
static void LoadBmps(void) {
    char dir[MAX_PATH];
    GetModuleFileNameA(NULL, dir, sizeof dir);
    char *sl = strrchr(dir, '\\');
    if (sl) *(sl+1) = '\0'; else dir[0] = '\0';
    for (int i = 0; i < 4; i++) {
        char p[MAX_PATH];
        snprintf(p, sizeof p, "%sAwSimUI-%d.bmp", dir, i+1);
        hBmp[i] = (HBITMAP)LoadImageA(NULL, p, IMAGE_BITMAP,
                                       0, 0, LR_LOADFROMFILE);
        if (!hBmp[i]) {
            char warning[96];
            snprintf(warning, sizeof warning, "Could not load UI bitmap AwSimUI-%d.bmp.", i + 1);
            LogApp(warning);
        }
    }
}
static void FreeBmps(void) {
    for (int i = 0; i < 4; i++) {
        if (hBmp[i]) { DeleteObject(hBmp[i]); hBmp[i] = NULL; }
    }
}

/* ═══════════════════════════════════════════════════════════════
   VIEW MANAGEMENT
═══════════════════════════════════════════════════════════════ */
static void SetView(int v) {
    if (v < 0 || v > 5) v = 0;
    viewState = v;
    int y = 0;
    for (int i = 0; i < 4; i++) {
        sectionY[i] = VIEW_VIS[v][i] ? y : -1;
        if (VIEW_VIS[v][i]) y += SECT_H[i];
    }
    RECT wr; GetWindowRect(wMain, &wr);
    SetWindowPos(wMain, NULL, wr.left, wr.top, WIN_W, y,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(wMain, NULL, TRUE);
    SaveIni();
}

static void InvalidateSect(int n) {
    if (!wMain || sectionY[n] < 0) return;
    RECT rc = { 0, sectionY[n], WIN_W, sectionY[n] + SECT_H[n] };
    InvalidateRect(wMain, &rc, FALSE);
}

/* Light a flash bit and arm the clear timer (resets if already running). */
static void FlashBtn(DWORD bits, int sect) {
    flashMask |= bits;
    InvalidateSect(sect);
    SetTimer(wMain, IDT_FLASH, 180, NULL);
}

static void SetAppStatus(const char *status) {
    if (!wMain) return;
    char title[128];
    snprintf(title, sizeof title, "%s  v%s - %s", APP_NAME, APP_VER, status);
    SetWindowTextA(wMain, title);
}

/* ═══════════════════════════════════════════════════════════════
    ENGINE LOGIC
    btn is 1-indexed: 1..2=CR, 3..6=FS, 7..10=Advanced.
═══════════════════════════════════════════════════════════════ */
static void EngineClick(int btn) {
    int v[11] = {0};
    for (int i = 1; i <= 10; i++) v[i] = engVars[i] - '0';

    /* Count active FS engines (3-6) for run-order slot assignment */
    int hiVal = 0;
    for (int i = 3; i <= 6; i++) if (v[i] > 0) hiVal++;

    if (btn >= 7) {
        int parent = btn - 4;
        if (v[btn] == 0) {
            if (v[parent] == 0) {
                v[parent] = hiVal + 1;
                for (int i = 1; i <= 2; i++)
                    if (v[i] > 0) v[i]++;
            }
            v[btn] = 1;
        } else {
            v[btn] = 0;
        }
    } else if (v[btn] == 0) {
        /* ── OFF → ON ──────────────────────────────────────── */
        if (btn >= 3 && btn <= 6) {
            /* FS: take the next run-order slot, then bump CR         */
            v[btn] = hiVal + 1;
            for (int i = 1; i <= 2; i++)
                if (v[i] > 0) v[i]++;
        } else {
            /* CR: mutually exclusive; rank = active-FS-count + 1    */
            for (int i = 1; i <= 2; i++) v[i] = 0;
            v[btn] = hiVal + 1;
        }
    } else {
        /* ── ON → OFF ──────────────────────────────────────── */
        if (btn >= 3 && btn <= 6) {
            int comp = v[btn];
            v[btn] = 0;
            v[btn + 4] = 0;  /* clear this engine's Advanced flag */
            /* Compact whole sequence including CR.
               CR is always > comp so it decrements by 1,
               keeping rank = remaining-FS-count + 1. Never hits 0. */
            for (int i = 1; i <= 6; i++)
                if (v[i] > comp) v[i]--;
        } else {
            v[btn] = 0;                   /* CR toggle off            */
        }
    }

    for (int i = 1; i <= 10; i++) engVars[i] = '0' + v[i];
}

static void MpcCycle(void) {
    int m = engVars[11] - '0';
    engVars[11] = '0' + (m % 5) + 1;   /* 1→2→3→4→5→1 */
}
static void DotNumToggle(void) {
    engVars[0] = (engVars[0] == '.') ? '#' : '.';
}
static void EngineReset(void) {
    strcpy(engVars, ".10000000003");
}

/* ═══════════════════════════════════════════════════════════════
   BOXGRID / CYCLE BUTTONS
═══════════════════════════════════════════════════════════════ */
/* Toggle Presets button: cycle the grid through 5 presets each click.
   0=All  1=None  2=Left 5 cols  3=Right 5 cols  4=Top 2 rows.       */
static void TogglePresets(void) {
    presetIdx = (presetIdx + 1) % 5;
    for (int i = 0; i < NCELLS; i++) {
        int c = i % GCOLS, r = i / GCOLS;
        switch (presetIdx) {
        case 0: boxState[i] = BOX_ACTIVE;  break;   /* All           */
        case 1: boxState[i] = BOX_RESERVE; break;   /* None          */
        case 2: boxState[i] = (c < 5) ? BOX_ACTIVE : BOX_RESERVE; break;
        case 3: boxState[i] = (c >= GCOLS-5) ? BOX_ACTIVE : BOX_RESERVE; break;
        case 4: boxState[i] = (r < 2) ? BOX_ACTIVE : BOX_RESERVE; break;
        }
    }
    InvalidateSect(2);
    SaveIni();
}

/* ── Windows Magnifier (Magnify.exe) helpers ──────────────────
   XEROX only captures correctly when the screen is at true 1:1 scale,
   so if Magnifier is running and zoomed in, XEROX drops it to 100% for
   the scan and restores the previous zoom afterwards.                  */
static BOOL MagnifierRunning(void) {
    BOOL found = FALSE;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe;  pe.dwSize = sizeof pe;
        if (Process32First(snap, &pe)) {
            do {
                if (_stricmp(pe.szExeFile, "Magnify.exe") == 0) { found = TRUE; break; }
            } while (Process32Next(snap, &pe));
        }
        CloseHandle(snap);
    }
    return found;
}
static int MagRegDword(const char *value, int def) {
    HKEY k;  DWORD v = def, sz = sizeof v, ty = REG_DWORD;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\ScreenMagnifier",
                      0, KEY_READ, &k) == ERROR_SUCCESS) {
        RegQueryValueExA(k, value, NULL, &ty, (LPBYTE)&v, &sz);
        RegCloseKey(k);
    }
    return (int)v;
}
/* Win + (numpad +/-): dir>0 zoom in, dir<0 zoom out. */
static void MagZoomKey(int dir) {
    WORD vk = (dir > 0) ? 0x6B : 0x6D;
    keybd_event(0x5B, 0, 0, 0);                 Sleep(20);
    keybd_event(vk, 0, 0, 0);
    keybd_event(vk, 0, KEYEVENTF_KEYUP, 0);        Sleep(20);
    keybd_event(0x5B, 0, KEYEVENTF_KEYUP, 0);   Sleep(120);
}

/* XEROX: copy the game's current formation into the boxgrid.
   A unit normally shows NO bolt; a bolt means that unit is disabled.
   XEROX detects every occupied box while leaving any already-disabled
   units disabled and excluding them from the formation:
     - scan #1 (before 'z') records which units are already disabled
     - 'z' disables all, scan #2 records every occupied box, 'z' re-enables
     - the already-disabled units are re-disabled via the 'x' selective
       toggle so the game state is unchanged
     - occ = (all occupied) AND NOT (initially disabled)                  */
#define XEROX_THRESHOLD  6   /* min pure-red badge pixels in the right strip */
#define XEROX_WMAX     160   /* max scan-region dimension (buffer bound)       */

/* Focus the game's browser tab WITHOUT clicking (so no unit/mirror state
   is disturbed): find the top-level window sitting under the boxgrid and
   bring it to the foreground. Used wherever tab focus is needed before
   sending keys. */
static BOOL FocusGameTab(void) {
    if (!gridSet) return FALSE;
    POINT p;  p.x = gx + gw / 2;  p.y = gy + gh / 2;
    HWND root = GetAncestor(WindowFromPoint(p), GA_ROOT);
    if (!root || root == wMain) return FALSE;
    HWND foreground = GetForegroundWindow();
    if (foreground && GetAncestor(foreground, GA_ROOT) == root) {
        gameWindow = root;
        return TRUE;
    }
    for (int attempt = 0; attempt < 3; attempt++) {
        SetForegroundWindow(root);
        Sleep(250);
        HWND foreground = GetForegroundWindow();
        if (foreground && GetAncestor(foreground, GA_ROOT) == root) {
            gameWindow = root;
            return TRUE;
        }
    }
    gameWindow = NULL;
    return FALSE;
}

static BOOL EnsureGameForeground(void) {
    if (!running || !gameWindow || !hStop) return FALSE;
    for (int attempt = 0; attempt < 3; attempt++) {
        HWND foreground = GetForegroundWindow();
        if (foreground && GetAncestor(foreground, GA_ROOT) == gameWindow) return TRUE;
        if (WaitForSingleObject(hStop, 0) == WAIT_OBJECT_0) return FALSE;
        if (attempt < 2) {
            SetForegroundWindow(gameWindow);
            if (WaitForSingleObject(hStop, 180) == WAIT_OBJECT_0) return FALSE;
        }
    }
    LogApp("Automation stopped: game window lost foreground focus.");
    SetEvent(hStop);
    return FALSE;
}

static BOOL GameIsForeground(void) {
    HWND foreground = GetForegroundWindow();
    return gameWindow && foreground &&
           GetAncestor(foreground, GA_ROOT) == gameWindow;
}

/* Capture the calibrated grid region and flag each cell whose bolt area
   holds at least XEROX_THRESHOLD bright-white pixels. The disabled bolt is
   a thin element on the far-right edge, so only the RIGHT 1/8 (width) of
   the LOWER HALF (height) is sampled — this ignores the lighter unit art
   and the top-left star/number labels, keying purely on the bolt. */
static void XeroxScan(BOOL *out, const char *tag) {
    char dbg[8192];  int dlen = 0;
    dlen += snprintf(dbg+dlen, sizeof dbg - dlen,
                     "=== XEROX scan [%s]  grid %dx%d ===\r\n", tag, gw, gh);

    HDC scr = GetDC(NULL);
    HDC mem = CreateCompatibleDC(scr);
    HBITMAP bm = CreateCompatibleBitmap(scr, gw, gh);
    HBITMAP ob = (HBITMAP)SelectObject(mem, bm);
    BitBlt(mem, 0, 0, gw, gh, scr, gx, gy, SRCCOPY);
    int cellW = gw / GCOLS, cellH = gh / GROWS;
    for (int i = 0; i < NCELLS; i++) {
        int c = i % GCOLS, r = i / GCOLS;
        /* Right strip of the cell: holds the red lightning badge of a disabled
           unit. Excludes the centre (red unit art) and the very edge (border). */
        int xL = c * cellW + (cellW * 76) / 100, xR = c * cellW + (cellW * 93) / 100;
        int yT = r * cellH + (cellH * 35) / 100, yB = r * cellH + (cellH * 99) / 100;
        int w = xR - xL, h = yB - yT;
        if (w < 1 || h < 1) { out[i] = FALSE; continue; }

        /* Bolt badge = pure saturated red (R>200, G<40, B<40). The brownish/
           less-saturated red of unit art doesn't qualify, and the red cell
           border only ever appears on already-disabled cells, so it can't
           cause a false positive on an enabled one. */
        int red = 0, bR = 0, bG = 0, bB = 0, bmax = -1;
        for (int yy = 0; yy < h; yy++)
            for (int xx = 0; xx < w; xx++) {
                COLORREF px = GetPixel(mem, xL + xx, yT + yy);
                int R = GetRValue(px), G = GetGValue(px), B = GetBValue(px);
                if (R > 200 && G < 40 && B < 40) red++;
                if (R > bmax) { bmax = R; bR = R; bG = G; bB = B; }
            }
        BOOL bolt = (red >= XEROX_THRESHOLD);
        out[i] = bolt;

        if (dlen < (int)sizeof dbg - 120)
            dlen += snprintf(dbg+dlen, sizeof dbg - dlen,
                "c%02d r%d col%d  region %dx%d: pureRed=%d reddest=(%d,%d,%d) -> %s\r\n",
                i, r, c, w, h, red, bR, bG, bB, bolt ? "BOLT" : ".");
    }
    SelectObject(mem, ob);
    DeleteObject(bm);  DeleteDC(mem);  ReleaseDC(NULL, scr);

    /* Write the diagnostic next to the INI: PRE truncates, POST appends. */
    char path[MAX_PATH];  strcpy(path, iniPath);
    char *sl = strrchr(path, '\\');
    strcpy(sl ? sl + 1 : path, "_XeroxScan.txt");
    BOOL append = (tag[0] == 'P' && tag[1] == 'O');   /* "POST" */
    if (debugMode) {
        HANDLE hf = CreateFileA(path, append ? FILE_APPEND_DATA : GENERIC_WRITE,
                                FILE_SHARE_READ, NULL,
                                append ? OPEN_ALWAYS : CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, NULL);
        if (hf != INVALID_HANDLE_VALUE) {
            DWORD wr;  WriteFile(hf, dbg, (DWORD)dlen, &wr, NULL);  CloseHandle(hf);
        }
    }
}

/* ── Auto-calibrate the boxgrid from its 24 dark intersection diamonds ──────
   A 9×4 boxgrid has 8 internal vertical separators × 3 horizontal = 24 dark
   diamonds at the lattice crossings. Using the saved grid as a prior (so we
   know roughly where to look), we find the dark diamond in a small window at
   each expected crossing, then least-squares-fit the column/row positions to
   refine gx, gy, gw, gh. Returns TRUE (and updates the globals) on success;
   FALSE — leaving the saved values untouched — if too few diamonds are found
   or the fitted size is implausible (grid absent / wrong screen). Needs a
   saved prior; with none, the caller falls back to corner marking. */
#define AC_MARGIN  16   /* capture margin around saved grid (px)          */
#define AC_TOL     13   /* half-size of the per-crossing search window    */
#define AC_DARK    22   /* per-channel "very dark" threshold              */
#define AC_MINDARK  8   /* min dark pixels in a window to accept a diamond*/
#define AC_MINCOLS  3   /* min distinct vertical lines for a valid fit    */
#define AC_MINROWS  2   /* min distinct horizontal lines for a valid fit  */

static BOOL AutoCalibrateGrid(void) {
    if (!gridSet || gw < 90 || gh < 40) return FALSE;     /* need a prior  */
    double cwP = gw / (double)GCOLS, chP = gh / (double)GROWS;

    int rx = gx - AC_MARGIN, ry = gy - AC_MARGIN;
    int rw = gw + 2*AC_MARGIN, rh = gh + 2*AC_MARGIN;

    HDC scr = GetDC(NULL);
    HDC mem = CreateCompatibleDC(scr);
    HBITMAP bm = CreateCompatibleBitmap(scr, rw, rh);
    HBITMAP ob = (HBITMAP)SelectObject(mem, bm);
    BitBlt(mem, 0, 0, rw, rh, scr, rx, ry, SRCCOPY);

    /* x ~ a + b*k  (column index k);  y ~ a + b*j  (row index j) */
    int    n = 0;
    double SK = 0, SX = 0, SKK = 0, SKX = 0;     /* x-regression sums */
    double SJ = 0, SY = 0, SJJ = 0, SJY = 0;     /* y-regression sums */
    int    cols[GCOLS] = {0}, rows[GROWS] = {0};

    for (int k = 1; k < GCOLS; k++)
        for (int j = 1; j < GROWS; j++) {
            int exM = gx + (int)(k*cwP + 0.5) - rx;   /* expected, mem-DC */
            int eyM = gy + (int)(j*chP + 0.5) - ry;
            long sx = 0, sy = 0;  int cnt = 0;
            for (int yy = eyM-AC_TOL; yy <= eyM+AC_TOL; yy++) {
                if (yy < 0 || yy >= rh) continue;
                for (int xx = exM-AC_TOL; xx <= exM+AC_TOL; xx++) {
                    if (xx < 0 || xx >= rw) continue;
                    COLORREF px = GetPixel(mem, xx, yy);
                    if (GetRValue(px) < AC_DARK && GetGValue(px) < AC_DARK &&
                        GetBValue(px) < AC_DARK) { sx += xx; sy += yy; cnt++; }
                }
            }
            if (cnt >= AC_MINDARK) {
                double cx = (double)sx/cnt + rx;     /* centroid → screen */
                double cy = (double)sy/cnt + ry;
                SK += k; SX += cx; SKK += (double)k*k; SKX += (double)k*cx;
                SJ += j; SY += cy; SJJ += (double)j*j; SJY += (double)j*cy;
                cols[k] = rows[j] = 1;  n++;
            }
        }
    SelectObject(mem, ob);  DeleteObject(bm);  DeleteDC(mem);  ReleaseDC(NULL, scr);

    int nc = 0, nr = 0;
    for (int k = 0; k < GCOLS; k++) nc += cols[k];
    for (int j = 0; j < GROWS; j++) nr += rows[j];
    if (nc < AC_MINCOLS || nr < AC_MINROWS) return FALSE;

    double dX = n*SKK - SK*SK, dY = n*SJJ - SJ*SJ;
    if (dX == 0 || dY == 0) return FALSE;
    double cw = (n*SKX - SK*SX)/dX,  gxN = (SX - cw*SK)/n;   /* width, left */
    double ch = (n*SJY - SJ*SY)/dY,  gyN = (SY - ch*SJ)/n;   /* height, top */

    /* Reject implausible fits (grid not really there / bad detection). */
    if (cw < cwP*0.75 || cw > cwP*1.25 || ch < chP*0.75 || ch > chP*1.25)
        return FALSE;

    gx = (int)(gxN + 0.5);
    gy = (int)(gyN + 0.5);
    gw = (int)(cw*GCOLS + 0.5);
    gh = (int)(ch*GROWS + 0.5);
    return TRUE;
}

/* ── Cold-start: locate the boxgrid with no saved prior ────────────────────
   The grid lives in the lowest ~200 px of the screen. We capture that band and
   find the dark intersection diamonds by dark-density (so they're detected even
   when the thin gap-lines connect them all into one dark mass). We derive the
   column/row spacing from the diamonds, identify the lattice, and — only if all
   8 vertical and all 3 horizontal separators are present — regress the line
   positions to set the grid edges directly (left/top edge = first separator
   minus one cell). If it can't lock the full lattice it returns FALSE and the
   caller falls back to corner marking, so it can never make cold-start worse
   than the manual path. */
#define CS_DARK   22     /* per-channel "very dark" threshold        */
#define CS_BOXR    3     /* half-size of the density box (7x7)        */
#define CS_DMIN   28     /* min dark pixels in the box to be a "core" */

/* Dominant spacing of values along one axis. Picks the LARGEST period whose
   alignment count is near the maximum, so a true period of 100 isn't reported
   as its sub-harmonic 50 (which also aligns). Returns 0 on failure. */
static int csSpacing(const double *v, int nv, int lo, int hi, int *phaseOut, int *alignedOut) {
    int gmax = 0;
    int bestCnt[256]; int bestPh[256];          /* indexed by (S - lo) */
    if (hi - lo >= 256) hi = lo + 255;
    for (int S = lo; S <= hi; S++) {
        int cnt[400];  if (S > 400) break;
        for (int i = 0; i < S; i++) cnt[i] = 0;
        for (int i = 0; i < nv; i++) { int m = ((int)(v[i]+0.5)) % S; if (m < 0) m += S; cnt[m]++; }
        int bc = 0, bp = 0;
        for (int ph = 0; ph < S; ph++) {
            int c = 0;
            for (int d = -2; d <= 2; d++) { int q = (ph+d) % S; if (q < 0) q += S; c += cnt[q]; }
            if (c > bc) { bc = c; bp = ph; }
        }
        bestCnt[S-lo] = bc;  bestPh[S-lo] = bp;
        if (bc > gmax) gmax = bc;
    }
    if (gmax < 3) return 0;
    for (int S = hi; S >= lo; S--)              /* largest near-max period */
        if (bestCnt[S-lo] * 100 >= gmax * 85) {
            *phaseOut = bestPh[S-lo];  *alignedOut = bestCnt[S-lo];  return S;
        }
    return 0;
}

static BOOL DetectGridColdStart(void) {
    int SW = GetSystemMetrics(SM_CXSCREEN), SH = GetSystemMetrics(SM_CYSCREEN);
    int x0 = 0, rw = SW;                        /* full width               */
    int rh = 200, y0 = SH - rh;                 /* lowest 200 px            */
    if (y0 < 0) { y0 = 0; rh = SH; }
    if (rw < 200 || rh < 60) return FALSE;

    HDC scr = GetDC(NULL);
    HDC mem = CreateCompatibleDC(scr);
    BITMAPINFO bi;  memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = rw;  bi.bmiHeader.biHeight = -rh;   /* top-down */
    bi.bmiHeader.biPlanes = 1;  bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!dib) { DeleteDC(mem); ReleaseDC(NULL, scr); return FALSE; }
    HBITMAP ob = (HBITMAP)SelectObject(mem, dib);
    BitBlt(mem, 0, 0, rw, rh, scr, x0, y0, SRCCOPY);
    GdiFlush();
    unsigned char *pix = (unsigned char *)bits;   /* BGRA, row stride rw*4 */

    int n = rw * rh;
    unsigned char *dark = (unsigned char *)malloc(n);
    unsigned char *vis  = (unsigned char *)malloc(n);
    int *stk = (int *)malloc(sizeof(int) * n);
    int W1 = rw + 1;
    int *ii  = (int *)malloc(sizeof(int) * W1 * (rh + 1));
    double *bx = (double *)malloc(sizeof(double) * 2048);
    double *by = (double *)malloc(sizeof(double) * 2048);
    if (!dark || !vis || !stk || !ii || !bx || !by) {
        free(dark); free(vis); free(stk); free(ii); free(bx); free(by);
        SelectObject(mem, ob); DeleteObject(dib); DeleteDC(mem); ReleaseDC(NULL, scr);
        return FALSE;
    }
    int totalDark = 0;
    for (int i = 0; i < n; i++) {
        unsigned char *q = pix + (size_t)i*4;
        dark[i] = (q[0] < CS_DARK && q[1] < CS_DARK && q[2] < CS_DARK);
        if (dark[i]) totalDark++;
        vis[i] = 0;
    }
    SelectObject(mem, ob); DeleteObject(dib); DeleteDC(mem); ReleaseDC(NULL, scr);

    /* Integral image of the dark map for fast box-density queries. */
    for (int x = 0; x <= rw; x++) ii[x] = 0;
    for (int y = 1; y <= rh; y++) {
        int rs = 0;  ii[y*W1] = 0;
        for (int x = 1; x <= rw; x++) {
            rs += dark[(y-1)*rw + (x-1)];
            ii[y*W1 + x] = ii[(y-1)*W1 + x] + rs;
        }
    }
    /* A diamond is a compact dark patch, so dark density in a small box is high
       at its centre; a thin gap-line scores low. Marking high-density "cores"
       finds diamonds even when the gap-lines connect them into one dark mass. */
    int nCore = 0;
    for (int y = 0; y < rh; y++)
        for (int x = 0; x < rw; x++) {
            int x0 = x-CS_BOXR, y0 = y-CS_BOXR, x1 = x+CS_BOXR, y1 = y+CS_BOXR;
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 >= rw) x1 = rw-1;
            if (y1 >= rh) y1 = rh-1;
            int dens = ii[(y1+1)*W1+(x1+1)] - ii[y0*W1+(x1+1)]
                     - ii[(y1+1)*W1+x0]    + ii[y0*W1+x0];
            dark[y*rw + x] = (dens >= CS_DMIN);   /* repurpose dark[] as core map */
            if (dens >= CS_DMIN) nCore++;
        }
    free(ii);

    /* Flood-fill the cores into diamond centroids (screen coords). Cores are
       isolated even when the raw dark pixels were all connected, so each
       diamond becomes its own small blob. */
    int nb = 0;
    for (int s = 0; s < n && nb < 2048; s++) {
        if (!dark[s] || vis[s]) continue;
        int sp = 0; stk[sp++] = s; vis[s] = 1;
        long sx = 0, sy = 0; int cnt = 0, mnx = rw, mxx = 0, mny = rh, mxy = 0;
        while (sp) {
            int idx = stk[--sp], x = idx % rw, y = idx / rw;
            sx += x; sy += y; cnt++;
            if (x < mnx) mnx = x;
            if (x > mxx) mxx = x;
            if (y < mny) mny = y;
            if (y > mxy) mxy = y;
            const int ddx[4] = {1,-1,0,0}, ddy[4] = {0,0,1,-1};
            for (int d = 0; d < 4; d++) {
                int nx = x+ddx[d], ny = y+ddy[d];
                if (nx < 0 || nx >= rw || ny < 0 || ny >= rh) continue;
                int ni = ny*rw + nx;
                if (dark[ni] && !vis[ni]) { vis[ni] = 1; stk[sp++] = ni; }
            }
        }
        int bw = mxx-mnx+1, bh = mxy-mny+1;
        if (cnt >= 3 && cnt <= 250 && bw <= 30 && bh <= 30) {
            bx[nb] = (double)sx/cnt + x0;  by[nb] = (double)sy/cnt + y0;  nb++;
        }
    }
    free(dark); free(vis); free(stk);

    BOOL ok = FALSE;
    int dCW=0, dCH=0, dCRUN=0, dRRUN=0, dNBcol=0, dNBrow=0;
    int dSelCol=-1, dSelRow=-1, dGridX=0, dGridY=0, dGridW=0, dGridH=0;
    if (nb >= 8) {
        int phx, alx, phy, aly;
        int cw = csSpacing(bx, nb, 40, 220, &phx, &alx);    /* cell width  */
        int ch = csSpacing(by, nb, 22,  90, &phy, &aly);    /* cell height */
        dCW = cw; dCH = ch;
        if (cw && ch) {
            /* Snap each diamond to the column/row lattice (separators ≡ phx/phy
               mod cw/ch) and accumulate per-line position sums. */
            #define CS_KMAX 128
            double colSum[CS_KMAX] = {0}, rowSum[CS_KMAX] = {0};
            int    colCnt[CS_KMAX] = {0}, rowCnt[CS_KMAX] = {0};
            int    crossings[CS_KMAX][CS_KMAX] = {{0}};
            int    tolx = cw/5 + 2, toly = ch/5 + 2;
            for (int i = 0; i < nb; i++) {
                int k = (int)floor((bx[i]-phx)/(double)cw + 0.5);
                int j = (int)floor((by[i]-phy)/(double)ch + 0.5);
                BOOL onCol = FALSE, onRow = FALSE;
                if (k >= 0 && k < CS_KMAX) {
                    double d = bx[i]-(phx+(double)k*cw);  if (d < 0) d = -d;
                    if (d <= tolx) { colSum[k] += bx[i]; colCnt[k]++; onCol = TRUE; }
                }
                if (j >= 0 && j < CS_KMAX) {
                    double d = by[i]-(phy+(double)j*ch);  if (d < 0) d = -d;
                    if (d <= toly) { rowSum[j] += by[i]; rowCnt[j]++; onRow = TRUE; }
                }
                if (onCol && onRow) crossings[k][j]++;
            }
            /* Longest run of consecutive populated column / row lines. */
            int crun = 0, rrun = 0, cur;
            cur = 0;
            for (int k = 0; k < CS_KMAX; k++) {
                if (colCnt[k]) { cur++; if (cur > crun) crun = cur; }
                else cur = 0;
            }
            cur = 0;
            for (int j = 0; j < CS_KMAX; j++) {
                if (rowCnt[j]) { cur++; if (cur > rrun) rrun = cur; }
                else cur = 0;
            }
            for (int k = 0; k < CS_KMAX; k++) if (colCnt[k]) dNBcol++;
            for (int j = 0; j < CS_KMAX; j++) if (rowCnt[j]) dNBrow++;
            dCRUN = crun; dRRUN = rrun;
            /* Find the densest complete separator block so unrelated aligned
               rows below or above the grid cannot define its perimeter. */
            int bestScore = -1, bestK = -1, bestJ = -1;
            for (int firstK = 0; firstK + GCOLS - 1 < CS_KMAX; firstK++)
                for (int firstJ = 0; firstJ + GROWS - 1 < CS_KMAX; firstJ++) {
                    int score = 0;
                    BOOL complete = TRUE;
                    for (int ck = 0; ck < GCOLS - 1; ck++)
                        for (int rj = 0; rj < GROWS - 1; rj++) {
                            int hits = crossings[firstK + ck][firstJ + rj];
                            if (!hits) complete = FALSE;
                            score += hits;
                        }
                    if (complete && score > bestScore) {
                        bestScore = score;
                        bestK = firstK;
                        bestJ = firstJ;
                    }
                }
            if (bestK >= 0 && bestJ >= 0) {
                dSelCol = bestK; dSelRow = bestJ;
                int nC = GCOLS-1, nR = GROWS-1;
                double selectedX[GCOLS] = {0}, selectedY[GROWS] = {0};
                int selectedXC[GCOLS] = {0}, selectedYC[GROWS] = {0};
                for (int i = 0; i < nb; i++) {
                    int k = (int)floor((bx[i]-phx)/(double)cw + 0.5);
                    int j = (int)floor((by[i]-phy)/(double)ch + 0.5);
                    if (k < bestK || k >= bestK+nC || j < bestJ || j >= bestJ+nR)
                        continue;
                    double ex = bx[i] - (phx + (double)k*cw);
                    double ey = by[i] - (phy + (double)j*ch);
                    if (fabs(ex) > tolx || fabs(ey) > toly) continue;
                    selectedX[k-bestK] += bx[i]; selectedXC[k-bestK]++;
                    selectedY[j-bestJ] += by[i]; selectedYC[j-bestJ]++;
                }
                double Sm=0,SX=0,Smm=0,SmX=0;
                for (int t = 0; t < nC; t++) {
                    if (!selectedXC[t]) { bestK = -1; break; }
                    double X = selectedX[t]/selectedXC[t];  int m = t+1;
                    Sm+=m; SX+=X; Smm+=(double)m*m; SmX+=(double)m*X;
                }
                double Sp=0,SY=0,Spp=0,SpY=0;
                for (int t = 0; t < nR && bestK >= 0; t++) {
                    if (!selectedYC[t]) { bestK = -1; break; }
                    double Y = selectedY[t]/selectedYC[t];  int p = t+1;
                    Sp+=p; SY+=Y; Spp+=(double)p*p; SpY+=(double)p*Y;
                }
                double dX = nC*Smm-Sm*Sm, dY = nR*Spp-Sp*Sp;
                if (bestK >= 0 && dX != 0 && dY != 0) {
                    double cwF = (nC*SmX-Sm*SX)/dX, gxF = (SX-cwF*Sm)/nC;
                    double chF = (nR*SpY-Sp*SY)/dY, gyF = (SY-chF*Sp)/nR;
                    if (cwF > 20 && chF > 10) {
                        gx = (int)(gxF+0.5);  gy = (int)(gyF+0.5);
                        gw = (int)(cwF*GCOLS+0.5);  gh = (int)(chF*GROWS+0.5);
                        dGridX = gx; dGridY = gy; dGridW = gw; dGridH = gh;
                        gridSet = TRUE;
                        ok = TRUE;
                    }
                }
            }
        }
    }
    free(bx); free(by);

    /* Diagnostic: record what cold-start saw, next to the INI. */
    {
        char path[MAX_PATH];  strcpy(path, iniPath);
        char *sl = strrchr(path, '\\');
        strcpy(sl ? sl+1 : path, "_ColdStart.txt");
        char d[512];
        int dl = snprintf(d, sizeof d,
            "region x0=%d y0=%d rw=%d rh=%d (screen %dx%d)\r\n"
            "darkPixels=%d  cores=%d  diamonds=%d\r\n"
            "cellW=%d cellH=%d  colLines=%d rowLines=%d  colRun=%d rowRun=%d\r\n"
            "selected lattice k=%d j=%d  perimeter=(%d,%d) size=%dx%d\r\n"
            "need complete %dx%d internal intersections  ->  %s\r\n",
            x0, y0, rw, rh, SW, SH, totalDark, nCore, nb,
            dCW, dCH, dNBcol, dNBrow, dCRUN, dRRUN,
            dSelCol, dSelRow, dGridX, dGridY, dGridW, dGridH,
            GCOLS-1, GROWS-1, ok ? "LOCKED" : "fell back to user setup");
        if (debugMode) {
            HANDLE hf = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hf != INVALID_HANDLE_VALUE) {
                DWORD wr; WriteFile(hf, d, (DWORD)dl, &wr, NULL); CloseHandle(hf);
            }
        }
    }
    return ok;
}

/* "WORKING" image blitted over the XEROX button while a capture runs.
   XeroxFormation blocks the main thread (Sleeps), so no WM_PAINT fires —
   we blit straight to the window DC, then InvalidateSect(3) at the end
   repaints the normal button. The bitmap (red text on black) is embedded
   in the EXE as a resource, so nothing external is required. */
static void ShowXeroxWait(void) {
    if (!wMain || sectionY[2] < 0 || !hWork) return;
    int top = sectionY[2];

    BITMAP bm;
    GetObject(hWork, sizeof bm, &bm);

     /* Center the 116px WORKING image over the shifted XEROX wordmark. */
     int dx = 448;
    int dy = top + 107;

    HDC dc  = GetDC(wMain);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP ob = (HBITMAP)SelectObject(mem, hWork);
    BitBlt(dc, dx, dy, bm.bmWidth, bm.bmHeight, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteDC(mem);
    ReleaseDC(wMain, dc);
}

static void XeroxFormation(void) {
    if (!gridSet) return;   /* StartXerox ensures the grid is calibrated */

    if (!FocusGameTab()) {
        LogApp("XEROX aborted: could not focus the window under the calibrated grid.");
        SetAppStatus("Game window not focused");
        return;
    }

    ShowXeroxWait();        /* cover XEROX button with WORKING until we finish */

    /* Remember where the cursor was (the XEROX button) to restore later. */
    POINT savePos;  GetCursorPos(&savePos);

    /* If Magnifier is running and zoomed past 100%, drop it to 100% so the
       screen capture is at true 1:1 scale; remember how far to restore. */
    BOOL mag = MagnifierRunning();
    int  magSteps = 0;
    if (mag) {
        int zoom = MagRegDword("Magnification", 100);
        int inc  = MagRegDword("ZoomIncrement", 100);
        if (inc  < 5)   inc  = 100;
        if (zoom > 100) {
            magSteps = (zoom - 100 + inc - 1) / inc;             /* ceil */
            for (int i = 0; i < magSteps + 3; i++) MagZoomKey(-1); /* clamp@100 */
            Sleep(200);
        }
    }

    /* Now at 100% zoom and about to scan — re-calibrate the grid from its
       dark intersection diamonds so XEROX stays aligned if it has shifted.
       Keeps the saved grid if the diamonds aren't found. */
    if (!AutoCalibrateGrid())
        LogApp("XEROX grid refinement did not lock; using current calibrated dimensions.");

    /* Scan #1: record which units are ALREADY disabled (bolt present now).
       Done BEFORE focus — it is only a screen read, so it captures the
       pristine grid before anything can disturb it. */
    BOOL preDis[NCELLS];
    XeroxScan(preDis, "PRE");
    int nPre = 0;
    for (int i = 0; i < NCELLS; i++) if (preDis[i]) nPre++;

    /* Deactivate all units → every occupied box shows a bolt. */
    SendKey('Z');
    Sleep(800);                 /* allow the (low-fps) game to render them   */

    /* Scan #2: record every occupied box. */
    BOOL allUnits[NCELLS];
    XeroxScan(allUnits, "POST");

    /* Reactivate all units to restore the normal (bolt-free) state. */
    SendKey('Z');
    Sleep(100);

    /* Re-disable only the units that were ALREADY disabled at the start.
       If none were, skip the selective-toggle entirely (no 'x', no clicks). */
    if (nPre > 0) {
        SendKey('X');                 /* enter selective toggle               */
        Sleep(300);
        for (int i = 0; i < NCELLS; i++) {
            if (!preDis[i]) continue;
            int cx, cy;  Center(i, &cx, &cy);
            DoClick(cx, cy);          /* enabled now → click re-disables it    */
            Sleep(150);
        }
        SendKey('X');                 /* leave selective toggle               */
        Sleep(200);
    }

    /* Formation = every occupied box EXCEPT those initially disabled. */
    for (int i = 0; i < NCELLS; i++)
        if (allUnits[i] && preDis[i]) boxState[i] = BOX_DISABLED;
        else boxState[i] = allUnits[i] ? BOX_ACTIVE : BOX_RESERVE;

    /* Restore Magnifier zoom last, after all operations. Everything above
       ran at 100%; the clicks use true screen coords so zoom didn't matter. */
    for (int i = 0; i < magSteps; i++) MagZoomKey(+1);

    /* Redraw the boxgrid overlay and tidy up; cursor returns to XEROX. */
    SetCursorPos(savePos.x, savePos.y);
    InvalidateSect(2);
    SaveIni();
    SetForegroundWindow(wMain);   /* bring AwSim back to the front */

    /* Done — repaint the boxgrid section to clear the WORKING image off XEROX. */
    InvalidateSect(2);
    UpdateWindow(wMain);
}

static void ColToggle(int col) {
    /* Cycle the whole column: Active → Disabled → Reserve → Active. */
    BOOL anyActive = FALSE;
    BOOL anyDisabled = FALSE;
    for (int r = 0; r < GROWS; r++)
        if (boxState[r*GCOLS + col] == BOX_ACTIVE) anyActive = TRUE;
        else if (boxState[r*GCOLS + col] == BOX_DISABLED) anyDisabled = TRUE;
    BYTE nextState = anyActive ? BOX_DISABLED
                    : anyDisabled ? BOX_RESERVE
                    : BOX_ACTIVE;
    for (int r = 0; r < GROWS; r++) {
        int idx = r*GCOLS + col;
        boxState[idx] = nextState;
    }
    InvalidateSect(2);
    SaveIni();
}

/* ═══════════════════════════════════════════════════════════════
   MOUSE HELPERS
═══════════════════════════════════════════════════════════════ */
static BOOL InputWait(DWORD ms) {
    if (running && hStop)
        return WaitForSingleObject(hStop, ms) != WAIT_OBJECT_0;
    Sleep(ms);
    return TRUE;
}
static void DoClick(int x, int y) {
    if (running && !EnsureGameForeground()) return;
    if (!InputWait(60)) return;
    if (!SetCursorPos(x, y)) return;
    if (!InputWait(50)) return;
    mouse_event(MOUSEEVENTF_LEFTDOWN, 0,0,0,0);
    if (!InputWait(60)) { mouse_event(MOUSEEVENTF_LEFTUP, 0,0,0,0); return; }
    mouse_event(MOUSEEVENTF_LEFTUP, 0,0,0,0);
}
static void DoDbl(int x, int y) {
    if (running && !EnsureGameForeground()) return;
    if (!InputWait(60)) return;
    if (!SetCursorPos(x, y)) return;
    if (!InputWait(50)) return;
    mouse_event(MOUSEEVENTF_LEFTDOWN, 0,0,0,0);
    if (!InputWait(50)) { mouse_event(MOUSEEVENTF_LEFTUP, 0,0,0,0); return; }
    mouse_event(MOUSEEVENTF_LEFTUP, 0,0,0,0);
    if (!InputWait(90)) return;
    mouse_event(MOUSEEVENTF_LEFTDOWN, 0,0,0,0);
    if (!InputWait(50)) { mouse_event(MOUSEEVENTF_LEFTUP, 0,0,0,0); return; }
    mouse_event(MOUSEEVENTF_LEFTUP, 0,0,0,0);
}
static void Center(int idx, int *ox, int *oy) {
    int c = idx % GCOLS, r = idx / GCOLS;
    *ox = gx + (int)((c + 0.5) * gw / (double)GCOLS);
    *oy = gy + (int)((r + 0.5) * gh / (double)GROWS);
}

/* ═══════════════════════════════════════════════════════════════
   KEY HELPER  — sends a single virtual-key press+release via SendInput
═══════════════════════════════════════════════════════════════ */
static void SendKey(WORD vk) {
    if (running && !EnsureGameForeground()) return;
    if (running && hStop && WaitForSingleObject(hStop, 0) == WAIT_OBJECT_0) return;
    INPUT inp[2];
    memset(inp, 0, sizeof inp);
    inp[0].type       = INPUT_KEYBOARD;
    inp[0].ki.wVk     = vk;
    inp[1]            = inp[0];
    inp[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inp, sizeof(INPUT));
    InputWait(40);
}

/* ═══════════════════════════════════════════════════════════════
   SHARED ENGINE HELPERS
   ─────────────────────────────────────────────────────────────
   RowBtnXY: 12 row buttons sit left of the boxgrid (MR/SL/SR ×
   rows 1-4). The user calibrates two corners — MR1 (top-left) and
   SR4 (bottom-right) — and every other centre is interpolated.
   col: 0=Mirror 1=ShiftLeft 2=ShiftRight   row: 0=row1 … 3=row4.
   ColIsActive: true if a grid column holds at least one checked cell.
═══════════════════════════════════════════════════════════════ */
static void RowBtnXY(int col, int row, int *x, int *y) {
    /* x: interpolate between left-col (rb1x) and right-col (rb2x) */
    *x = rb1x + (int)((double)col * (rb2x - rb1x) / 2.0);
    /* y: fixed offsets from grid top (gy), matching boxgrid row centres */
    static const int ROW_DY[4] = { 20, 58, 97, 135 };
    *y = gy + ROW_DY[row];
}

static int RowButtonColorKind(COLORREF px) {
    int r = GetRValue(px), g = GetGValue(px), b = GetBValue(px);
    if (r >= 115 && r > g * 3 / 2 && r > b * 3 / 2) return 1;
    if (b >= 90 && b > r * 5 / 4 && b > g * 6 / 5) return 2;
    return 0;
}

static int RowButtonRectCount(const int *integral, int stride,
                              int x1, int y1, int x2, int y2) {
    return integral[y2*stride+x2] - integral[y1*stride+x2]
         - integral[y2*stride+x1] + integral[y1*stride+x1];
}

static BOOL AutoCalibrateRowButtons(void) {
    if (!gridSet || gw < 90 || gh < 40) return FALSE;
    int cellW = gw / GCOLS;
    int cellH = gh / GROWS;
    int sideLo = (cellH * 50) / 100;
    int sideHi = (cellH * 75) / 100;
    if (sideLo < 8) sideLo = 8;
    if (sideHi < sideLo) sideHi = sideLo;
    int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int right = left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int bottom = top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    int scanX = gx - 2*cellW;
    if (scanX < left) scanX = left;
    int scanRight = gx;
    if (scanRight > right) scanRight = right;
    int rowY[4];
    for (int row = 0; row < 4; row++)
        rowY[row] = gy + (int)((row + 0.5) * gh / GROWS);
    int scanY = rowY[0] - sideHi/2 - 2;
    int scanBottom = rowY[3] + sideHi/2 + 2;
    if (scanX >= scanRight || scanY < top || scanBottom > bottom) return FALSE;
    int scanW = scanRight - scanX;
    int scanH = scanBottom - scanY;
    if (scanW <= 0 || scanW > 1024 || scanH <= 0 || scanH > 512)
        return FALSE;
    HDC screen = GetDC(NULL);
    if (!screen) return FALSE;
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = memory ? CreateCompatibleBitmap(screen, scanW, scanH) : NULL;
    if (!memory || !bitmap) {
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        ReleaseDC(NULL, screen);
        return FALSE;
    }
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memory, bitmap);
    if (!oldBitmap || oldBitmap == HGDI_ERROR ||
        !BitBlt(memory, 0, 0, scanW, scanH, screen, scanX, scanY, SRCCOPY)) {
        if (oldBitmap && oldBitmap != HGDI_ERROR) SelectObject(memory, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(memory);
        ReleaseDC(NULL, screen);
        return FALSE;
    }

    BITMAPINFO info;
    memset(&info, 0, sizeof info);
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = scanW;
    info.bmiHeader.biHeight = -scanH;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    size_t pixelBytes = (size_t)scanW * (size_t)scanH * 4;
    unsigned char *pixels = (unsigned char *)malloc(pixelBytes);
    if (!pixels) {
        SelectObject(memory, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(memory);
        ReleaseDC(NULL, screen);
        return FALSE;
    }
    SelectObject(memory, oldBitmap);
    int copiedRows = GetDIBits(screen, bitmap, 0, (UINT)scanH, pixels,
                               &info, DIB_RGB_COLORS);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(NULL, screen);
    if (copiedRows != scanH) { free(pixels); return FALSE; }

    int integralStride = scanW + 1;
    size_t integralCount = (size_t)integralStride * (scanH + 1);
    int *redIntegral = (int *)calloc(integralCount, sizeof(int));
    int *blueIntegral = (int *)calloc(integralCount, sizeof(int));
    if (!redIntegral || !blueIntegral) {
        free(redIntegral); free(blueIntegral); free(pixels);
        return FALSE;
    }
    for (int y = 1; y <= scanH; y++) {
        int redRow = 0, blueRow = 0;
        for (int x = 1; x <= scanW; x++) {
            const unsigned char *pixel = pixels + (size_t)(y-1)*scanW*4
                                       + (size_t)(x-1)*4;
            int kind = RowButtonColorKind(RGB(pixel[2], pixel[1], pixel[0]));
            redRow += kind == 1;
            blueRow += kind == 2;
            redIntegral[y*integralStride+x] =
                redIntegral[(y-1)*integralStride+x] + redRow;
            blueIntegral[y*integralStride+x] =
                blueIntegral[(y-1)*integralStride+x] + blueRow;
        }
    }
    free(pixels);

    double bestScore = -1.0;
    int bestMirrorX = 0, bestShiftLeftX = 0, bestShiftRightX = 0;
    int bestSide = 0, bestGap = 0;
    double bestMinDensity = 0.0;
    for (int side = sideLo; side <= sideHi; side++) {
        int half = side / 2;
        int mirrorMin = scanX + half;
        int mirrorMax = gx - side/2 - side;
        int maxGap = side / 4;
        for (int mirrorX = mirrorMin; mirrorX <= mirrorMax; mirrorX++)
            for (int gap = 0; gap <= maxGap; gap++) {
                double score = 0.0, minDensity = 1.0;
                int validRows = 0;
                int pairX1 = mirrorX + half + gap;
                int pairX2 = pairX1 + 2*side;
                for (int row = 0; row < GROWS; row++) {
                    int y1 = rowY[row] - half - scanY;
                    int y2 = y1 + side;
                    int sqX1 = mirrorX - half - scanX;
                    int sqX2 = sqX1 + side;
                    int px1 = pairX1 - scanX;
                    int px2 = pairX2 - scanX;
                    int py1 = y1, py2 = y2;
                    if (sqX1 < 0 || px2 > scanW || py1 < 0 || py2 > scanH)
                        continue;
                    int squareArea = side*side;
                    int pairArea = 2*side*side;
                    int squareRed = RowButtonRectCount(redIntegral, integralStride,
                                                       sqX1, py1, sqX2, py2);
                    int squareBlue = RowButtonRectCount(blueIntegral, integralStride,
                                                        sqX1, py1, sqX2, py2);
                    int pairRed = RowButtonRectCount(redIntegral, integralStride,
                                                     px1, py1, px2, py2);
                    int pairBlue = RowButtonRectCount(blueIntegral, integralStride,
                                                      px1, py1, px2, py2);
                    int squareColor = squareRed >= squareBlue ? 1 : 2;
                    double squareDensity = (double)(squareColor == 1 ? squareRed : squareBlue)
                                         / squareArea;
                    int pairColorCount = squareColor == 1 ? pairRed : pairBlue;
                    double pairDensity = (double)pairColorCount / pairArea;
                    if (squareDensity < 0.20 || pairDensity < 0.18) continue;
                    double rowScore = squareDensity + pairDensity;
                    score += rowScore;
                    if (squareDensity < minDensity) minDensity = squareDensity;
                    if (pairDensity < minDensity) minDensity = pairDensity;
                    validRows++;
                }
                if (validRows == GROWS && minDensity >= 0.18 && score > bestScore) {
                    bestScore = score;
                    bestMirrorX = mirrorX;
                    bestSide = side;
                    bestGap = gap;
                    bestShiftLeftX = pairX1 + half;
                    bestShiftRightX = pairX2 - half;
                    bestMinDensity = minDensity;
                }
            }
    }
    free(redIntegral);
    free(blueIntegral);

    if (bestScore < 0.0) {
        char diagnostic[192];
        snprintf(diagnostic, sizeof diagnostic,
                 "Row Ops shape scan failed: grid=(%d,%d %dx%d), searchX=%d..%d side=%d..%d.",
                 gx, gy, gw, gh, scanX, scanRight, sideLo, sideHi);
        LogApp(diagnostic);
        SetAppStatus("Row buttons not detected; manual setup needed");
        return FALSE;
    }
    rb1x = bestMirrorX; rb1y = rowY[0]; rb1Set = TRUE;
    rb2x = bestShiftRightX; rb2y = rowY[3]; rb2Set = TRUE;
    SaveIni();
    char diagnostic[192];
    snprintf(diagnostic, sizeof diagnostic,
             "Row Ops shape scan found mirror=(%d,%d), shift-left-x=%d, shift-right=(%d,%d), side=%d gap=%d density=%.2f.",
             rb1x, rb1y, bestShiftLeftX, rb2x, rb2y,
             bestSide, bestGap, bestMinDensity);
    LogApp(diagnostic);
    return TRUE;
}

/* True if column col (0-indexed) has at least one active cell. */
static BOOL ColIsActive(int col) {
    for (int r = 0; r < GROWS; r++)
        if (boxState[r * GCOLS + col] == BOX_ACTIVE) return TRUE;
    return FALSE;
}


/* ═══════════════════════════════════════════════════════════════
    COLUMN OPS
   For each active column: ShiftUp ×3, refresh.
═══════════════════════════════════════════════════════════════ */
static BOOL ColOpsPass(BOOL advanced) {
    int btnY = gy - 15;
    for (int col = 0; col < GCOLS; col++) {
        if (!ColIsActive(col)) continue;
        int sdX = gx + 54 + (int)((double)col * gw / GCOLS);  /* ShiftUp  */
        if (advanced) {
            int flipX = gx + 27 + (int)((double)col * gw / GCOLS);
            DoClick(flipX, btnY);
            if (WaitForSingleObject(hStop, 3333) == WAIT_OBJECT_0) return FALSE;
        }

        for (int k = 0; k < 3; k++) {
            DoClick(sdX, btnY);
            if (WaitForSingleObject(hStop, 3333) == WAIT_OBJECT_0) return FALSE;
        }
        DoDbl(tgtX[activeTgt], tgtY[activeTgt]);
        if (WaitForSingleObject(hStop, 500) == WAIT_OBJECT_0) return FALSE;
    }
    return TRUE;
}

static BOOL RunColOps(void) {
    if (!ColOpsPass(FALSE)) return FALSE;
    if (engVars[7] != '0' && !ColOpsPass(TRUE)) return FALSE;
    DoDbl(tgtX[activeTgt], tgtY[activeTgt]);
    if (WaitForSingleObject(hStop, 500) == WAIT_OBJECT_0) return FALSE;
    return TRUE;
}

/* ═══════════════════════════════════════════════════════════════
    ROW OPS
    Rows 4→1: DoubleClick(SR), click(SL), DoubleClick(SL), click(SL),
     then a Target refresh.
═══════════════════════════════════════════════════════════════ */
static BOOL RowOpsPass(BOOL advanced) {
    for (int row = 3; row >= 0; row--) {
        int mrX, mrY, srX, srY, slX, slY;
        if (advanced) {
            RowBtnXY(0, row, &mrX, &mrY);
            DoClick(mrX, mrY);
            if (WaitForSingleObject(hStop, 3333) == WAIT_OBJECT_0) return FALSE;
        }
        RowBtnXY(2, row, &srX, &srY);
        RowBtnXY(1, row, &slX, &slY);

        DoDbl(srX, srY);
        if (WaitForSingleObject(hStop, 3333) == WAIT_OBJECT_0) return FALSE;
        DoClick(slX, slY);
        if (WaitForSingleObject(hStop, 3333) == WAIT_OBJECT_0) return FALSE;
        DoDbl(slX, slY);
        if (WaitForSingleObject(hStop, 3333) == WAIT_OBJECT_0) return FALSE;
        DoClick(slX, slY);
        if (WaitForSingleObject(hStop, 3333) == WAIT_OBJECT_0) return FALSE;
        DoDbl(tgtX[activeTgt], tgtY[activeTgt]);
        if (WaitForSingleObject(hStop, 500) == WAIT_OBJECT_0) return FALSE;
    }
    return TRUE;
}

static BOOL RunRowOps(void) {
    /* Game focus and double-click priming are already done by AutoRun's
       opening double-click, so RowOpsPass can start directly. */
    if (!RowOpsPass(FALSE)) return FALSE;
    if (engVars[8] != '0' && !RowOpsPass(TRUE)) return FALSE;
    DoDbl(tgtX[activeTgt], tgtY[activeTgt]);
    if (WaitForSingleObject(hStop, 500) == WAIT_OBJECT_0) return FALSE;
    return TRUE;
}

/* ═══════════════════════════════════════════════════════════════
    PERMUTE
   Calibrate two buttons (Swap 1-2, Swap 3-4); Swap 2-3 is their
    midpoint. Run 23 swaps; Advanced sends 7 and repeats the swaps.
═══════════════════════════════════════════════════════════════ */
static BOOL RunPermute(void) {
    /* rs23 derived as midpoint of rs12 and rs34 */
    int bx[3] = { rs12x, (rs12x+rs34x)/2, rs34x };
    int by[3] = { rs12y, (rs12y+rs34y)/2, rs34y };

    /* 23-button sequence then Target (0=Swap1-2, 1=Swap2-3, 2=Swap3-4) */
    static const int SEQ[23] = {
        0,1,0,1, 2,1,2,1,
        0,1,0,1, 2,1,2,1,
        0,1,0,1, 2,1,2
    };
    int passes = (engVars[9] != '0') ? 2 : 1;
    for (int pass = 0; pass < passes; pass++) {
        if (pass == 1) {
            SendKey(0x37);
            if (WaitForSingleObject(hStop, 3333) == WAIT_OBJECT_0) return FALSE;
        }
        for (int i = 0; i < 23; i++) {
            DoClick(bx[SEQ[i]], by[SEQ[i]]);
            if (WaitForSingleObject(hStop, 3333) == WAIT_OBJECT_0) return FALSE;
        }
    }
    DoDbl(tgtX[activeTgt], tgtY[activeTgt]);
    if (WaitForSingleObject(hStop, 500) == WAIT_OBJECT_0) return FALSE;
    return TRUE;
}

/* ── Scout — fixed keystroke sequence ─────────────────────── */
static BOOL RunScout(void) {
#define E4K(vk) do { SendKey(vk); \
    if (WaitForSingleObject(hStop,3333)==WAIT_OBJECT_0) return FALSE; } while(0)

    /* Standard pattern. */
    for (int i = 0; i < 8; i++) E4K(0x34);
    E4K(0x37);
    for (int i = 0; i < 8; i++) E4K(0x34);

    /* Advanced adds a second pattern after the standard one. */
    if (engVars[10] != '0') {
        E4K(0x39);
        for (int i = 0; i < 8; i++) E4K(0x34);
        E4K(0x37);
        for (int i = 0; i < 8; i++) E4K(0x34);
    }

    DoDbl(tgtX[activeTgt], tgtY[activeTgt]);
    if (WaitForSingleObject(hStop, 500) == WAIT_OBJECT_0) return FALSE;

    /* Remaining pattern; the two 6 presses have no long pause between them. */
    for (int i = 0; i < 3; i++) E4K(0x38);
    E4K(0x34);
    for (int i = 0; i < 2; i++) E4K(0x32);
    SendKey(0x36);
    SendKey(0x36);
    if (WaitForSingleObject(hStop,3333)==WAIT_OBJECT_0) return FALSE;
    for (int i = 0; i < 2; i++) E4K(0x38);
    DoDbl(tgtX[activeTgt], tgtY[activeTgt]);

#undef E4K
    return TRUE;
}

/* ═══════════════════════════════════════════════════════════════
   AUTOMATION THREAD  — two-phase dispatcher
   ─────────────────────────────────────────────────────────────
    Phase 1: Active FS engines [3..6] run once each, in run-order rank.
   Phase 2: Active CR engine  [1..2] (AwSim/Decoy) runs indefinitely;
            if none is selected, the thread stops after Phase 1.
═══════════════════════════════════════════════════════════════ */
DWORD WINAPI AutoRun(LPVOID _u) {
    (void)_u;
    srand((unsigned)time(NULL) ^ GetCurrentThreadId());

    static const int MV_ORG[5] = { 0, 1, 2, 0, 1 };
    static const int MV_DST[5] = { 1, 2, 0, 1, 2 };

     /* Allow the UI thread to finish restoring game focus after minimizing. */
     if (WaitForSingleObject(hStop, 1000) == WAIT_OBJECT_0) goto stop;

    /* The UI thread focused the game directly from the Start click. Do not
       attempt to steal foreground focus again from this worker thread. */
    if (!GameIsForeground()) {
        LogApp("Automation stopped: game was not foreground after AwSim minimized.");
        goto stop;
    }
    if (WaitForSingleObject(hStop, 500) == WAIT_OBJECT_0) goto stop;

    /* ── Phase 1: FS engines in run-order value (1 = first) ─── */
    for (int ord = 1; ord <= 4; ord++) {
        for (int fsi = 3; fsi <= 6; fsi++) {
            if ((engVars[fsi] - '0') != ord) continue;
            switch (fsi) {
            case 3:  if (!RunColOps()) goto stop; break;
            case 4:  if (!RunRowOps()) goto stop; break;
            case 5:  if (!RunPermute()) goto stop; break;
            case 6:  if (!RunScout()) goto stop; break;
            }
        }
    }

    /* ── Phase 2: CR engine runs indefinitely (or stop if none) ─── */
    {
        int crEngine = 0;
        for (int i = 1; i <= 2; i++)
            if (engVars[i] != '0') { crEngine = i; break; }
        if (!crEngine) goto stop;

        int  mpc        = engVars[11] - '0';
        int  need       = (mpc == 1) ? 2 : 3;
        BOOL firstCycle = TRUE;

        for (;;) {
            int aList[NCELLS], na = 0;
            int iList[NCELLS], ni = 0;
            for (int i = 0; i < NCELLS; i++) {
                if (boxState[i] == BOX_ACTIVE) aList[na++] = i;
                else if (boxState[i] == BOX_RESERVE) iList[ni++] = i;
            }

            BOOL ok;
            switch (crEngine) {
            case 2: ok = (na >= need-1) && (ni >= 1); break;
            default: ok = (na >= need); break;
            }
            if (!ok) {
                if (WaitForSingleObject(hStop, 500) == WAIT_OBJECT_0) goto stop;
                continue;
            }

            int box[3] = {-1,-1,-1};
            int tmpA[NCELLS];
            memcpy(tmpA, aList, na * sizeof(int));

            switch (crEngine) {
            case 2: {
                int j = rand() % na;
                box[0] = tmpA[j];  tmpA[j] = tmpA[--na];
                box[1] = iList[rand() % ni];
                if (need == 3) box[2] = tmpA[rand() % na];
                break;
            }
            default: {
                for (int i = 0; i < need; i++) {
                    int j = i + rand() % (na - i);
                    int t = tmpA[i]; tmpA[i] = tmpA[j]; tmpA[j] = t;
                }
                for (int i = 0; i < need; i++) box[i] = tmpA[i];
                break;
            }
            }

            int bx[3], by[3];
            for (int i = 0; i < need; i++) Center(box[i], &bx[i], &by[i]);

            if (firstCycle) {
                firstCycle = FALSE;
                SendKey(0x41);  SendKey(0x58);  SendKey(0x58);
                if (WaitForSingleObject(hStop, 250) == WAIT_OBJECT_0) goto stop;
            }

            for (int m = 0; m < mpc; m++) {
                int oi = MV_ORG[m], di = MV_DST[m];
                DoClick(bx[oi], by[oi]);
                if (WaitForSingleObject(hStop, 0) == WAIT_OBJECT_0) goto stop;
                SendKey(0x41);
                if (WaitForSingleObject(hStop, 0) == WAIT_OBJECT_0) goto stop;
                DoClick(bx[di], by[di]);
                if (WaitForSingleObject(hStop, 0) == WAIT_OBJECT_0) goto stop;
                SendKey(0x41);
                if (WaitForSingleObject(hStop, 3333) == WAIT_OBJECT_0) goto stop;
            }

            DoDbl(tgtX[activeTgt], tgtY[activeTgt]);
            if (WaitForSingleObject(hStop, 500) == WAIT_OBJECT_0) goto stop;
        }
    }

stop:
    PostMessage(wMain, WM_STOPAUTO, 0, 0);   /* let main thread run StopAuto */
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
   LOW-LEVEL MOUSE HOOK
═══════════════════════════════════════════════════════════════ */
LRESULT CALLBACK ProcHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && running) {
        MSLLHOOKSTRUCT *ms = (MSLLHOOKSTRUCT*)lp;
        if (wp == WM_RBUTTONDOWN) {
            PostMessage(wMain, WM_STOPAUTO, 0, 0);  return 1;
        }
        if (wp == WM_MOUSEMOVE && !(ms->flags & LLMHF_INJECTED)) {
            int edgeX = GetSystemMetrics(SM_XVIRTUALSCREEN) + EDGE_X;
            if (ms->pt.x > edgeX) edgeExitArmed = TRUE;
            else if (edgeExitArmed) {
                char diagnostic[128];
                snprintf(diagnostic, sizeof diagnostic,
                         "Emergency exit triggered by cursor reaching left edge at (%ld,%ld).",
                         ms->pt.x, ms->pt.y);
                LogApp(diagnostic);
                edgeExitArmed = FALSE;
                PostMessage(wMain, WM_EXITAPP, 0, 0);
                return 1;
            }
        }
    }
    return CallNextHookEx(hHook, code, wp, lp);
}

/* ═══════════════════════════════════════════════════════════════
   START / STOP AUTOMATION
═══════════════════════════════════════════════════════════════ */
static void StartAuto(void) {
    if (running) return;
    /* Grid capture is needed by CR engines, Column Ops and Row Ops. */
    {
        BOOL needsGrid = FALSE;
        for (int i = 1; i <= 2; i++) if (engVars[i] != '0') { needsGrid = TRUE; break; }
        if (engVars[3] != '0') needsGrid = TRUE;
        if (engVars[4] != '0') needsGrid = TRUE;
        if (needsGrid && !gridSet) {
            char diagnostic[192];
            snprintf(diagnostic, sizeof diagnostic,
                     "Start grid retry: saved=(%d,%d %dx%d), desktop=(%d,%d %dx%d).",
                     gx, gy, gw, gh,
                     GetSystemMetrics(SM_XVIRTUALSCREEN),
                     GetSystemMetrics(SM_YVIRTUALSCREEN),
                     GetSystemMetrics(SM_CXVIRTUALSCREEN),
                     GetSystemMetrics(SM_CYVIRTUALSCREEN));
            LogApp(diagnostic);
            if (DetectGridColdStart()) {
                SaveIni();
                LogApp("Start grid retry succeeded; using automatically detected calibration.");
            } else {
                LogApp("Start grid retry failed; requesting manual corner capture.");
                pendingStart = TRUE;
                SetAppStatus("Grid calibration required");
                StartCap(1);
                return;
            }
        }
    }
    /* Capture active target coord if not set */
    if (!tgtSet[activeTgt]) {
        pendingStart = TRUE; SetAppStatus("Target calibration required");
        StartCap(2 + activeTgt); return;
    }
    /* Row button calibration for Row Ops. */
    if (engVars[4] != '0') {
        BOOL haveSavedRowCalibration = rb1Set && rb2Set;
        if (!AutoCalibrateRowButtons() && !haveSavedRowCalibration) {
            LogApp("Automatic Row Ops button detection failed; requesting manual calibration.");
        }
        if (!rb1Set) { pendingStart = TRUE; SetAppStatus("Row button calibration required"); StartCap(7); return; }
        if (!rb2Set) { pendingStart = TRUE; SetAppStatus("Row button calibration required"); StartCap(8); return; }
    }
    /* Permute (swap) calibration. */
    if (engVars[5] != '0') {
        if (!rs12Set) { pendingStart = TRUE; SetAppStatus("Swap button calibration required"); StartCap(9);  return; }
        if (!rs34Set) { pendingStart = TRUE; SetAppStatus("Swap button calibration required"); StartCap(10); return; }
    }
    /* Which engines are active? Verify minimums per type. */
    int crEngine = 0;
    for (int i = 1; i <= 2; i++) if (engVars[i] != '0') { crEngine = i; break; }
    BOOL hasFS = FALSE;
    for (int i = 3; i <= 6; i++) if (engVars[i] != '0') { hasFS = TRUE; break; }
    if (!crEngine && !hasFS) { SetAppStatus("Select an engine first"); return; }

    if (crEngine) {
        int mpc  = engVars[11] - '0';
        int need = (mpc == 1) ? 2 : 3;
        int na = 0, ni = 0;
        for (int i = 0; i < NCELLS; i++) {
            if (boxState[i] == BOX_ACTIVE) na++;
            else if (boxState[i] == BOX_RESERVE) ni++;
        }
        switch (crEngine) {
        case 2:  if (na < need-1 || ni < 1) { SetAppStatus("Boxgrid has too few usable cells"); return; } break;
        default: if (na < need)             { SetAppStatus("Boxgrid has too few active cells"); return; } break;
        }
    }
    /* Column Ops needs at least one active column. */
    if (engVars[3] != '0') {
        BOOL any = FALSE;
        for (int i = 0; i < NCELLS; i++)
            if (boxState[i] == BOX_ACTIVE) { any = TRUE; break; }
        if (!any) { SetAppStatus("Column Ops needs an active cell"); return; }
    }
    /* Start is called from the user's click, so foreground handoff is allowed
       here. A worker thread after minimizing is not reliably allowed to steal it. */
    if (!FocusGameTab()) {
        SetAppStatus("Click the game once, then press Start");
        LogApp("Automation not started: game focus handoff failed on Start click.");
        return;
    }
    /* All prerequisites met — launch thread */
    hStop   = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!hStop) { SetAppStatus("Could not create stop event"); LogApp("CreateEvent failed while starting automation."); return; }
    hHook   = SetWindowsHookExA(WH_MOUSE_LL, ProcHook, hI, 0);
    if (!hHook) {
        CloseHandle(hStop); hStop = NULL;
        SetAppStatus("Could not install stop hook"); LogApp("SetWindowsHookEx failed while starting automation.");
        return;
    }
    edgeExitArmed = FALSE;
    POINT startCursor;
    if (GetCursorPos(&startCursor))
        edgeExitArmed = startCursor.x > GetSystemMetrics(SM_XVIRTUALSCREEN) + EDGE_X;
    running = TRUE;
    SetAppStatus("Running - right-click to stop");
    InvalidateSect(0);    /* paint RIGHT CLICK / TO STOP over Start button */
    SetWindowPos(wMain, HWND_NOTOPMOST, 0,0,0,0,
                 SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    hThread = CreateThread(NULL, 0, AutoRun, NULL, 0, NULL);
    if (!hThread) {
        running = FALSE;
        UnhookWindowsHookEx(hHook); hHook = NULL;
        CloseHandle(hStop); hStop = NULL;
        SetWindowPos(wMain, HWND_TOPMOST, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        SetAppStatus("Could not start automation thread");
        LogApp("CreateThread failed while starting automation.");
        InvalidateSect(0);
        return;
    }
    ShowWindow(wMain, SW_MINIMIZE);   /* minimise only after successful launch */
    if (gameWindow) {
        BOOL focused = FALSE;
        for (int attempt = 0; attempt < 10; attempt++) {
            SetForegroundWindow(gameWindow);
            Sleep(50);
            if (GameIsForeground()) { focused = TRUE; break; }
        }
        if (!focused) {
            LogApp("Could not retain game foreground after minimizing AwSim; requesting stop.");
            SetEvent(hStop);
        }
    }
}

static BOOL StopAuto(void) {
    if (!running) return TRUE;
    SetEvent(hStop);
    if (WaitForSingleObject(hThread, 0) != WAIT_OBJECT_0) {
        SetAppStatus("Stopping automation...");
        SetTimer(wMain, IDT_STOP_POLL, 50, NULL);
        return FALSE;
    }
    KillTimer(wMain, IDT_STOP_POLL);
    CloseHandle(hThread); hThread = NULL;
    CloseHandle(hStop);   hStop   = NULL;
    if (hHook) { UnhookWindowsHookEx(hHook); hHook = NULL; }
    running = FALSE;
    InvalidateSect(0);    /* restore Start button */
    /* Restore window, make always-on-top while idle */
    ShowWindow(wMain, SW_RESTORE);
    SetWindowPos(wMain, HWND_TOPMOST, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    SetForegroundWindow(wMain);
    SetAppStatus("Ready");
    return TRUE;
}

/* ═══════════════════════════════════════════════════════════════
   CAPTURE OVERLAY
═══════════════════════════════════════════════════════════════ */
static void StartCap(int mode) {
    if (wCap) return;
    capMode = mode;  capStep = 0;
    int sx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int sy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int sw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int sh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    wCap = CreateWindowExA(WS_EX_TOPMOST | WS_EX_LAYERED,
                           CLS_CAP, "", WS_POPUP,
                           sx, sy, sw, sh, NULL, NULL, hI, NULL);
    if (!wCap) {
        pendingStart = FALSE; pendingXerox = FALSE;
        LogApp("Could not create calibration overlay.");
        SetAppStatus("Could not open calibration overlay");
        return;
    }
    SetAppStatus("Calibration in progress");
    SetLayeredWindowAttributes(wCap, 0, 140, LWA_ALPHA);
    ShowWindow(wCap, SW_SHOW);
    SetForegroundWindow(wCap);
    SetFocus(wCap);
    InvalidateSect(0);    /* highlight Setup button while capturing */
}

static void EndCap(void) {
    if (!wCap) return;
    DestroyWindow(wCap);  wCap = NULL;  capMode = 0;
    InvalidateSect(0);
    if (!pendingStart && !pendingXerox) SetAppStatus("Ready");
    if (app.taskbarPromptPending) {
        app.taskbarPromptPending = FALSE;
        PostMessage(wMain, WM_TASKBARPROMPT, 0, 0);
    }
    if (pendingStart) {
        pendingStart = FALSE;
        PostMessage(wMain, WM_STARTDEFER, 0, 0);
    } else if (pendingXerox) {
        pendingXerox = FALSE;
        PostMessage(wMain, WM_XEROXDEFER, 0, 0);
    }
}

/* XEROX entry point: ensure the boxgrid is calibrated (capturing it if
   missing) before running the copy. The capture re-enters here via
   WM_XEROXDEFER once done. Focus is windowless, so no row-button setup. */
static void StartXerox(void) {
    if (running) return;
    if (!gridSet) { pendingXerox = TRUE; SetAppStatus("Grid calibration required"); StartCap(1); return; }
    XeroxFormation();
}

/* ═══════════════════════════════════════════════════════════════
   HELP  —  topic list (left) + reading pane (right)
   To add/edit a topic: edit the HELP[] table below. Use "\n" for a
   line break and "\n\n" for a paragraph gap in the body text.
═══════════════════════════════════════════════════════════════ */
typedef struct { const char *title; const char *body; } HelpTopic;
static const HelpTopic HELP[] = {
    {"Introduction",
     "AwSim for CC:TA improves game play by managing the work of moving units around the battlefield.\n\nUse it before hitting a new target, and between attacks.\n\nInflicted damage will rise over time while RT will typically decrease."},
    {"Getting Started",
     "AwSim drives the TABS V2 battle simulator script from a small always-on-top overlay. TABS V2 is required.\n\nPick a Target, choose an Engine, then press Start.\n\nRight-click to interrupt operation.\n\nFor a quick exit while operating, move cursor to the far left."},
    {"Targets",
     "Five Targets map to the Target columns in TABS V2: CY, DF, Deff, CC and CY*.\n\nClick a tile once to set as the current Target. Follow the prompt and set its coordinates by pointing to the data region of the TABS V2 column of that name.\n\nClick a tile three times to reset its coordinates.\n\nAt the bottom of the TABS V2 window is a button to reset the sim cache. Use this if you are starting with a formation different from the one shown as most favorable for the Target you have selected.\n\nThe current Target is reflected in the corresponding header letter (red.)"},
    {"Engines",
     "Engines are the logic and AwSim/Decoy are the workhorse Engines. Once started, they will run continuously until stopped.\n\nThe difference between the two is that AwSim limits its operation to Active boxes (green check), while Decoy includes Reserve (unmarked) boxes as a destination for transferring units.\n\nThere are four special purpose finite duration Engines which will run in the order selected:\n\n    Column Ops - cycles unit positions in each column.\n    Row Ops - shifts entire rows left and right by 2 positions.\n    Permute - swaps rows for all 24 possible arrangements.\n    Scout - shifts the formation to find an optimal starting position.\n\nEnabling Advanced may improve results by extending operations.\n\nAwSim or Decoy will run last, if selected."},
    {"Boxgrid",
     "The Boxgrid is used to show which of the game boxes are occupied (green check), which are not, and which should be excluded from operations (red X.)\n\nThe button below the Boxgrid on the left toggles commonly used regions.\n\nColumns can be toggled by clicking just above them."},
    {"XEROX Capture",
     "XEROX reads the current unit formation from the game and copies it into the Boxgrid.\n\nWhile XEROX runs, the red WORKING badge covers the button - don't move the mouse until it clears.\n\nCalibration is sensitive and can occasionally be lost. This is a minor issue. If a XEROX scan produces an inaccurate result, click Setup to recalibrate the grid then rerun XEROX."},
    {"Moves Per Cycle",
     "The Moves-Per-Cycle value (1-5) sets how many moves an Engine makes before re-targeting. It applies only to AwSim and Decoy.\n\nClick the '+' button to change the number shown on the left side.\n\nFor shorter sessions of a few minutes an MPC of 1-3 works well."},
    {"Calibration / Setup",
     "The first time AwSim runs it looks at the game screen to locate the formation boxes. If it is unable to do so with confidence you will be prompted to assist by pointing to the grid corners.\n\nYou may also be prompted to identify game screen buttons that control unit movement.\n\nCalibration may be performed any time if needed. Click Setup, then follow the on-screen prompt for each point."},
    {"About",
     "By crackfed. AwSim was made possible in large part by the encouragement and financial backing of its one paid customer. Thank you, balcy."},
};
#define NHELP ((int)(sizeof HELP / sizeof HELP[0]))

#define HLP_W        560
#define HLP_H        360
#define HLP_TITLE_H   26
#define HLP_LIST_W   184
#define HLP_ITEM_H    30
#define HLP_LIST_Y0   34
#define HLP_PAD       14

static void OpenHelp(void) {
    if (wHelp) { SetForegroundWindow(wHelp); return; }
    /* Centre over the main window, clamped to the screen. */
    RECT mr;  GetWindowRect(wMain, &mr);
    int cx = (mr.left + mr.right) / 2 - HLP_W / 2;
    int cy = (mr.top  + mr.bottom) / 2 - HLP_H / 2;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    if (cx + HLP_W > sw) cx = sw - HLP_W;
    if (cy + HLP_H > sh) cy = sh - HLP_H;
    wHelp = CreateWindowExA(WS_EX_TOPMOST, CLS_HELP, "", WS_POPUP,
                            cx, cy, HLP_W, HLP_H, wMain, NULL, hI, NULL);
    ShowWindow(wHelp, SW_SHOW);
    SetForegroundWindow(wHelp);
    SetFocus(wHelp);
}

static void CloseHelp(void) {
    if (!wHelp) return;
    DestroyWindow(wHelp);  wHelp = NULL;
    SetForegroundWindow(wMain);
}

/* Rectangle of one topic-list item. */
static RECT HelpItemRect(int i) {
    RECT r = { 8, HLP_LIST_Y0 + i * HLP_ITEM_H,
               HLP_LIST_W - 8, HLP_LIST_Y0 + i * HLP_ITEM_H + HLP_ITEM_H - 4 };
    return r;
}

LRESULT CALLBACK ProcHelp(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT rc;  GetClientRect(w, &rc);

        /* Background + steel-blue frame. */
        HBRUSH bg = CreateSolidBrush(RGB(12,12,14));
        FillRect(dc, &rc, bg);  DeleteObject(bg);
        HPEN fr = CreatePen(PS_SOLID, 1, RGB(96,142,161));
        HPEN ofr = (HPEN)SelectObject(dc, fr);
        HBRUSH ob = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, 0, 0, rc.right, rc.bottom);
        SelectObject(dc, ob);  SelectObject(dc, ofr);  DeleteObject(fr);

        /* Title strip. */
        RECT ts = { 1, 1, rc.right - 1, HLP_TITLE_H };
        HBRUSH tb = CreateSolidBrush(RGB(20,20,22));
        FillRect(dc, &ts, tb);  DeleteObject(tb);
        SetBkMode(dc, TRANSPARENT);

        HFONT ft = CreateFontA(15,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,
                               CLEARTYPE_QUALITY,0,"Segoe UI");
        HFONT oft = (HFONT)SelectObject(dc, ft);
        SetTextColor(dc, RGB(84,206,224));
        RECT tt = { 10, 1, 300, HLP_TITLE_H };
        DrawTextA(dc, "AwSim \x97 Help", -1, &tt, DT_VCENTER|DT_SINGLELINE);
        /* Close X. */
        SetTextColor(dc, RGB(255,17,17));
        RECT xr = { rc.right - 26, 1, rc.right - 2, HLP_TITLE_H };
        DrawTextA(dc, "X", -1, &xr, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        SelectObject(dc, oft);  DeleteObject(ft);

        /* Divider between list and pane. */
        HPEN dv = CreatePen(PS_SOLID, 1, RGB(42,47,51));
        HPEN odv = (HPEN)SelectObject(dc, dv);
        MoveToEx(dc, HLP_LIST_W, HLP_TITLE_H, NULL);
        LineTo  (dc, HLP_LIST_W, rc.bottom);
        SelectObject(dc, odv);  DeleteObject(dv);

        /* Topic list. */
        HFONT fl = CreateFontA(14,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,
                               CLEARTYPE_QUALITY,0,"Segoe UI");
        HFONT ofl = (HFONT)SelectObject(dc, fl);
        for (int i = 0; i < NHELP; i++) {
            RECT ir = HelpItemRect(i);
            if (i == helpSel) {
                HBRUSH sb = CreateSolidBrush(RGB(26,13,13));
                FillRect(dc, &ir, sb);  DeleteObject(sb);
                HPEN sp = CreatePen(PS_SOLID, 1, RGB(255,17,17));
                HPEN osp = (HPEN)SelectObject(dc, sp);
                HBRUSH nb = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
                Rectangle(dc, ir.left, ir.top, ir.right, ir.bottom);
                SelectObject(dc, nb);  SelectObject(dc, osp);  DeleteObject(sp);
                SetTextColor(dc, RGB(255,72,72));
            } else {
                SetTextColor(dc, RGB(207,214,218));
            }
            RECT lr = { ir.left + 8, ir.top, ir.right - 4, ir.bottom };
            DrawTextA(dc, HELP[i].title, -1, &lr, DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        }
        SelectObject(dc, ofl);  DeleteObject(fl);

        /* Reading pane. */
        HFONT fh = CreateFontA(17,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,
                               CLEARTYPE_QUALITY,0,"Segoe UI");
        HFONT ofh = (HFONT)SelectObject(dc, fh);
        SetTextColor(dc, RGB(84,206,224));
        RECT ph = { HLP_LIST_W + HLP_PAD, HLP_TITLE_H + 10,
                    rc.right - HLP_PAD, HLP_TITLE_H + 38 };
        DrawTextA(dc, HELP[helpSel].title, -1, &ph, DT_SINGLELINE);
        SelectObject(dc, ofh);  DeleteObject(fh);

        HFONT fb = CreateFontA(14,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,
                               CLEARTYPE_QUALITY,0,"Segoe UI");
        HFONT ofb = (HFONT)SelectObject(dc, fb);
        SetTextColor(dc, RGB(230,230,230));
        RECT pb = { HLP_LIST_W + HLP_PAD, HLP_TITLE_H + 46,
                    rc.right - HLP_PAD, rc.bottom - HLP_PAD };
        DrawTextA(dc, HELP[helpSel].body, -1, &pb,
                  DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
        SelectObject(dc, ofb);  DeleteObject(fb);

        EndPaint(w, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int mx = (short)LOWORD(lp), my = (short)HIWORD(lp);
        RECT cr;  GetClientRect(w, &cr);
        if (my < HLP_TITLE_H && mx >= cr.right - 26) { CloseHelp(); return 0; }
        for (int i = 0; i < NHELP; i++) {
            RECT ir = HelpItemRect(i);
            if (mx >= ir.left && mx <= ir.right && my >= ir.top && my <= ir.bottom) {
                if (helpSel != i) { helpSel = i; InvalidateRect(w, NULL, TRUE); }
                return 0;
            }
        }
        return 0;
    }

    case WM_KEYDOWN:
        if (wp == 0x1B) { CloseHelp(); return 0; }
        if (wp == 0x28 || wp == 0x27) {
            helpSel = (helpSel + 1) % NHELP;  InvalidateRect(w, NULL, TRUE);  return 0;
        }
        if (wp == 0x26 || wp == 0x25) {
            helpSel = (helpSel + NHELP - 1) % NHELP;  InvalidateRect(w, NULL, TRUE);  return 0;
        }
        if (wp == 0x24) { helpSel = 0;         InvalidateRect(w, NULL, TRUE); return 0; }
        if (wp == 0x23)  { helpSel = NHELP - 1; InvalidateRect(w, NULL, TRUE); return 0; }
        return 0;

    case WM_CLOSE:
        CloseHelp();
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

LRESULT CALLBACK ProcCap(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT rc; GetClientRect(w, &rc);
        FillRect(dc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        char txt[256];
        if (capMode == 1 && capStep == 0)
            strcpy(txt, "Click  TOP-LEFT  corner of game grid          [Esc = cancel]");
        else if (capMode == 1 && capStep == 1)
            strcpy(txt, "Click  BOTTOM-RIGHT  corner of game grid      [Esc = cancel]");
        else if (capMode == 7)
            strcpy(txt, "Click the TOP-LEFT row button (MR1)           [Esc = cancel]");
        else if (capMode == 8)
            strcpy(txt, "Click the BOTTOM-RIGHT row button (SR4)       [Esc = cancel]");
        else if (capMode == 9)
            strcpy(txt, "Click the  SWAP ROWS 1-2  button              [Esc = cancel]");
        else if (capMode == 10)
            strcpy(txt, "Click the  SWAP ROWS 3-4  button              [Esc = cancel]");
        else
            sprintf(txt, "Click the  %s  target location in the game     [Esc = cancel]",
                    tgtLabels[capMode - 2]);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255,255,80));
        HFONT f = CreateFontA(32,0,0,0,FW_BOLD,0,0,0,
                              DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Segoe UI");
        HFONT of = (HFONT)SelectObject(dc, f);
        DrawTextA(dc, txt, -1, &rc, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        SelectObject(dc, of);  DeleteObject(f);
        EndPaint(w, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        POINT pt;  GetCursorPos(&pt);
        if (capMode == 1) {
            if (capStep == 0) {
                capP1 = pt;  capStep = 1;
                InvalidateRect(w, NULL, TRUE);
            } else {
                int x1 = pt.x < capP1.x ? pt.x : capP1.x;
                int y1 = pt.y < capP1.y ? pt.y : capP1.y;
                int x2 = pt.x > capP1.x ? pt.x : capP1.x;
                int y2 = pt.y > capP1.y ? pt.y : capP1.y;
                if (x2-x1 < 20 || y2-y1 < 20) {
                    capStep = 0;
                    InvalidateRect(w, NULL, TRUE);
                } else {
                    gx=x1; gy=y1; gw=x2-x1; gh=y2-y1;
                    gridSet = TRUE;
                    EndCap();  SaveIni();
                }
            }
        } else if (capMode >= 2 && capMode <= 6) {
            int ti = capMode - 2;
            tgtX[ti]=pt.x;  tgtY[ti]=pt.y;  tgtSet[ti]=TRUE;
            EndCap();  SaveIni();  InvalidateSect(3);
        } else if (capMode == 7) {
            rb1x = pt.x;  rb1y = pt.y;  rb1Set = TRUE;
            EndCap();  SaveIni();
        } else if (capMode == 8) {
            rb2x = pt.x;  rb2y = pt.y;  rb2Set = TRUE;
            EndCap();  SaveIni();
        } else if (capMode == 9) {
            rs12x = pt.x;  rs12y = pt.y;  rs12Set = TRUE;
            EndCap();  SaveIni();
        } else if (capMode == 10) {
            rs34x = pt.x;  rs34y = pt.y;  rs34Set = TRUE;
            EndCap();  SaveIni();
        }
        return 0;
    }

    case WM_KEYDOWN:
        if (wp == 0x1B) { pendingStart = FALSE; pendingXerox = FALSE; EndCap(); }
        return 0;

    case WM_SETCURSOR:
        SetCursor(LoadCursor(NULL, IDC_CROSS));
        return TRUE;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

/* ═══════════════════════════════════════════════════════════════
   DRAWING HELPERS
═══════════════════════════════════════════════════════════════ */

/* ═══════════════════════════════════════════════════════════════
   ACTIVE-TARGET LETTER  (red overlay from the LETTERSBMP resource)
   The header BMP shows the cyan "AWSIM" title; the active target's
   letter is lit by blitting just that letter's slice from the red
   overlay, keying out the black background so the header shows through.
   Sub-rectangles below are the red letters' pixel runs in the 580×72
   overlay (A, W, S, i, m → targets 0..4). Section-1-relative coords.
═══════════════════════════════════════════════════════════════ */
static const int LTR_X1[5] = {  49, 163, 279, 397, 501 };
static const int LTR_X2[5] = {  82, 199, 304, 413, 532 };
#define LTR_Y1  2
#define LTR_Y2 27

/* Blit the active target's red letter over the header (black = transparent). */
static void DrawLetter(HDC dc, int t) {
    if (!hLetters || t < 0 || t > 4) return;
    int x = LTR_X1[t], w = LTR_X2[t] - LTR_X1[t] + 1;
    int y = LTR_Y1,    h = LTR_Y2 - LTR_Y1 + 1;

    HDC src = CreateCompatibleDC(dc);
    HBITMAP osrc = (HBITMAP)SelectObject(src, hLetters);

    /* 1-bpp mask: source's black bg → white(1), red letter → black(0). */
    HDC mask = CreateCompatibleDC(dc);
    HBITMAP hmask = CreateBitmap(w, h, 1, 1, NULL);
    HBITMAP omask = (HBITMAP)SelectObject(mask, hmask);
    COLORREF obk = SetBkColor(src, RGB(0,0,0));
    BitBlt(mask, 0, 0, w, h, src, x, y, SRCCOPY);
    SetBkColor(src, obk);

    /* Punch the letter shape to black in the header, then OR the letter in.
       Mono→colour: 0-bits use text colour, 1-bits use bk colour. */
    COLORREF otx = SetTextColor(dc, RGB(0,0,0));
    COLORREF od  = SetBkColor(dc, RGB(255,255,255));
    BitBlt(dc, x, y, w, h, mask, 0, 0, SRCAND);    /* letter→black, bg→kept   */
    SetTextColor(dc, otx);  SetBkColor(dc, od);
    BitBlt(dc, x, y, w, h, src, x, y, SRCPAINT);   /* OR letter (black bg=0)  */

    SelectObject(mask, omask);  DeleteObject(hmask);  DeleteDC(mask);
    SelectObject(src, osrc);    DeleteDC(src);
}

/* ═══════════════════════════════════════════════════════════════
   RADIO-BUTTON INDICATOR SHAPES  (measured from overlay image)
   DSeg = relative coords: dy=row offset, dx1..dx2 = column range.
   Digit block: 5 px wide × 7 px tall.  Diamond tic: 5 × 5.
   Base X: CR = 24, FS = 333.   Base Y: row[r] = {41,64,87,110}.
═══════════════════════════════════════════════════════════════ */
typedef struct { short dy, dx1, dx2; } DSeg;

static const DSeg DS1[] = { {0,1,3},{1,2,3},{2,2,3},{3,2,3},{4,2,3},{5,2,3},{6,1,4} };
static const DSeg DS2[] = { {0,0,4},{1,4,4},{2,0,4},{3,0,0},{4,0,0},{5,0,4},{6,0,4} };
static const DSeg DS3[] = { {0,0,4},{1,4,4},{2,2,4},{3,4,4},{4,4,4},{5,0,4},{6,0,4} };
static const DSeg DS4[] = { {0,0,1},{1,0,1},{2,0,1},{2,3,4},{3,0,1},{3,3,4},{4,0,4},{5,3,4},{6,3,4} };
static const DSeg DS5[] = { {0,0,4},{1,0,0},{2,0,4},{3,4,4},{4,4,4},{5,0,4},{6,0,4} };
static const DSeg DS6[] = { {0,0,4},{1,0,0},{2,0,4},{3,0,0},{3,4,4},{4,0,0},{4,4,4},{5,0,4},{6,0,4} };
static const DSeg DS_TIC[] = { {0,2,2},{1,1,3},{2,0,4},{3,1,3},{4,2,2} };   /* dot-mode diamond */

static const DSeg *DSEGS[7] = { NULL, DS1, DS2, DS3, DS4, DS5, DS6 };
static const int   DSEG_N[7] = {    0,   7,   7,   7,   9,   7,   9 };

/* EEDDIITT n=  Draw one indicator (digit or tic) at section-2-relative (bx, by). */
static void DrawIndicator(HDC dc, int bx, int by, int val, BOOL dotMode) {
    if (val == 0) return;
    const DSeg *s;  int n;
    if (dotMode) { s = DS_TIC; n = 5; }
    else         { s = DSEGS[val]; n = DSEG_N[val]; }
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(255,255,255));
    HPEN op  = (HPEN)SelectObject(dc, pen);
    for (int i = 0; i < n; i++) {
        MoveToEx(dc, bx + s[i].dx1, by + s[i].dy, NULL);
        LineTo  (dc, bx + s[i].dx2 + 1, by + s[i].dy);
    }
    SelectObject(dc, op);  DeleteObject(pen);
}

/* Draw the 5×5 white diamond for an active Advanced button. */
static void DrawAdvancedDiamond(HDC dc, int cx, int cy) {
    static const DSeg D[] = { {-2,0,0},{-1,-1,1},{0,-2,2},{1,-1,1},{2,0,0} };
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(255,255,255));
    HPEN op  = (HPEN)SelectObject(dc, pen);
    for (int i = 0; i < 5; i++) {
        MoveToEx(dc, cx + D[i].dx1, cy + D[i].dy, NULL);
        LineTo  (dc, cx + D[i].dx2 + 1, cy + D[i].dy);
    }
    SelectObject(dc, op);  DeleteObject(pen);
}


/* Blit section BMP at (0,0) using current viewport origin.
   Falls back to a dark fill if the BMP failed to load.       */
static void BlitBmp(HDC dc, int n) {
    if (hBmp[n]) {
        HDC mdc = CreateCompatibleDC(dc);
        HBITMAP old = (HBITMAP)SelectObject(mdc, hBmp[n]);
        BITMAP bm;  GetObject(hBmp[n], sizeof bm, &bm);
        BitBlt(dc, 0, 0, bm.bmWidth, bm.bmHeight, mdc, 0, 0, SRCCOPY);
        SelectObject(mdc, old);  DeleteDC(mdc);
    } else {
        RECT rc = {0, 0, WIN_W, SECT_H[n]};
        HBRUSH br = CreateSolidBrush(RGB(22,22,22));
        FillRect(dc, &rc, br);  DeleteObject(br);
    }
}

/* Small check/X mark drawn directly inside a cell without any opaque box.
   This uses the exact pixel coordinates provided for the source examples, so the
   transparent mark matches the UI reference without guessing. */
static void DrawBitmapMark(HDC dc, int sx, int x, int y) {
    if (sx < 300) {
        static const int greenSegs[7][2][2] = {
            {{8, 9}, {-1, -1}},
            {{7, 9}, {-1, -1}},
            {{6, 8}, {-1, -1}},
            {{0, 1}, {5, 7}},
            {{0, 2}, {4, 6}},
            {{1, 5}, {-1, -1}},
            {{2, 4}, {-1, -1}}
        };
        for (int r = 0; r < 7; r++) {
            for (int s = 0; s < 2; s++) {
                int a = greenSegs[r][s][0];
                int b = greenSegs[r][s][1];
                if (a < 0 || b < 0) continue;
                RECT rc = { x + a, y + r, x + b + 1, y + r + 1 };
                HBRUSH br = CreateSolidBrush(RGB(46, 255, 92));
                FillRect(dc, &rc, br);
                DeleteObject(br);
            }
        }
        return;
    }

    static const int redSegs[7][2][2] = {
        {{0, 2}, {6, 8}},
        {{1, 3}, {5, 7}},
        {{2, 6}, {-1, -1}},
        {{3, 5}, {-1, -1}},
        {{2, 6}, {-1, -1}},
        {{1, 3}, {5, 7}},
        {{0, 2}, {6, 8}}
    };
    for (int r = 0; r < 7; r++) {
        for (int s = 0; s < 2; s++) {
            int a = redSegs[r][s][0];
            int b = redSegs[r][s][1];
            if (a < 0 || b < 0) continue;
            RECT rc = { x + a, y + r, x + b + 1, y + r + 1 };
            HBRUSH br = CreateSolidBrush(RGB(255, 76, 76));
            FillRect(dc, &rc, br);
            DeleteObject(br);
        }
    }
}

/* MPC digit rendered as 7-segment red bars ('figure 8').
 * Fits inside the 7×7 overlay rectangle at x=139..145, y=81..87.
 *
 *  Horizontal segments (5px wide × 1px tall):
 *    TOP  140,81 → 144,81
 *    MID  140,87 → 144,87
 *    BOT  140,93 → 144,93
 *  Vertical segments (1px wide × 5px tall):
 *    TL   139,82 → 139,86    TR   145,82 → 145,86
 *    BL   139,88 → 139,92    BR   145,88 → 145,92
 */
static void DrawMpcSeg(HDC dc, int val) {
    static const RECT SEG[7] = {
        {51,70,56,71},   /* 0 TOP was 110,82,115,83 */
        {51,76,56,77},   /* 1 MID was 110,88,115,89 */
        {51,82,56,83},   /* 2 BOT was 110,94,115,95 */
        {50,71,51,76},   /* 3 TL  was 109,83,110,88 */
        {56,71,57,76},   /* 4 TR  was 115,83,116,88 */
        {50,77,51,82},   /* 5 BL  was 109,89,110,94 */
        {56,77,57,82},   /* 6 BR  was 115,89,116,94 */
    };
    /* TOP MID BOT TL TR BL BR  for digits 1-5 */
    static const BYTE MASK[5][7] = {
        {0,0,0, 0,1,0,1},   /* 1 */
        {1,1,1, 0,1,1,0},   /* 2 */
        {1,1,1, 0,1,0,1},   /* 3 */
        {0,1,0, 1,1,0,1},   /* 4 */
        {1,1,1, 1,0,0,1},   /* 5 */
    };
    if (val < 1 || val > 5) return;
    const BYTE *m = MASK[val - 1];
    HBRUSH br = CreateSolidBrush(RGB(255,255,255));
    for (int s = 0; s < 7; s++)
        if (m[s]) FillRect(dc, &SEG[s], br);
    DeleteObject(br);
}

/* ── Section 1: active letter + button state overlays ─── */
static void DrawDyn1(HDC dc) {
    /* Overlay the CC470C segments of the active target's letter */
    DrawLetter(dc, activeTgt);

    /* "RIGHT CLICK / TO STOP" two-line overlay on Start while running */
    if (running) {
        RECT rc = {239, S1_BTN_Y1, 340, S1_BTN_Y2};
        HBRUSH br = CreateSolidBrush(RGB(160,20,20));
        FillRect(dc, &rc, br);  DeleteObject(br);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255,255,255));
        HFONT f = CreateFontA(20,0,0,0,FW_BOLD,0,0,0,
                              DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Segoe UI");
        HFONT of = (HFONT)SelectObject(dc, f);
        int mid = (S1_BTN_Y1 + S1_BTN_Y2) / 2;
        RECT r1 = {239, S1_BTN_Y1, 340, mid};
        RECT r2 = {239, mid,       340, S1_BTN_Y2};
        DrawTextA(dc, "RIGHT CLICK", -1, &r1, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        DrawTextA(dc, "TO STOP",     -1, &r2, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        SelectObject(dc, of);  DeleteObject(f);
    }

    /* Amber highlight on Setup during capture */
    if (wCap) {
        RECT rc = {352, S1_BTN_Y1, 453, S1_BTN_Y2};
        HBRUSH br = CreateSolidBrush(RGB(130,110,0));
        FillRect(dc, &rc, br);  DeleteObject(br);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255,235,0));
        HFONT f = CreateFontA(12,0,0,0,FW_BOLD,0,0,0,
                              DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Segoe UI");
        HFONT of = (HFONT)SelectObject(dc, f);
        const char *lbl = (capMode==1) ? "SET GRID" : "SET TARGET";
        DrawTextA(dc, lbl, -1, &rc, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        SelectObject(dc, of);  DeleteObject(f);
    }

}

/* ── Section 2: radio-button indicators + MPC digit ─────── */
static void DrawDyn2(HDC dc) {
    BOOL dot = (engVars[0] == '.');

    /* CR indicators: AwSim (tic1 x+15,y+1) and Decoy (tic2 x-104,y+2) */
    static const int CR_CX[2] = { S2_CR_CX + 19, S2_CR_CX + 19 };
    static const int CR_CY[2] = { S2_CR1_Y - 1, S2_CR2_Y + 3 };
    for (int r = 0; r < 2; r++)
        DrawIndicator(dc, CR_CX[r], CR_CY[r] - 3 + (dot ? 1 : 0),
                      engVars[r+1] - '0', dot);

    /* Four FS selectors and their Advanced indicators, in bitmap order. */
    for (int r = 0; r < 4; r++) {
        DrawIndicator(dc, S2_FS_CX, S2_CY[r] - 4 + (dot ? 1 : 0),
                      engVars[r+3] - '0', dot);
        if (engVars[r+7] != '0')
            DrawAdvancedDiamond(dc, S2_OD_CX + 3, S2_CY[r] - 1);
    }

    /* MPC digit: 7 red bars shown when CR1 or CR2 is active */
    if (engVars[1] != '0' || engVars[2] != '0')
        DrawMpcSeg(dc, engVars[11] - '0');

    /* MPC+ flash: CC470C diamond outline, half-diag = S2_MPC_R.
       One over the + button (right), one over the digit display (left). */
    if (flashMask & FLASH_MPC) {
        HPEN   pen = CreatePen(PS_SOLID, 1, RGB(255,17,17));   /* match Section 1 */
        HPEN   op  = (HPEN)SelectObject(dc, pen);
        HBRUSH ob  = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
        static const int CXS[2] = { S2_MPC_CX, S2_MPCDIG_CX };
        for (int d = 0; d < 2; d++) {
            POINT pts[5] = {
                { CXS[d],              S2_MPC_CY - S2_MPC_R },
                { CXS[d] + S2_MPC_R,  S2_MPC_CY             },
                { CXS[d],              S2_MPC_CY + S2_MPC_R },
                { CXS[d] - S2_MPC_R,  S2_MPC_CY             },
                { CXS[d],              S2_MPC_CY - S2_MPC_R },
            };
            Polyline(dc, pts, 5);
        }
        SelectObject(dc, op);  SelectObject(dc, ob);
        DeleteObject(pen);
    }

    /* number/dot toggle flash: CC470C rectangle outline */
    if (flashMask & FLASH_DN) {
        HPEN   pen = CreatePen(PS_SOLID, 1, RGB(255,17,17));   /* match Section 1 */
        HPEN   op  = (HPEN)SelectObject(dc, pen);
        HBRUSH ob  = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, S2_DN_X1-1, S2_DN_Y1-1, S2_DN_X2+2, S2_DN_Y2+2);
        SelectObject(dc, op);  SelectObject(dc, ob);
        DeleteObject(pen);
    }

    /* Reset flash: CC470C rectangle outline */
    if (flashMask & FLASH_RST) {
        HPEN   pen = CreatePen(PS_SOLID, 1, RGB(255,17,17));   /* match Section 1 */
        HPEN   op  = (HPEN)SelectObject(dc, pen);
        HBRUSH ob  = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, S2_RST_X1-1, S2_RST_Y1-1, S2_RST_X2+2, S2_RST_Y2+2);
        SelectObject(dc, op);  SelectObject(dc, ob);
        DeleteObject(pen);
    }
}

/* ── Section 3: checkmarks + action-button flash ────────── */
static void DrawDyn3(HDC dc) {
    for (int r = 0; r < GROWS; r++)
        for (int c = 0; c < GCOLS; c++) {
            int idx = r*GCOLS + c;
            int cx = (S3_CX1[c] + S3_CX2[c]) / 2;
            int cy = (S3_RY1[r] + S3_RY2[r]) / 2 + 4;
            if (boxState[idx] == BOX_ACTIVE)
                DrawBitmapMark(dc, 176, cx-5, cy-3);
            else if (boxState[idx] == BOX_DISABLED)
                DrawBitmapMark(dc, 412, cx-4, cy-3);
        }

    /* Remaining action buttons: 1px DCE314 rectangle outline each */
    static const DWORD BITS[2] = {
        FLASH_S3B3, FLASH_S3B4
    };
    for (int b = 0; b < 2; b++) {
        if (flashMask & BITS[b]) {
            HPEN   pen = CreatePen(PS_SOLID, 1, RGB(220,227,20));
            HPEN   op  = (HPEN)SelectObject(dc, pen);
            HBRUSH ob  = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
            Rectangle(dc, S3_BTN_X1[b], S3_BTN_Y1[b],
                          S3_BTN_X2[b]+1, S3_BTN_Y2[b]+1);
            SelectObject(dc, op);  SelectObject(dc, ob);
            DeleteObject(pen);
        }
    }
}

/* ── Section 4: CC470C border around active target ──────── */
static void DrawDyn4(HDC dc) {
    HBRUSH br = CreateSolidBrush(RGB(255,17,17));   /* match Section 1 letters */
    int x1 = S4_X1[activeTgt];
    int x2 = S4_X2[activeTgt] + 2;   /* right edge: 1px wider */
    RECT r;
    /* Top and bottom horizontal strips span full width — corners always connect */
    r = (RECT){x1-1,  1,   x2,    3  };  FillRect(dc, &r, br);   /* top    2px */
    r = (RECT){x1,    117, x2,    119};  FillRect(dc, &r, br);   /* bottom 2px */
    r = (RECT){x1-1,  1,   x1+1,  119};  FillRect(dc, &r, br);  /* left   2px */
    r = (RECT){x2-2,  2,   x2,    119};  FillRect(dc, &r, br);  /* right  2px */
    DeleteObject(br);
}

/* ═══════════════════════════════════════════════════════════════
   CLICK HANDLERS  (ry = y relative to section top)
═══════════════════════════════════════════════════════════════ */
static void ClickSect1(int mx, int ry) {
    if (ry < S1_BTN_Y1 || ry > S1_BTN_Y2) return;
    if      (mx>=13  && mx<=63)  { SetView((viewState+5)%6); }
    else if (mx>=65  && mx<=114) { SetView((viewState+1)%6); }
    else if (mx>=126 && mx<=227) {
        if (wHelp) CloseHelp();
        else OpenHelp();
    }
    else if (mx>=239 && mx<=340) { running ? StopAuto() : StartAuto(); }
    else if (mx>=352 && mx<=453) {
        if (!running) {
            /* Clicking Setup clears grid + row/swap button calibration,
               forcing fresh capture (targets are left untouched). */
            gx = gy = gw = gh = 0;       gridSet = FALSE;
            rb1x = rb1y = 0;             rb1Set  = FALSE;
            rb2x = rb2y = 0;             rb2Set  = FALSE;
            rs12x = rs12y = 0;           rs12Set = FALSE;
            rs34x = rs34y = 0;           rs34Set = FALSE;
            SaveIni();
            StartCap(1);
        }
    }
    else if (mx>=465 && mx<=566) {
        LogApp("Window exit button clicked.");
        app.exitPending = TRUE;
        if (StopAuto()) { app.exitPending = FALSE; DestroyWindow(wMain); }
    }
}

static void ClickSect2(int mx, int ry) {
    /* number/dot toggle */
    if (mx>=S2_DN_X1 && mx<=S2_DN_X2 && ry>=S2_DN_Y1 && ry<=S2_DN_Y2) {
        FlashBtn(FLASH_DN, 1);  DotNumToggle();  InvalidateSect(1);  SaveIni();  return;
    }
    /* Reset */
    if (mx>=S2_RST_X1 && mx<=S2_RST_X2 && ry>=S2_RST_Y1 && ry<=S2_RST_Y2) {
        FlashBtn(FLASH_RST, 1);  EngineReset();  InvalidateSect(1);  SaveIni();  return;
    }
    /* MPC+ (diamond bounding box) */
    if (mx>=S2_MPC_X1 && mx<=S2_MPC_X2 && ry>=S2_MPC_Y1 && ry<=S2_MPC_Y2) {
        FlashBtn(FLASH_MPC, 1);  MpcCycle();  InvalidateSect(1);  SaveIni();  return;
    }
    /* CR1 AwSim */
    if (mx>=S2_CR_XA && mx<=S2_CR_XB &&
        ry >= S2_CR1_Y - S2_ROW_YTOL && ry <= S2_CR1_Y + S2_ROW_YTOL) {
        EngineClick(1);  InvalidateSect(1);  SaveIni();  return;
    }
    /* CR2 Decoy */
    if (mx>=S2_CR_XA && mx<=S2_CR_XB &&
        ry >= S2_CR2_Y - S2_ROW_YTOL && ry <= S2_CR2_Y + S2_ROW_YTOL) {
        EngineClick(2);  InvalidateSect(1);  SaveIni();  return;
    }
    /* FS left selectors and right Advanced controls. */
    for (int r = 0; r < 4; r++) {
        if (ry < S2_CY[r] - S2_ROW_YTOL || ry > S2_CY[r] + S2_ROW_YTOL) continue;
        if (mx>=S2_OD_XA && mx<=S2_OD_XB) {
            EngineClick(r+7);  InvalidateSect(1);  SaveIni();  return;
        }
        if (mx>=S2_FS_XA && mx<=S2_FS_XB) {
            EngineClick(r+3);  InvalidateSect(1);  SaveIni();  return;
        }
    }
}

static void ClickSect3(int mx, int ry) {
    /* Column-toggle strip along the very top (checked before the grid so
       the top band toggles columns rather than the row-0 cells). */
    if (ry >= S3_TOG_Y1 && ry <= S3_TOG_Y2) {
        for (int c = 0; c < GCOLS; c++)
            if (mx >= S3_CX1[c] && mx <= S3_CX2[c]) { ColToggle(c); return; }
    }
    /* Cell grid */
    for (int r = 0; r < GROWS; r++) {
        if (ry < S3_RY1[r] || ry > S3_RY2[r]) continue;
        for (int c = 0; c < GCOLS; c++) {
            if (mx < S3_CX1[c] || mx > S3_CX2[c]) continue;
            int idx = r*GCOLS + c;
            boxState[idx] = (BYTE)((boxState[idx] + 1) % 3);
            InvalidateSect(2);  SaveIni();  return;
        }
    }
    /* Remaining bottom action buttons: Toggle Presets and XEROX */
    for (int b = 0; b < 2; b++) {
        if (mx < S3_BTN_X1[b] || mx > S3_BTN_X2[b]) continue;
        if (ry < S3_BTN_Y1[b] || ry > S3_BTN_Y2[b]) continue;
        FlashBtn(1u << (8+b), 2);
        switch (b) {
        case 0: TogglePresets();  break;
        case 1: StartXerox();     break;
        }
        return;
    }
}

static void ClickSect4(int mx, int ry) {
    if (ry < S4_Y1 || ry > S4_Y2) return;
    for (int t = 0; t < NTGTS; t++) {
        if (mx < S4_X1[t] || mx > S4_X2[t]) continue;
        DWORD now = GetTickCount();
        DWORD win = (DWORD)GetDoubleClickTime() * 3;
        if (now - tgtLastTick[t] < win) tgtClickCnt[t]++;
        else tgtClickCnt[t] = 1;
        tgtLastTick[t] = now;

        if (tgtClickCnt[t] == 1) {
            activeTgt = t;
            InvalidateSect(0);  InvalidateSect(3);  SaveIni();
        }
        if (tgtClickCnt[t] >= 3) {
            tgtClickCnt[t] = 0;
            if (running) return;
            activeTgt = t;
            StartCap(2 + t);
            InvalidateSect(0);  InvalidateSect(3);
        }
        return;
    }
}

/* ═══════════════════════════════════════════════════════════════
   MAIN WINDOW PROC
═══════════════════════════════════════════════════════════════ */
LRESULT CALLBACK ProcMain(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {

    case WM_ERASEBKGND: {
        RECT rc;  GetClientRect(w, &rc);
        FillRect((HDC)wp, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        return 1;
    }

    case WM_TIMER:
        if (wp == IDT_STOP_POLL) {
            if (StopAuto() && app.exitPending) {
                app.exitPending = FALSE;
                DestroyWindow(w);
            }
            return 0;
        }
        if (wp == IDT_FLASH) {
            DWORD prev = flashMask;  flashMask = 0;
            KillTimer(w, IDT_FLASH);
            if (prev & (FLASH_MPC|FLASH_DN|FLASH_RST))
                InvalidateSect(1);
            if (prev & (FLASH_S3B3|FLASH_S3B4))
                InvalidateSect(2);
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        for (int i = 0; i < 4; i++) {
            if (sectionY[i] < 0) continue;
            SetViewportOrgEx(dc, 0, sectionY[i], NULL);
            BlitBmp(dc, i);
            switch (i) {
            case 0: DrawDyn1(dc); break;
            case 1: DrawDyn2(dc); break;
            case 2: DrawDyn3(dc); break;
            case 3: DrawDyn4(dc); break;
            }
        }
        SetViewportOrgEx(dc, 0, 0, NULL);

        /* 608EA1 bottom separator: 2px at end of last non-target section.
           Section heights: 0=72, 1..3=138.  Line at section-local y=70..71
           (sec0) or y=136..137 (sec1/2), drawn in absolute coords.        */
        {
            int bot = -1;
            for (int i = 0; i < 4; i++) if (sectionY[i] >= 0) bot = i;
            if (bot >= 0 && bot <= 2) {
                int ly = sectionY[bot] + SECT_H[bot] - 2;
                RECT lr = {0, ly, 580, ly+2};
                HBRUSH lb = CreateSolidBrush(RGB(96,142,161));  /* 608EA1 */
                FillRect(dc, &lr, lb);
                DeleteObject(lb);
            }
        }
        EndPaint(w, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int mx = LOWORD(lp), my = HIWORD(lp);
        for (int i = 0; i < 4; i++) {
            int sy = sectionY[i];
            if (sy < 0 || my < sy || my >= sy + SECT_H[i]) continue;
            int ry = my - sy;
            switch (i) {
            case 0: ClickSect1(mx, ry); break;
            case 1: ClickSect2(mx, ry); break;
            case 2: ClickSect3(mx, ry); break;
            case 3: ClickSect4(mx, ry); break;
            }
            break;
        }
        return 0;
    }

    case WM_MOVING:
        /* Constrain drag: window bottom must not go below gy (boxgrid top). */
        if (gridSet) {
            RECT *pr = (RECT *)lp;
            int h = pr->bottom - pr->top;
            if (pr->bottom > gy) {
                pr->bottom = gy;
                pr->top    = gy - h;
                if (pr->top < 0) pr->top = 0;
            }
        }
        return TRUE;

    case WM_NCHITTEST: {
        /* Return HTCAPTION over header non-button area to allow drag. */
        POINT pt;
        pt.x = (short)LOWORD(lp);
        pt.y = (short)HIWORD(lp);
        ScreenToClient(w, &pt);
        int sy = sectionY[0];   /* header is always visible */
        if (pt.y >= sy && pt.y < sy + SH0) {
            int ry = pt.y - sy;
            if (ry < S1_BTN_Y1 || ry > S1_BTN_Y2) return HTCAPTION;
            BOOL onBtn =
                (pt.x>=13  && pt.x<=63)  ||
                (pt.x>=65  && pt.x<=114) ||
                (pt.x>=126 && pt.x<=227) ||
                (pt.x>=239 && pt.x<=340) ||
                (pt.x>=352 && pt.x<=453) ||
                (pt.x>=465 && pt.x<=566);
            if (!onBtn) return HTCAPTION;
        }
        return HTCLIENT;
    }

    case WM_STOPAUTO:
        if (StopAuto() && app.exitPending) {
            app.exitPending = FALSE;
            DestroyWindow(w);
        }
        return 0;
    case WM_EXITAPP:
        LogApp("Emergency exit message received.");
        app.exitPending = TRUE;
        EndCap();
        SaveIni();
        if (StopAuto()) { app.exitPending = FALSE; DestroyWindow(w); }
        return 0;
    case WM_STARTDEFER: StartAuto();                 return 0;
    case WM_XEROXDEFER: StartXerox();                return 0;
    case WM_TASKBARPROMPT: PromptTaskbarPin();       return 0;

    case WM_CLOSE:
        LogApp("Main window received WM_CLOSE.");
        app.exitPending = TRUE;
        EndCap();  SaveIni();
        if (StopAuto()) { app.exitPending = FALSE; DestroyWindow(w); }
        return 0;

    case WM_DESTROY:
        LogApp("Main window destroyed.");
        SaveIni();       /* always save on any exit path */
        FreeBmps();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

/* ═══════════════════════════════════════════════════════════════
   WINMAIN
═══════════════════════════════════════════════════════════════ */
static void EnableDpiAwareness(void) {
    HMODULE user32 = GetModuleHandleA("user32.dll");
    typedef BOOL (WINAPI *SetDpiContextFn)(HANDLE);
    FARPROC proc = user32 ? GetProcAddress(user32, "SetProcessDpiAwarenessContext") : NULL;
    SetDpiContextFn setContext = NULL;
    if (proc) memcpy(&setContext, &proc, sizeof setContext);
    if (setContext && setContext((HANDLE)(LONG_PTR)-4)) return;
    SetProcessDPIAware();
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE _prev, LPSTR _cmd, int show) {
    (void)_prev;  (void)_cmd;
    debugMode = HasDebugArgument();
    HANDLE instanceMutex = CreateMutexA(NULL, FALSE, "Local\\AwSim.SingleInstance");
    if (instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = NULL;
        for (int attempt = 0; attempt < 20 && !existing; attempt++) {
            existing = FindWindowA(CLS_MAIN, NULL);
            if (!existing) Sleep(50);
        }
        if (existing) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        CloseHandle(instanceMutex);
        return 0;
    }
    EnableDpiAwareness();
    hI = hi;
    srand((unsigned)time(NULL));

    /* Load the embedded "WORKING" bitmap (shown over the XEROX button while a
       capture runs). Baked into the EXE as a resource — no external file. */
    hWork = (HBITMAP)LoadImageA(hi, "WORKINGBMP", IMAGE_BITMAP,
                                0, 0, LR_CREATEDIBSECTION);
    hLetters = (HBITMAP)LoadImageA(hi, "LETTERSBMP", IMAGE_BITMAP,
                                   0, 0, LR_CREATEDIBSECTION);

    memset(boxState,     0, sizeof boxState);
    memset(tgtSet,       0, sizeof tgtSet);
    memset(tgtX,         0, sizeof tgtX);
    memset(tgtY,         0, sizeof tgtY);
    memset(tgtLastTick,  0, sizeof tgtLastTick);
    memset(tgtClickCnt,  0, sizeof tgtClickCnt);

    MakeIniPath();
    LoadIni();
    app.taskbarPromptPending = !IsTaskbarPinned();
    LoadBmps();

    /* Compute sectionY for the loaded view state */
    { int y = 0;
      for (int i = 0; i < 4; i++) {
          sectionY[i] = VIEW_VIS[viewState][i] ? y : -1;
          if (VIEW_VIS[viewState][i]) y += SECT_H[i];
      }
    }

    /* boxState[] is loaded directly from _Formation in LoadIni. */

    /* ── Register main window class ────────────────────────── */
    WNDCLASSEXA wcx;
    memset(&wcx, 0, sizeof wcx);
    wcx.cbSize        = sizeof wcx;
    wcx.hInstance     = hi;
    wcx.hIcon         = LoadIcon(hi, MAKEINTRESOURCE(IDI_APP));
    wcx.hIconSm       = (HICON)LoadImage(hi, MAKEINTRESOURCE(IDI_APP),
                             IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    wcx.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wcx.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wcx.lpfnWndProc   = ProcMain;
    wcx.lpszClassName = CLS_MAIN;
    RegisterClassExA(&wcx);

    /* ── Register capture overlay class ─────────────────────── */
    WNDCLASSA wc;
    memset(&wc, 0, sizeof wc);
    wc.hInstance      = hi;
    wc.hbrBackground  = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.hCursor        = LoadCursor(NULL, IDC_CROSS);
    wc.lpfnWndProc    = ProcCap;
    wc.lpszClassName  = CLS_CAP;
    RegisterClassA(&wc);

    /* ── Register Help window class ─────────────────────────── */
    WNDCLASSA wh;
    memset(&wh, 0, sizeof wh);
    wh.hInstance      = hi;
    wh.hbrBackground  = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wh.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wh.lpfnWndProc    = ProcHelp;
    wh.lpszClassName  = CLS_HELP;
    RegisterClassA(&wh);

    /* ── Compute initial window height ─────────────────────── */
    int winH = 0;
    for (int i = 0; i < 4; i++)
        if (VIEW_VIS[viewState][i]) winH += SECT_H[i];

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int defaultX = (sw * 49) / 100;
    int defaultY = (sh * 18) / 100;

    int wx = (winSX == INI_UNSET) ? defaultX : winSX;
    int wy = (winSY == INI_UNSET) ? defaultY : winSY;
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vw >= WIN_W) {
        if (wx < vx) wx = vx;
        if ((long long)wx + WIN_W > (long long)vx + vw) wx = vx + vw - WIN_W;
    }
    if (vh >= winH) {
        if (wy < vy) wy = vy;
        if ((long long)wy + winH > (long long)vy + vh) wy = vy + vh - winH;
    }

    wMain = CreateWindowExA(
        WS_EX_APPWINDOW,
        CLS_MAIN,
        APP_NAME "  v" APP_VER,
        WS_POPUP,
        wx, wy, WIN_W, winH,
        NULL, NULL, hi, NULL);

    ShowWindow(wMain, show);
    UpdateWindow(wMain);
    /* Always-on-top when idle */
    SetWindowPos(wMain, HWND_TOPMOST, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);

    /* WS_POPUP ignores CW_USEDEFAULT — force the saved position explicitly. */
    if (winSX != INI_UNSET && winSY != INI_UNSET)
        SetWindowPos(wMain, NULL, winSX, winSY, 0, 0,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE);

    /* Boxgrid calibration chain:
       - saved grid present  -> auto-calibrate from the 24 dark intersection
         diamonds; if they aren't found, AutoCalibrateGrid leaves the saved
         values untouched (revert to saved).
       - no saved grid        -> ask the user to mark the corners. */
    if (gridSet) {
        if (AutoCalibrateGrid()) SaveIni();
        else {
            LogApp("Grid auto-calibration did not lock; retained saved calibration.");
            SetAppStatus("Using saved grid calibration");
        }
    } else {
        /* No saved grid: try to auto-detect it from the diamond lattice;
           only fall back to corner marking if that can't lock confidently. */
        if (DetectGridColdStart()) SaveIni();
        else {
            LogApp("Cold-start grid detection failed; requesting manual calibration.");
            SetAppStatus("Mark game grid");
            StartCap(1);
        }
    }
    if (!wCap) SetAppStatus("Ready");
    if (!wCap && app.taskbarPromptPending) {
        app.taskbarPromptPending = FALSE;
        PostMessage(wMain, WM_TASKBARPROMPT, 0, 0);
    }

    MSG mmsg;
    while (GetMessageA(&mmsg, NULL, 0, 0)) {
        TranslateMessage(&mmsg);
        DispatchMessageA(&mmsg);
    }
    return (int)mmsg.wParam;
}
