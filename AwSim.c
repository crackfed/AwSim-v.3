/*
 * AwSim.c  –  v3.0
 * ═══════════════════════════════════════════════════════════════════
 * Compile:
 *   windres AwSim.rc -O coff -o AwSim.res
 *   gcc -O2 -o AwSim.exe AwSim.c AwSim.res -luser32 -lgdi32 -mwindows
 *
 * Config : <EXE dir>\_AwSim.ini
 * Assets (same directory as AwSim.exe):
 *   AwSimUI-1.bmp  580×70   Header
 *   AwSimUI-2.bmp  580×135  Engines
 *   AwSimUI-3.bmp  580×135  Boxgrid
 *   AwSimUI-4.bmp  580×135  Targets
 *
 * ── View states (< > buttons cycle circularly) ───────────────────
 *   0  Hdr + Eng + Box + Tgt   477 px
 *   1  Hdr + Eng + Box         342 px
 *   2  Hdr + Eng               207 px
 *   3  Hdr + Box               207 px
 *   4  Hdr + Tgt               207 px
 *   5  Hdr only                 72 px
 *
 * ── Engine-vars string (13 chars + NUL) ──────────────────────────
 *   [0]     = '.' or '#'  dot-mode vs number-mode
 *   [1..4]  = CR buttons 1-4  value: 0=off, 1-5=run-order
 *   [5..8]  = FS buttons 5-8  value: 0=off, 1-4=run-order
 *   [9..11] = EP circles 9-11 value: 0=off or mirrors FS 6-8
 *   [12]    = MPC '1'..'5'
 *   Default: ".100000000003"
 *
 * ── Stop conditions (low-level hook, during automation) ──────────
 *   Right-click anywhere  →  stop automation
 *   Cursor x ≤ 3          →  exit program
 *
 * ── Target buttons ───────────────────────────────────────────────
 *   ×1 click  →  select as active (highlighted)
 *   ×3 clicks →  recapture screen coordinate via crosshair overlay
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define APP_NAME   "AwSim"
#define APP_VER    "3.0"
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

/* ── Window / class names ─────────────────────────────────────── */
#define CLS_MAIN  "AwSimMain"
#define CLS_CAP   "AwSimCap"

/* ── Custom messages ─────────────────────────────────────────── */
#define WM_STOPAUTO    (WM_APP + 1)
#define WM_EXITAPP     (WM_APP + 2)
#define WM_STARTDEFER  (WM_APP + 3)

/* ── Button flash IDs (bit positions in flashMask) ──────────── */
#define FLASH_VIEW   (1u<<0)   /* Sec1 <View>  (both < and >)     */
#define FLASH_HELP   (1u<<1)   /* Sec1 Help                        */
#define FLASH_START  (1u<<2)   /* Sec1 Start / Stop                */
#define FLASH_SETUP  (1u<<3)   /* Sec1 Setup                       */
#define FLASH_EXIT   (1u<<4)   /* Sec1 Exit                        */
#define FLASH_MPC    (1u<<5)   /* Sec2 MPC + button                */
#define FLASH_DN     (1u<<6)   /* Sec2 #/. toggle                  */
#define FLASH_RST    (1u<<7)   /* Sec2 Reset button                */
#define FLASH_PRESL  (1u<<8)   /* Sec3 < Preset                    */
#define FLASH_PRESR  (1u<<9)   /* Sec3 Preset >                    */
#define FLASH_XEROX  (1u<<10)  /* Sec3 XEROX                       */
#define IDT_FLASH    1         /* WM_TIMER ID for flash clear      */

/* ═══════════════════════════════════════════════════════════════
   HOTSPOT COORDINATES  — measured from click-overlay images.
   All y-values are RELATIVE to the section's top edge (y=0).
═══════════════════════════════════════════════════════════════ */

/* Section 1 – Header ──────────────────────────────────────── */
/* 6 buttons; all share y = 27..64                            */
#define S1_BTN_Y1  27
#define S1_BTN_Y2  64
static const int S1_X1[6] = {  13,  65, 126, 239, 352, 465 };
static const int S1_X2[6] = {  63, 114, 227, 340, 453, 566 };
/* AwSim letter x-centres (underline drawn at y=20..22)       */
static const int S1_LX[5] = { 64, 176, 289, 402, 515 };

/* Section 2 – Engines ─────────────────────────────────────── */
static const int S2_ROW_Y1[4] = {  33,  56,  79, 102 };
static const int S2_ROW_Y2[4] = {  51,  74,  97, 120 };
/* CR button clickable x-ranges (rows 0-3 = buttons 1-4)      */
static const int S2_CR_X1[4]  = {  18,  18,  18,  18 };
static const int S2_CR_X2[4]  = { 133, 128, 168, 188 };
/* FS button clickable x-ranges (rows 0-3 = buttons 5-8)      */
static const int S2_FS_X1[4]  = { 326, 326, 326, 326 };
static const int S2_FS_X2[4]  = { 484, 475, 501, 466 };
/* EP circle x-range (rows 1-3 = circles 9-11; no EP row 0)   */
#define S2_EP_X1   512
#define S2_EP_X2   531
/* #/. toggle button                                           */
#define S2_DN_X1   270
#define S2_DN_X2   310
#define S2_DN_Y1    16
#define S2_DN_Y2    56
/* Reset button                                                */
#define S2_RST_X1  270
#define S2_RST_X2  310
#define S2_RST_Y1   75
#define S2_RST_Y2  115
/* MPC + button                                                */
#define S2_MPC_X1  173
#define S2_MPC_X2  189
#define S2_MPC_Y1   50
#define S2_MPC_Y2   66
/* Circle draw-centres                                         */
#define S2_CR_CX    22
#define S2_FS_CX   333
#define S2_EP_CX   521
static const int S2_CY[4] = { 42, 65, 88, 111 };
/* MPC digit display rectangle                                 */
#define S2_DIGX1   148
#define S2_DIGY1    30
#define S2_DIGX2   183
#define S2_DIGY2    70

/* Section 3 – Boxgrid ─────────────────────────────────────── */
#define S3_PRES_X1  167
#define S3_PRES_X2  227
#define S3_PRES_CX  197   /* left < right split point          */
#define S3_PRES_Y1    3
#define S3_PRES_Y2   22
static const int S3_CX1[9] = {  12,  74, 136, 198, 260, 322, 384, 446, 508 };
static const int S3_CX2[9] = {  71, 133, 195, 257, 319, 381, 443, 505, 567 };
static const int S3_RY1[4] = {  30,  53,  76,  99 };
static const int S3_RY2[4] = {  51,  74,  97, 120 };
#define S3_TOG_Y1  126
#define S3_TOG_Y2  132

/* Section 4 – Targets ─────────────────────────────────────── */
static const int S4_X1[5] = {   8, 122, 236, 350, 464 };
static const int S4_X2[5] = { 116, 230, 344, 458, 572 };
#define S4_Y1   5
#define S4_Y2  113

/* ═══════════════════════════════════════════════════════════════
   GLOBAL STATE
═══════════════════════════════════════════════════════════════ */
static HINSTANCE hI;
static HWND      wMain = NULL;
static HWND      wCap  = NULL;
static HBITMAP   hBmp[4];

/* View ──────────────────────────────────────────────────────── */
static int viewState = 0;
static int sectionY[4] = { 0, SH0, SH0+SH1, SH0+SH1+SH2 };

static const int  SECT_H[4]        = { SH0, SH1, SH2, SH3 };
static const BOOL VIEW_VIS[6][4]   = {
    {1,1,1,1}, {1,1,1,0}, {1,1,0,0},
    {1,0,1,0}, {1,0,0,1}, {1,0,0,0}
};

/* Engine ─────────────────────────────────────────────────────── */
static char engVars[14] = ".100000000003";

/* Boxgrid ───────────────────────────────────────────────────── */
static BOOL occ[NCELLS];
static BOOL customOcc[NCELLS];
static int  cycleState = 0;

/* Targets ───────────────────────────────────────────────────── */
static const char *tgtLabels[NTGTS] = { "CY","DF","Deff","CC","CY*" };
static int  tgtX[NTGTS], tgtY[NTGTS];
static BOOL tgtSet[NTGTS];
static int  activeTgt = 0;
static DWORD tgtLastTick[NTGTS];
static int   tgtClickCnt[NTGTS];

/* Game screen grid ──────────────────────────────────────────── */
static int  gx, gy, gw, gh;
static BOOL gridSet = FALSE;

/* Automation ────────────────────────────────────────────────── */
static BOOL   running  = FALSE;
static HANDLE hThread  = NULL;
static HANDLE hStop    = NULL;
static HHOOK  hHook    = NULL;

/* Flash ─────────────────────────────────────────────────────── */
static DWORD flashMask = 0;

/* Capture ───────────────────────────────────────────────────── */
static int   capMode     = 0;   /* 1=grid  2..6=target 0..4    */
static int   capStep     = 0;
static POINT capP1;
static BOOL  pendingStart = FALSE;

/* INI ───────────────────────────────────────────────────────── */
static char iniPath[MAX_PATH];
static int  winSX = INI_UNSET, winSY = INI_UNSET;

/* ═══════════════════════════════════════════════════════════════
   FORWARD DECLARATIONS
═══════════════════════════════════════════════════════════════ */
static void SetView(int v);
static void ApplyCycle(int state);
static void EngineClick(int btn);
static void InvalidateSect(int n);
static void SaveIni(void);
static void LoadIni(void);
static void StartAuto(void);
static void StopAuto(void);
static void StartCap(int mode);
static void EndCap(void);
static void FlashBtn(DWORD bits, int sect);
static void Center(int idx, int *ox, int *oy);
static void SetCursorXY(int x, int y);
static void DoClick(int x, int y);
static void DoDbl(int x, int y);
LRESULT CALLBACK ProcMain(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ProcCap (HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ProcHook(int, WPARAM, LPARAM);
DWORD   WINAPI   AutoRun (LPVOID);

/* ═══════════════════════════════════════════════════════════════
   INI HELPERS
═══════════════════════════════════════════════════════════════ */
static void MakeIniPath(void) {
    GetModuleFileNameA(NULL, iniPath, sizeof iniPath);
    char *sl = strrchr(iniPath, '\\');
    if (sl) *(sl+1) = '\0'; else iniPath[0] = '\0';
    strcat(iniPath, "_AwSim.ini");
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
    IniSetInt("_WindowX", wr.left);
    IniSetInt("_WindowY", wr.top);
    IniSetStr("_Version",    APP_VER);
    IniSetInt("_ViewState",  viewState);
    IniSetStr("_EngineVars", engVars);
    IniSetInt("_CycleState", cycleState);
    IniSetInt("_ActiveTarget", activeTgt);
    IniSetInt("_GridX", gx);  IniSetInt("_GridY", gy);
    IniSetInt("_GridW", gw);  IniSetInt("_GridH", gh);
    IniSetInt("_GridSet", gridSet ? 1 : 0);
    char key[32];
    for (int i = 0; i < NTGTS; i++) {
        sprintf(key, "_Tgt%cX",   'A'+i); IniSetInt(key, tgtX[i]);
        sprintf(key, "_Tgt%cY",   'A'+i); IniSetInt(key, tgtY[i]);
        sprintf(key, "_Tgt%cSet", 'A'+i); IniSetInt(key, tgtSet[i]?1:0);
    }
    char s[NCELLS+1];
    for (int i = 0; i < NCELLS; i++) s[i] = customOcc[i] ? '1' : '0';
    s[NCELLS] = '\0';
    IniSetStr("_CustomOcc", s);
}

static void LoadIni(void) {
    winSX      = IniGetInt("_WindowX",      INI_UNSET);
    winSY      = IniGetInt("_WindowY",      INI_UNSET);
    viewState  = IniGetInt("_ViewState",    0);
    if (viewState < 0 || viewState > 5) viewState = 0;
    IniGetStr("_EngineVars", ".100000000003", engVars, sizeof engVars);
    if (strlen(engVars) < 13 || (engVars[0]!='.' && engVars[0]!='#'))
        strcpy(engVars, ".100000000003");
    cycleState = IniGetInt("_CycleState",   0);
    if (cycleState < 0 || cycleState > 4) cycleState = 0;
    activeTgt  = IniGetInt("_ActiveTarget", 0);
    if (activeTgt < 0 || activeTgt >= NTGTS) activeTgt = 0;
    gx = IniGetInt("_GridX", 0);  gy = IniGetInt("_GridY", 0);
    gw = IniGetInt("_GridW", 0);  gh = IniGetInt("_GridH", 0);
    gridSet = IniGetInt("_GridSet", 0) ? TRUE : FALSE;
    char key[32];
    for (int i = 0; i < NTGTS; i++) {
        sprintf(key, "_Tgt%cX",   'A'+i); tgtX[i]  = IniGetInt(key, -1);
        sprintf(key, "_Tgt%cY",   'A'+i); tgtY[i]  = IniGetInt(key, -1);
        sprintf(key, "_Tgt%cSet", 'A'+i); tgtSet[i] = IniGetInt(key,0)?TRUE:FALSE;
    }
    char s[NCELLS+2] = "";
    IniGetStr("_CustomOcc", "", s, sizeof s);
    int slen = (int)strlen(s);
    for (int i = 0; i < NCELLS; i++)
        customOcc[i] = (i < slen && s[i]=='1') ? TRUE : FALSE;
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

/* ═══════════════════════════════════════════════════════════════
   ENGINE LOGIC  — exact translation of ParseMain-CkBoxes batch
   btn is 1-indexed (1..11).
   engVars[1..11] = button values '0'..'5'.
   engVars[12]    = MPC '1'..'5'.
═══════════════════════════════════════════════════════════════ */
static void EngineClick(int btn) {
    int v[12] = {0};
    for (int i = 1; i <= 11; i++) v[i] = engVars[i] - '0';

    /* Count active FS buttons (5-8) → hiVal */
    int hiVal = 0;
    for (int i = 5; i <= 8; i++) if (v[i] > 0) hiVal++;

    int mateLow  = btn - 3;   /* EP(9-11) → parent FS(6-8)          */
    int mateHigh = btn + 3;   /* FS(6-8)  → EP child (9-11)         */

    if (v[btn] == 0) {
        /* ── Currently OFF → turn ON ─────────────────────────── */
        if (btn >= 9) {
            /* EP button */
            if (v[mateLow] > 0) {
                v[btn] = v[mateLow];          /* sync to active parent  */
            } else {
                v[mateLow] = hiVal + 1;       /* activate parent FS too */
                v[btn]     = hiVal + 1;
                for (int i = 1; i <= 4; i++)
                    if (v[i] > 0) v[i]++;     /* bump any active CR     */
            }
        }
        if (btn >= 5 && btn <= 8) {
            /* FS button */
            v[btn] = hiVal + 1;
            for (int i = 1; i <= 4; i++)
                if (v[i] > 0) v[i]++;
        }
        if (btn <= 4) {
            /* CR button — mutually exclusive; always holds highest val */
            for (int i = 1; i <= 4; i++) v[i] = 0;
            v[btn] = hiVal + 1;
        }
    } else {
        /* ── Currently ON → turn OFF ─────────────────────────── */
        if (btn >= 5 && btn <= 8) {
            int comp = v[btn];
            v[btn] = 0;
            if (btn >= 6) v[mateHigh] = 0;   /* also clear EP mate     */
            for (int i = 1; i <= 11; i++)
                if (v[i] > comp) v[i]--;      /* compact sequence       */
        }
        if (btn >= 9) v[btn] = 0;            /* EP off, parent unchanged */
        if (btn <= 4) v[btn] = 0;
    }

    for (int i = 1; i <= 11; i++) engVars[i] = '0' + v[i];
}

static void MpcCycle(void) {
    int m = engVars[12] - '0';
    engVars[12] = '0' + (m % 5) + 1;   /* 1→2→3→4→5→1 */
}
static void DotNumToggle(void) {
    engVars[0] = (engVars[0] == '.') ? '#' : '.';
}
static void EngineReset(void) {
    strcpy(engVars, ".100000000003");
}

/* ═══════════════════════════════════════════════════════════════
   BOXGRID / PRESET CYCLE
═══════════════════════════════════════════════════════════════ */
static void ApplyCycle(int state) {
    cycleState = state;
    for (int i = 0; i < NCELLS; i++) {
        int c = i % GCOLS;
        switch (state) {
        case 0: occ[i] = customOcc[i]; break;
        case 1: occ[i] = TRUE;          break;   /* All          */
        case 2: occ[i] = (c < 5);       break;   /* Left 5 cols  */
        case 3: occ[i] = (c >= 4);      break;   /* Right 5 cols */
        case 4: occ[i] = FALSE;         break;   /* None         */
        }
    }
    InvalidateSect(2);
}

static void ColToggle(int col) {
    /* If any cell in this column is active → deactivate all.
       If all are inactive → activate all.                         */
    BOOL anyActive = FALSE;
    for (int r = 0; r < GROWS; r++)
        if (occ[r*GCOLS + col]) { anyActive = TRUE; break; }
    if (cycleState != 0) {
        memcpy(customOcc, occ, sizeof occ);
        cycleState = 0;
    }
    BOOL ns = !anyActive;
    for (int r = 0; r < GROWS; r++) {
        int idx = r*GCOLS + col;
        occ[idx] = customOcc[idx] = ns;
    }
    InvalidateSect(2);
    SaveIni();
}

/* ═══════════════════════════════════════════════════════════════
   MOUSE HELPERS
═══════════════════════════════════════════════════════════════ */
static void SetCursorXY(int x, int y) {
    SetCursorPos(x, y);                                Sleep(60);
}
static void DoClick(int x, int y) {
    SetCursorPos(x, y);                                Sleep(60);
    mouse_event(MOUSEEVENTF_LEFTDOWN, 0,0,0,0);        Sleep(50);
    mouse_event(MOUSEEVENTF_LEFTUP,   0,0,0,0);        Sleep(60);
}
static void DoDbl(int x, int y) {
    SetCursorPos(x, y);                                Sleep(60);
    mouse_event(MOUSEEVENTF_LEFTDOWN, 0,0,0,0);        Sleep(50);
    mouse_event(MOUSEEVENTF_LEFTUP,   0,0,0,0);        Sleep(90);
    mouse_event(MOUSEEVENTF_LEFTDOWN, 0,0,0,0);        Sleep(50);
    mouse_event(MOUSEEVENTF_LEFTUP,   0,0,0,0);        Sleep(60);
}
static void Center(int idx, int *ox, int *oy) {
    int c = idx % GCOLS, r = idx / GCOLS;
    *ox = gx + (int)((c + 0.5) * gw / (double)GCOLS);
    *oy = gy + (int)((r + 0.5) * gh / (double)GROWS);
}

/* ═══════════════════════════════════════════════════════════════
   AUTOMATION THREAD
═══════════════════════════════════════════════════════════════ */
/* ═══════════════════════════════════════════════════════════════
   KEY HELPER  — sends a single virtual-key press+release via SendInput
═══════════════════════════════════════════════════════════════ */
static void SendKey(WORD vk) {
    INPUT inp[2];
    memset(inp, 0, sizeof inp);
    inp[0].type       = INPUT_KEYBOARD;
    inp[0].ki.wVk     = vk;
    inp[1]            = inp[0];
    inp[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inp, sizeof(INPUT));
    Sleep(40);
}

/* ═══════════════════════════════════════════════════════════════
   AUTOMATION THREAD  — multi-engine dispatcher
   ─────────────────────────────────────────────────────────────
   Engine is determined by the active CR button (1-4).

   ENGINE 1 — AwSim
     All boxes drawn from the active set.

   ENGINE 2 — Decoy
     Box1 and Box3 drawn from the active set.
     Box2 drawn from the inactive set.
     Requires ≥1 inactive box.

   Box count:  MPC=1 → 2 boxes (Box1, Box2)
               MPC≥2 → 3 boxes (Box1, Box2, Box3)

   Move sequence, first-cycle reset, and Target Refresh
   are identical across all engines.
═══════════════════════════════════════════════════════════════ */
DWORD WINAPI AutoRun(LPVOID _u) {
    (void)_u;
    srand((unsigned)time(NULL) ^ GetCurrentThreadId());

    /* Which CR engine is active? */
    int engine = 1;
    for (int i = 1; i <= 4; i++)
        if (engVars[i] != '0') { engine = i; break; }

    int  mpc        = engVars[12] - '0';   /* 1..5 */
    int  need       = (mpc == 1) ? 2 : 3;
    BOOL firstCycle = TRUE;

    static const int MV_ORG[5] = { 0, 1, 2, 0, 1 };
    static const int MV_DST[5] = { 1, 2, 0, 1, 2 };

    for (;;) {
        /* ── Build active / inactive lists each cycle ─── */
        int aList[NCELLS], na = 0;
        int iList[NCELLS], ni = 0;
        for (int i = 0; i < NCELLS; i++) {
            if (occ[i]) aList[na++] = i;
            else        iList[ni++] = i;
        }

        /* ── Minimum-box check (engine-specific) ─── */
        BOOL ok;
        switch (engine) {
        case 2:   /* Decoy: (need-1) active + 1 inactive */
            ok = (na >= need - 1) && (ni >= 1);
            break;
        default:  /* AwSim: all boxes from active */
            ok = (na >= need);
            break;
        }
        if (!ok) {
            if (WaitForSingleObject(hStop, 500) == WAIT_OBJECT_0) break;
            continue;
        }

        /* ── Select boxes (engine-specific) ─── */
        int box[3] = {-1, -1, -1};
        int tmpA[NCELLS];
        memcpy(tmpA, aList, na * sizeof(int));

        switch (engine) {

        case 2: {   /* Decoy */
            /* Box1: random from active */
            int j = rand() % na;
            box[0] = tmpA[j];  tmpA[j] = tmpA[--na];  /* remove from pool */
            /* Box2: random from inactive */
            box[1] = iList[rand() % ni];
            /* Box3: random from remaining active (only when need==3) */
            if (need == 3)
                box[2] = tmpA[rand() % na];
            break;
        }

        default: {  /* AwSim — partial-shuffle active list */
            for (int i = 0; i < need; i++) {
                int j = i + rand() % (na - i);
                int t = tmpA[i]; tmpA[i] = tmpA[j]; tmpA[j] = t;
            }
            for (int i = 0; i < need; i++) box[i] = tmpA[i];
            break;
        }
        }

        /* ── Screen centres for selected boxes ─── */
        int bx[3], by[3];
        for (int i = 0; i < need; i++) Center(box[i], &bx[i], &by[i]);

        /* ── One-time cursor reset before first Cycle ─── */
        if (firstCycle) {
            firstCycle = FALSE;
            SendKey(0x41);   /* A */
            SendKey(0x58);   /* X */
            SendKey(0x58);   /* X */
            if (WaitForSingleObject(hStop, 250) == WAIT_OBJECT_0) break; /* pre-move delay */
        }

        /* ── Execute MPC Moves ─── */
        for (int m = 0; m < mpc; m++) {
            int oi = MV_ORG[m];
            int di = MV_DST[m];

            DoClick(bx[oi], by[oi]);
            if (WaitForSingleObject(hStop, 0) == WAIT_OBJECT_0) goto stop;

            SendKey(0x41);
            if (WaitForSingleObject(hStop, 0) == WAIT_OBJECT_0) goto stop;

            DoClick(bx[di], by[di]);
            if (WaitForSingleObject(hStop, 0) == WAIT_OBJECT_0) goto stop;

            SendKey(0x41);

            if (WaitForSingleObject(hStop, 3333) == WAIT_OBJECT_0) goto stop;
        }

        /* ── Cycle complete: click Target Refresh ─── */
        DoDbl(tgtX[activeTgt], tgtY[activeTgt]);
        if (WaitForSingleObject(hStop, 400) == WAIT_OBJECT_0) break;
    }

stop:
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
        if (ms->pt.x <= EDGE_X) {
            PostMessage(wMain, WM_EXITAPP, 0, 0);   return 1;
        }
    }
    return CallNextHookEx(hHook, code, wp, lp);
}

/* ═══════════════════════════════════════════════════════════════
   START / STOP AUTOMATION
═══════════════════════════════════════════════════════════════ */
static void StartAuto(void) {
    if (running) return;
    /* Capture game grid if not yet set */
    if (!gridSet)             { pendingStart = TRUE; StartCap(1);             return; }
    /* Capture active target coord if not set */
    if (!tgtSet[activeTgt])   { pendingStart = TRUE; StartCap(2 + activeTgt); return; }
    /* Determine active engine and check minimum box requirements */
    int engine = 1;
    for (int i = 1; i <= 4; i++)
        if (engVars[i] != '0') { engine = i; break; }
    int mpc  = engVars[12] - '0';
    int need = (mpc == 1) ? 2 : 3;
    int na = 0, ni = 0;
    for (int i = 0; i < NCELLS; i++) { if (occ[i]) na++; else ni++; }
    switch (engine) {
    case 2:  if (na < need-1 || ni < 1) return;  break;
    default: if (na < need)             return;  break;
    }
    /* All prerequisites met — launch thread */
    hStop   = CreateEventA(NULL, TRUE, FALSE, NULL);
    hHook   = SetWindowsHookExA(WH_MOUSE_LL, ProcHook, hI, 0);
    running = TRUE;
    InvalidateSect(0);    /* paint STOP over Start button */
    hThread = CreateThread(NULL, 0, AutoRun, NULL, 0, NULL);
}

static void StopAuto(void) {
    if (!running) return;
    SetEvent(hStop);
    WaitForSingleObject(hThread, 5000);
    CloseHandle(hThread); hThread = NULL;
    CloseHandle(hStop);   hStop   = NULL;
    if (hHook) { UnhookWindowsHookEx(hHook); hHook = NULL; }
    running = FALSE;
    InvalidateSect(0);    /* restore Start button */
    /* Pop the UI to the front so it's immediately accessible */
    SetWindowPos(wMain, HWND_TOP, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    SetForegroundWindow(wMain);
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
    if (pendingStart) {
        pendingStart = FALSE;
        PostMessage(wMain, WM_STARTDEFER, 0, 0);
    }
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
        } else {
            int ti = capMode - 2;
            tgtX[ti]=pt.x;  tgtY[ti]=pt.y;  tgtSet[ti]=TRUE;
            EndCap();  SaveIni();  InvalidateSect(3);
        }
        return 0;
    }

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { pendingStart = FALSE; EndCap(); }
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
   LETTER SEGMENT DATA  (measured from AwSimUI-1.bmp pixel scan)
   Each entry {y, x1, x2}: draw 1px horizontal line at y from x1 to x2.
   Color RGB(0,162,231) — the BMP's cyan stroke color.
   Coordinates are Section-1-relative (y=0 at section top).
═══════════════════════════════════════════════════════════════ */
typedef struct { short y, x1, x2; } Seg;

/* A — Target 0 (CY) : 17 segments */
static const Seg LTR_A[] = {
    {4,60,67},{6,59,68},{8,58,62},{8,65,69},
    {10,56,61},{10,66,71},{12,54,60},{12,67,73},
    {14,52,75},
    {16,50,59},{16,68,77},{18,48,55},{18,72,79},
    {20,44,52},{20,75,83},{22,39,50},{22,77,88}
};
/* w — Target 1 (DF) : 17 segments */
static const Seg LTR_W[] = {
    {12,158,161},{12,193,196},
    {14,160,165},{14,170,173},{14,181,184},{14,189,194},
    {16,162,169},{16,172,176},{16,178,182},{16,185,192},
    {18,164,172},{18,174,180},{18,182,190},
    {20,166,176},{20,178,188},{22,168,173},{22,181,186}
};
/* S — Target 2 (Deff) : 14 segments */
static const Seg LTR_S[] = {
    {4,277,311},
    {6,276,285},{6,306,309},
    {8,276,284},{8,305,307},
    {10,278,286},
    {12,282,294},
    {14,288,302},
    {16,298,306},
    {18,277,279},{18,296,308},
    {20,275,278},{20,295,308},
    {22,273,307}
};
/* i — Target 3 (CC) : 5 segments */
static const Seg LTR_I[] = {
    {12,399,405},{16,398,406},{18,399,405},{20,399,405},{22,397,407}
};
/* m — Target 4 (CY*) : 17 segments */
static const Seg LTR_M[] = {
    {12,499,504},{12,526,531},
    {14,501,507},{14,514,516},{14,523,529},
    {16,503,510},{16,512,518},{16,520,527},
    {18,501,507},{18,509,521},{18,523,529},
    {20,499,506},{20,512,518},{20,524,531},
    {22,496,505},{22,514,516},{22,525,534}
};

static const Seg *LETTERS[5] = { LTR_A, LTR_W, LTR_S, LTR_I, LTR_M };
static const int  LETTER_N[5] = {    17,    17,    14,     5,    17 };

/* Draw one letter's segments over the BMP in the BMP's cyan color. */
static void DrawLetter(HDC dc, int t) {
    const Seg *s = LETTERS[t];
    int        n = LETTER_N[t];
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(254,93,24));   /* FE5D18 */
    HPEN op  = (HPEN)SelectObject(dc, pen);
    for (int i = 0; i < n; i++) {
        MoveToEx(dc, s[i].x1, s[i].y, NULL);
        LineTo  (dc, s[i].x2 + 1, s[i].y);
    }
    SelectObject(dc, op);
    DeleteObject(pen);
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
static const DSeg DS_TIC[] = { {0,2,2},{1,1,3},{2,0,4},{3,1,3},{4,2,2} };   /* dot-mode diamond */

static const DSeg *DSEGS[6] = { NULL, DS1, DS2, DS3, DS4, DS5 };
static const int   DSEG_N[6] = {    0,   7,   7,   7,   9,   7 };

/* Draw one indicator (digit or tic) at section-2-relative (bx, by). */
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

/* Draw the 5×5 white diamond for an active EP button. */
static void DrawEPDiamond(HDC dc, int cx, int cy) {
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

/* Engine indicator circle.
   val=0   → empty dark outline.
   val>0   → filled cyan (CR/FS) or filled red (EP circles).
             Numbers shown inside CR/FS circles when in # mode. */
static void DrawCircle(HDC dc, int cx, int cy, int r,
                       int val, BOOL dotMode, BOOL isEP) {
    if (val == 0) {
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(70,70,70));
        HPEN op = (HPEN)SelectObject(dc, pen);
        HBRUSH ob = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
        Ellipse(dc, cx-r, cy-r, cx+r, cy+r);
        SelectObject(dc, op);  SelectObject(dc, ob);
        DeleteObject(pen);
    } else {
        /* EP circles → red;  CR/FS circles → cyan */
        COLORREF fillC = isEP ? RGB(200,40,40) : RGB(0,200,255);
        COLORREF rimC  = isEP ? RGB(150,20,20) : RGB(0,150,200);
        HBRUSH br = CreateSolidBrush(fillC);
        HPEN pen = CreatePen(PS_SOLID, 1, rimC);
        HBRUSH ob = (HBRUSH)SelectObject(dc, br);
        HPEN op = (HPEN)SelectObject(dc, pen);
        Ellipse(dc, cx-r, cy-r, cx+r, cy+r);
        SelectObject(dc, ob);  SelectObject(dc, op);
        DeleteObject(br);  DeleteObject(pen);
        if (!dotMode && !isEP) {
            char s[3];  sprintf(s, "%d", val);
            RECT rc = {cx-r, cy-r, cx+r, cy+r};
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(255,255,255));          /* white digits */
            HFONT f = CreateFontA(11,0,0,0,FW_BOLD,0,0,0,
                                  DEFAULT_CHARSET,0,0,0,0,"Arial");
            HFONT of = (HFONT)SelectObject(dc, f);
            DrawTextA(dc, s, 1, &rc, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            SelectObject(dc, of);  DeleteObject(f);
        }
    }
}

/* Small check-mark drawn inside a cell rectangle. */
static void DrawCheck(HDC dc, int x1, int y1, int x2, int y2) {
    int cx=(x1+x2)/2, cy=(y1+y2)/2;
    POINT pts[3] = {{cx-5,cy},{cx-1,cy+4},{cx+6,cy-4}};
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(100,230,100));
    HPEN op = (HPEN)SelectObject(dc, pen);
    Polyline(dc, pts, 3);
    SelectObject(dc, op);  DeleteObject(pen);
}

/* Bright amber outline drawn over a button during the flash period. */
static void FlashRect(HDC dc, int x1, int y1, int x2, int y2) {
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(220,227,20));   /* DCE314 */
    HPEN op = (HPEN)SelectObject(dc, pen);
    HBRUSH ob = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, x1, y1, x2, y2);
    SelectObject(dc, op);  SelectObject(dc, ob);
    DeleteObject(pen);
}

/* MPC digit rendered as 7-segment, geometry measured from the BMP '8'.
 * Digit is 8×16 px, origin at (155, 50) within Section 2.
 *
 *  ██████   TOP  y+0..1  x+0..5
 *  ██  ██   TL/TR y+2..6 x+0..1 / x+6..7
 *  ██████   MID  y+7..8  x+0..5
 *  ██  ██   BL/BR y+9..13 x+0..1 / x+6..7
 *  ██████   BOT  y+14..15 x+0..5
 */
static void DrawMpcSeg(HDC dc, int val) {
    /* Absolute section-2 coords, derived from pixel scan of BMP '8' */
    static const RECT SEG[7] = {
        {152,51,158,52},   /* 0 TOP */
        {152,58,158,59},   /* 1 MID */
        {152,65,158,66},   /* 2 BOT */
        {151,52,152,58},   /* 3 TL  */
        {158,52,159,58},   /* 4 TR  */
        {151,59,152,65},   /* 5 BL  */
        {158,59,159,65},   /* 6 BR  */
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
    /* Draw only the lit (red) segments; let the BMP show through elsewhere */
    const BYTE *m = MASK[val - 1];
    HBRUSH br = CreateSolidBrush(RGB(200,40,40));
    for (int s = 0; s < 7; s++)
        if (m[s]) FillRect(dc, &SEG[s], br);
    DeleteObject(br);
}

/* ── Section 1: active letter + button state overlays ─── */
static void DrawDyn1(HDC dc) {
    /* Overlay the FE5D18 segments of the active target's letter */
    DrawLetter(dc, activeTgt);

    /* STOP overlay on Start while running */
    if (running) {
        RECT rc = {239, S1_BTN_Y1, 340, S1_BTN_Y2};
        HBRUSH br = CreateSolidBrush(RGB(160,20,20));
        FillRect(dc, &rc, br);  DeleteObject(br);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255,255,255));
        HFONT f = CreateFontA(15,0,0,0,FW_BOLD,0,0,0,
                              DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Segoe UI");
        HFONT of = (HFONT)SelectObject(dc, f);
        DrawTextA(dc, "STOP", -1, &rc, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
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

    /* ── Momentary button flash: 2 px-wide vertical stripes, DCE314 ──
       Positions pixel-measured from overlay image (section-1-local).
       Stripes span y=35..56 (inner button frame).                    */
    static const struct { DWORD mask; int xl, xr; } STRIPE[5] = {
        { FLASH_VIEW,   23, 104 },
        { FLASH_HELP,  136, 217 },
        { FLASH_START, 249, 330 },
        { FLASH_SETUP, 362, 443 },
        { FLASH_EXIT,  475, 556 },
    };
    for (int b = 0; b < 5; b++) {
        if (flashMask & STRIPE[b].mask) {
            HBRUSH fl = CreateSolidBrush(RGB(220,227,20));   /* DCE314 */
            RECT rl = { STRIPE[b].xl,   35, STRIPE[b].xl+2, 57 };
            RECT rr = { STRIPE[b].xr,   35, STRIPE[b].xr+2, 57 };
            FillRect(dc, &rl, fl);
            FillRect(dc, &rr, fl);
            DeleteObject(fl);
        }
    }
}

/* ── Section 2: radio-button indicators + MPC digit ─────── */
static void DrawDyn2(HDC dc) {
    BOOL dot = (engVars[0] == '.');

    /* CR buttons 1-4: white indicator at (bx=24, by=row_base_y).
       Row base-y values measured from overlay: {41,64,87,110}.   */
    static const int CR_BY[4] = {41, 64, 87, 110};
    for (int r = 0; r < 4; r++)
        DrawIndicator(dc, 24,  CR_BY[r] + (dot ? 1 : 0), engVars[r+1]-'0', dot);

    /* FS buttons 5-8: white indicator at (bx=333, same base-y).  */
    for (int r = 0; r < 4; r++)
        DrawIndicator(dc, 333, CR_BY[r] + (dot ? 1 : 0), engVars[r+5]-'0', dot);

    /* EP circles 9-11: white diamond at center of each row (rows 1-3).
       EP cx=521, cy = S2_CY[r] (+2 to match row centres).        */
    for (int r = 1; r < 4; r++) {
        if (engVars[r+8] != '0')
            DrawEPDiamond(dc, 521, S2_CY[r]+2);
    }

    /* MPC digit: only shown when Engine 1 (AwSim) or Engine 2 (Decoy) is active */
    if (engVars[1] != '0' || engVars[2] != '0')
        DrawMpcSeg(dc, engVars[12] - '0');

    /* MPC '+' flash: filled dot r=8 in DCE314.
       Centre = 3px right + 10px down from top-left of former vertical bar. */
    if (flashMask & FLASH_MPC) {
        int dot_cx = 181, dot_cy = 58;   /* section-2-relative, matches click region */
        HBRUSH fl = CreateSolidBrush(RGB(220,227,20));
        HPEN   np = CreatePen(PS_NULL, 0, 0);
        HBRUSH ob = (HBRUSH)SelectObject(dc, fl);
        HPEN   op = (HPEN)  SelectObject(dc, np);
        Ellipse(dc, dot_cx-8, dot_cy-8, dot_cx+8, dot_cy+8);
        SelectObject(dc, ob);  SelectObject(dc, op);
        DeleteObject(fl);  DeleteObject(np);
    }

    /* #/. toggle and Reset flash: filled dot r=16 at button centre */
    if (flashMask & FLASH_DN) {
        int cx = (S2_DN_X1  + S2_DN_X2)  / 2 + 1,  cy = (S2_DN_Y1  + S2_DN_Y2)  / 2 + 1;
        HBRUSH fl = CreateSolidBrush(RGB(220,227,20));
        HPEN   np = CreatePen(PS_NULL, 0, 0);
        HBRUSH ob = (HBRUSH)SelectObject(dc, fl);
        HPEN   op = (HPEN)  SelectObject(dc, np);
        Ellipse(dc, cx-16, cy-16, cx+16, cy+16);
        SelectObject(dc, ob);  SelectObject(dc, op);
        DeleteObject(fl);  DeleteObject(np);
    }
    if (flashMask & FLASH_RST) {
        int cx = (S2_RST_X1 + S2_RST_X2) / 2 + 1,  cy = (S2_RST_Y1 + S2_RST_Y2) / 2 + 1;
        HBRUSH fl = CreateSolidBrush(RGB(220,227,20));
        HPEN   np = CreatePen(PS_NULL, 0, 0);
        HBRUSH ob = (HBRUSH)SelectObject(dc, fl);
        HPEN   op = (HPEN)  SelectObject(dc, np);
        Ellipse(dc, cx-16, cy-16, cx+16, cy+16);
        SelectObject(dc, ob);  SelectObject(dc, op);
        DeleteObject(fl);  DeleteObject(np);
    }
}

/* ── Section 3: checkmarks on active cells only ─────────── */
static void DrawDyn3(HDC dc) {
    for (int r = 0; r < GROWS; r++) {
        for (int c = 0; c < GCOLS; c++) {
            if (occ[r*GCOLS + c])
                DrawCheck(dc, S3_CX1[c], S3_RY1[r], S3_CX2[c], S3_RY2[r]);
        }
    }

    /* Presets flash: 1px DCE314 outline, 62×20 px, y shifted up 2px vs click area */
    if (flashMask & (FLASH_PRESL|FLASH_PRESR)) {
        HPEN   pen = CreatePen(PS_SOLID, 1, RGB(220,227,20));
        HPEN   op  = (HPEN)SelectObject(dc, pen);
        HBRUSH ob  = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, S3_PRES_X1-1, S3_PRES_Y1-3, S3_PRES_X1-1+62, S3_PRES_Y1-3+20);
        SelectObject(dc, op);  SelectObject(dc, ob);
        DeleteObject(pen);
    }
    if (flashMask & FLASH_XEROX) {
        HPEN   pen = CreatePen(PS_SOLID, 1, RGB(220,227,20));
        HPEN   op  = (HPEN)SelectObject(dc, pen);
        HBRUSH ob  = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, 353, 1, 416, 21);
        SelectObject(dc, op);  SelectObject(dc, ob);
        DeleteObject(pen);
    }
}

/* ── Section 4: FE5D18 border around active target ──────── */
static void DrawDyn4(HDC dc) {
    /* 2px outline, y adjusted to match BMP border: top up 4px, bottom down 3px */
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(254,93,24));   /* FE5D18 */
    HPEN op = (HPEN)SelectObject(dc, pen);
    HBRUSH ob = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, S4_X1[activeTgt], 1,
                  S4_X2[activeTgt]+1, 118);
    SelectObject(dc, op);  SelectObject(dc, ob);
    DeleteObject(pen);
}

/* ═══════════════════════════════════════════════════════════════
   CLICK HANDLERS  (ry = y relative to section top)
═══════════════════════════════════════════════════════════════ */
static void ClickSect1(int mx, int ry) {
    if (ry < S1_BTN_Y1 || ry > S1_BTN_Y2) return;
    if      (mx>=13  && mx<=63)  { FlashBtn(FLASH_VIEW,  0); SetView((viewState+5)%6); }
    else if (mx>=65  && mx<=114) { FlashBtn(FLASH_VIEW,  0); SetView((viewState+1)%6); }
    else if (mx>=126 && mx<=227) { FlashBtn(FLASH_HELP,  0); /* Help — placeholder */ }
    else if (mx>=239 && mx<=340) { FlashBtn(FLASH_START, 0); running ? StopAuto() : StartAuto(); }
    else if (mx>=352 && mx<=453) { FlashBtn(FLASH_SETUP, 0); if (!running) StartCap(1); }
    else if (mx>=465 && mx<=566) { FlashBtn(FLASH_EXIT,  0); StopAuto(); DestroyWindow(wMain); }
}

static void ClickSect2(int mx, int ry) {
    /* #/. toggle */
    if (mx>=S2_DN_X1 && mx<=S2_DN_X2 && ry>=S2_DN_Y1 && ry<=S2_DN_Y2) {
        FlashBtn(FLASH_DN, 1);  DotNumToggle();  InvalidateSect(1);  SaveIni();  return;
    }
    /* Reset */
    if (mx>=S2_RST_X1 && mx<=S2_RST_X2 && ry>=S2_RST_Y1 && ry<=S2_RST_Y2) {
        FlashBtn(FLASH_RST, 1);  EngineReset();  InvalidateSect(1);  SaveIni();  return;
    }
    /* MPC + */
    if (mx>=S2_MPC_X1 && mx<=S2_MPC_X2 && ry>=S2_MPC_Y1 && ry<=S2_MPC_Y2) {
        FlashBtn(FLASH_MPC, 1);  MpcCycle();  InvalidateSect(1);  SaveIni();  return;
    }
    /* Engine button rows 0-3 */
    for (int r = 0; r < 4; r++) {
        if (ry < S2_ROW_Y1[r] || ry > S2_ROW_Y2[r]) continue;
        if (mx>=S2_CR_X1[r] && mx<=S2_CR_X2[r]) {   /* CR buttons 1-4 */
            EngineClick(r+1);  InvalidateSect(1);  SaveIni();  return;
        }
        if (mx>=S2_FS_X1[r] && mx<=S2_FS_X2[r]) {   /* FS buttons 5-8 */
            EngineClick(r+5);  InvalidateSect(1);  SaveIni();  return;
        }
        if (r>0 && mx>=S2_EP_X1 && mx<=S2_EP_X2) {  /* EP circles 9-11 */
            EngineClick(r+8);  InvalidateSect(1);  SaveIni();  return;
        }
    }
}

static void ClickSect3(int mx, int ry) {
    /* < Preset > */
    if (mx>=S3_PRES_X1 && mx<=S3_PRES_X2 &&
        ry>=S3_PRES_Y1 && ry<=S3_PRES_Y2) {
        DWORD fb = (mx < S3_PRES_CX) ? FLASH_PRESL : FLASH_PRESR;
        int ns   = (mx < S3_PRES_CX) ? (cycleState+4)%5 : (cycleState+1)%5;
        FlashBtn(fb, 2);  ApplyCycle(ns);  SaveIni();  return;
    }
    /* XEROX (deferred) */
    if (mx>=353 && mx<=413 && ry>=3 && ry<=22) {
        FlashBtn(FLASH_XEROX, 2);  /* TODO */  return;
    }
    /* Cell grid */
    for (int r = 0; r < GROWS; r++) {
        if (ry < S3_RY1[r] || ry > S3_RY2[r]) continue;
        for (int c = 0; c < GCOLS; c++) {
            if (mx < S3_CX1[c] || mx > S3_CX2[c]) continue;
            int idx = r*GCOLS + c;
            if (cycleState != 0) {
                memcpy(customOcc, occ, sizeof occ);
                cycleState = 0;
            }
            occ[idx] = customOcc[idx] = !occ[idx];
            InvalidateSect(2);  SaveIni();  return;
        }
    }
    /* Column toggle row */
    if (ry >= S3_TOG_Y1 && ry <= S3_TOG_Y2) {
        for (int c = 0; c < GCOLS; c++) {
            if (mx >= S3_CX1[c] && mx <= S3_CX2[c]) { ColToggle(c); return; }
        }
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
        if (wp == IDT_FLASH) {
            DWORD prev = flashMask;  flashMask = 0;
            KillTimer(w, IDT_FLASH);
            if (prev & (FLASH_VIEW|FLASH_HELP|FLASH_START|FLASH_SETUP|FLASH_EXIT))
                InvalidateSect(0);
            if (prev & (FLASH_MPC|FLASH_DN|FLASH_RST))
                InvalidateSect(1);
            if (prev & (FLASH_PRESL|FLASH_PRESR|FLASH_XEROX))
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

    case WM_STOPAUTO:   StopAuto();                  return 0;
    case WM_EXITAPP:    StopAuto(); DestroyWindow(w); return 0;
    case WM_STARTDEFER: StartAuto();                 return 0;

    case WM_CLOSE:
        StopAuto();  EndCap();  SaveIni();
        DestroyWindow(w);
        return 0;

    case WM_DESTROY:
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
int WINAPI WinMain(HINSTANCE hi, HINSTANCE _prev, LPSTR _cmd, int show) {
    (void)_prev;  (void)_cmd;
    hI = hi;
    srand((unsigned)time(NULL));

    memset(occ,          0, sizeof occ);
    memset(customOcc,    0, sizeof customOcc);
    memset(tgtSet,       0, sizeof tgtSet);
    memset(tgtX,         0, sizeof tgtX);
    memset(tgtY,         0, sizeof tgtY);
    memset(tgtLastTick,  0, sizeof tgtLastTick);
    memset(tgtClickCnt,  0, sizeof tgtClickCnt);

    MakeIniPath();
    LoadIni();
    LoadBmps();

    /* Compute sectionY for the loaded view state */
    { int y = 0;
      for (int i = 0; i < 4; i++) {
          sectionY[i] = VIEW_VIS[viewState][i] ? y : -1;
          if (VIEW_VIS[viewState][i]) y += SECT_H[i];
      }
    }

    /* Populate occ[] from loaded custom/cycle state */
    ApplyCycle(cycleState);   /* wMain still NULL here; InvalidateSect is a no-op */

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

    /* ── Compute initial window height ─────────────────────── */
    int winH = 0;
    for (int i = 0; i < 4; i++)
        if (VIEW_VIS[viewState][i]) winH += SECT_H[i];

    int wx = (winSX == INI_UNSET) ? CW_USEDEFAULT : winSX;
    int wy = (winSY == INI_UNSET) ? CW_USEDEFAULT : winSY;

    wMain = CreateWindowExA(
        WS_EX_APPWINDOW,
        CLS_MAIN,
        APP_NAME "  v" APP_VER,
        WS_POPUP,
        wx, wy, WIN_W, winH,
        NULL, NULL, hi, NULL);

    ShowWindow(wMain, show);
    UpdateWindow(wMain);

    /* WS_POPUP ignores CW_USEDEFAULT — force the saved position explicitly. */
    if (winSX != INI_UNSET && winSY != INI_UNSET)
        SetWindowPos(wMain, NULL, winSX, winSY, 0, 0,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE);

    MSG mmsg;
    while (GetMessageA(&mmsg, NULL, 0, 0)) {
        TranslateMessage(&mmsg);
        DispatchMessageA(&mmsg);
    }
    return (int)mmsg.wParam;
}
