/*
 * AwSim.c  –  v3.0
 * ═══════════════════════════════════════════════════════════════════
 * Compile:
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
 *   [3..7]  = FS engines (ranked by selection order):
 *               [3]=Quick Opt [4]=Scout DZ [5]=Col.Ops
 *               [6]=Row Ops   [7]=Row Swap  0=off, 1-6=run-order rank
 *   [8..10] = Overdrive flags: [8]=Scout DZ [9]=Col.Ops [10]=Row Ops
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
#define FLASH_S3B1   (1u<<8)   /* Sec3 All-On  button            */
#define FLASH_S3B2   (1u<<9)   /* Sec3 Left-5  button            */
#define FLASH_S3B3   (1u<<10)  /* Sec3 Right-5 button            */
#define FLASH_S3B4   (1u<<11)  /* Sec3 All-Off button            */
#define FLASH_S3B5   (1u<<12)  /* Sec3 Save    button            */
#define FLASH_S3B6   (1u<<13)  /* Sec3 Load    button            */
#define IDT_FLASH    1         /* WM_TIMER ID for flash clear      */

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
#define S2_CR_CX     27   /* indicator draw x                  */
#define S2_CR1_Y     58   /* AwSim centre y                    */
#define S2_CR2_Y    117   /* Decoy  centre y                   */
#define S2_CR_XA      8   /* click x left  bound               */
#define S2_CR_XB    177   /* click x right bound               */
/* FS: 5 buttons at shared x, row centres                     */
#define S2_FS_CX    234   /* indicator draw x                  */
static const int S2_CY[5] = { 17, 42, 67, 92, 117 };
#define S2_FS_XA    185   /* click x left  bound               */
#define S2_FS_XB    439   /* click x right bound (OD starts 440) */
/* Overdrive diamonds: 3, for FS2/FS3/FS4 (rows 1,2,3)       */
#define S2_OD_CX    459   /* indicator draw x                  */
#define S2_OD_XA    440   /* click x left  bound               */
#define S2_OD_XB    478   /* click x right bound               */
#define S2_ROW_YTOL  12   /* ±px tolerance from row centre     */
/* number/dot toggle button (far-right top box)              */
#define S2_DN_X1    521
#define S2_DN_X2    559
#define S2_DN_Y1     14
#define S2_DN_Y2     52
/* Reset button (far-right bottom box)                         */
#define S2_RST_X1   521
#define S2_RST_X2   559
#define S2_RST_Y1    79
#define S2_RST_Y2   117
/* MPC+ button — diamond flash, half-diag = S2_MPC_R          */
#define S2_MPC_CX    42
#define S2_MPC_CY    87
#define S2_MPC_R     15
#define S2_MPC_X1   (S2_MPC_CX - S2_MPC_R)
#define S2_MPC_X2   (S2_MPC_CX + S2_MPC_R)
#define S2_MPC_Y1   (S2_MPC_CY - S2_MPC_R)
#define S2_MPC_Y2   (S2_MPC_CY + S2_MPC_R)
/* MPC digit display position (DrawDSeg 5x7 at this origin)   */
#define S2_DIGX     140
#define S2_DIGY      81

/* Section 3 – Boxgrid ─────────────────────────────────────── */
/* Cell grid — column x bounds unchanged, row y bounds (centres at y=14,37,60,83) */
static const int S3_CX1[9] = {  12,  74, 136, 198, 260, 322, 384, 446, 508 };
static const int S3_CX2[9] = {  71, 133, 195, 257, 319, 381, 443, 505, 567 };
static const int S3_RY1[4] = {   3,  26,  49,  72 };
static const int S3_RY2[4] = {  25,  48,  71,  94 };
/* Column-toggle click strip (no overlay drawn)                 */
#define S3_TOG_Y1   97
#define S3_TOG_Y2  112
/* 6 bottom action buttons — moved 2px up, 1px taller           */
static const int S3_BTN_X1[6] = { 23, 113, 203, 293, 383, 473 };
static const int S3_BTN_X2[6] = {106, 196, 286, 376, 466, 556 };
#define S3_BTN_Y1  113
#define S3_BTN_Y2  131

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
static char engVars[13] = ".10000000003";

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

/* Row button calibration (Engine 6 — Row Ops) ──────────────── */
static int  rb1x, rb1y;          /* MR1 centre — top-left  button */
static int  rb2x, rb2y;          /* SR4 centre — bot-right button */
static BOOL rb1Set = FALSE, rb2Set = FALSE;
/* Row Swap calibration (Engine 7 = FS5) ─────────────────────── */
static int  rs12x, rs12y;        /* Swap rows 1-2 button centre   */
static int  rs34x, rs34y;        /* Swap rows 3-4 button centre   */
static BOOL rs12Set = FALSE, rs34Set = FALSE;

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
    IniSetInt("_Rb1X", rb1x);  IniSetInt("_Rb1Y", rb1y);
    IniSetInt("_Rb1Set", rb1Set ? 1 : 0);
    IniSetInt("_Rb2X", rb2x);  IniSetInt("_Rb2Y", rb2y);
    IniSetInt("_Rb2Set", rb2Set ? 1 : 0);
    IniSetInt("_Rs12X", rs12x); IniSetInt("_Rs12Y", rs12y);
    IniSetInt("_Rs12Set", rs12Set ? 1 : 0);
    IniSetInt("_Rs34X", rs34x); IniSetInt("_Rs34Y", rs34y);
    IniSetInt("_Rs34Set", rs34Set ? 1 : 0);
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
    IniGetStr("_EngineVars", ".10000000003", engVars, sizeof engVars);
    if (strlen(engVars) < 12 || (engVars[0]!='.' && engVars[0]!='#'))
        strcpy(engVars, ".10000000003");
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
    rb1x = IniGetInt("_Rb1X", 0);  rb1y = IniGetInt("_Rb1Y", 0);
    rb1Set = IniGetInt("_Rb1Set", 0) ? TRUE : FALSE;
    rb2x = IniGetInt("_Rb2X", 0);  rb2y = IniGetInt("_Rb2Y", 0);
    rb2Set = IniGetInt("_Rb2Set", 0) ? TRUE : FALSE;
    rs12x = IniGetInt("_Rs12X", 0); rs12y = IniGetInt("_Rs12Y", 0);
    rs12Set = IniGetInt("_Rs12Set", 0) ? TRUE : FALSE;
    rs34x = IniGetInt("_Rs34X", 0); rs34y = IniGetInt("_Rs34Y", 0);
    rs34Set = IniGetInt("_Rs34Set", 0) ? TRUE : FALSE;
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
    int v[11] = {0};
    for (int i = 1; i <= 10; i++) v[i] = engVars[i] - '0';

    /* Count active FS buttons (3-7) for run-order slot assignment */
    int hiVal = 0;
    for (int i = 3; i <= 7; i++) if (v[i] > 0) hiVal++;

    if (v[btn] == 0) {
        /* ── OFF → ON ──────────────────────────────────────── */
        if (btn >= 8 && btn <= 10) {
            /* Overdrive: parent FS = btn - 4 */
            int par = btn - 4;
            if (v[par] > 0) {
                v[btn] = v[par];          /* sync to parent run-order */
            } else {
                v[par] = hiVal + 1;       /* activate parent FS too   */
                v[btn] = hiVal + 1;
                /* new FS slot added — bump CR so it stays highest    */
                for (int i = 1; i <= 2; i++)
                    if (v[i] > 0) v[i]++;
            }
        } else if (btn >= 3 && btn <= 7) {
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
        if (btn >= 3 && btn <= 7) {
            int comp = v[btn];
            v[btn] = 0;
            if (btn >= 4 && btn <= 6) v[btn + 4] = 0;  /* clear OD mate */
            /* Compact whole sequence including CR.
               CR is always > comp so it decrements by 1,
               keeping rank = remaining-FS-count + 1. Never hits 0. */
            for (int i = 1; i <= 10; i++)
                if (v[i] > comp) v[i]--;
        } else if (btn >= 8) {
            v[btn] = 0;                   /* OD off; parent unchanged */
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

/* True if column col (0-indexed) has at least one active cell. */
static BOOL ColIsActive(int col) {
    for (int r = 0; r < GROWS; r++)
        if (occ[r * GCOLS + col]) return TRUE;
    return FALSE;
}

/* ── Engine 3: Quick Opt (FS1) — 90-second time-limited cycle ── */
static BOOL RunQuickOpt(void) {
    DWORD t0   = GetTickCount();
    DWORD tLim = 90000;   /* 90 s */

    /* Snapshot active columns */
    int  activeCols[GCOLS]; int nAC = 0;
    for (int c = 0; c < GCOLS; c++)
        for (int r = 0; r < GROWS; r++)
            if (occ[r*GCOLS + c]) { activeCols[nAC++] = c; break; }
    if (nAC == 0) return TRUE;

    BOOL colUsed[GCOLS] = {0};
    BOOL rowUsed[GROWS] = {0};

    while (GetTickCount() - t0 < tLim) {

        /* ─ Column phase: pick unused active column at random ─ */
        int pool[GCOLS]; int np = 0;
        for (int i = 0; i < nAC; i++)
            if (!colUsed[activeCols[i]]) pool[np++] = activeCols[i];
        if (np == 0) {                       /* all used — reset */
            memset(colUsed, 0, sizeof colUsed);
            for (int i = 0; i < nAC; i++) pool[np++] = activeCols[i];
        }
        int col = pool[np > 1 ? rand() % np : 0];
        colUsed[col] = TRUE;

        int sdX  = gx + 55 + (int)((double)col * gw / GCOLS);
        int btnY = gy - 15;
        for (int i = 0; i < 3; i++) {
            DoClick(sdX, btnY);
            if (WaitForSingleObject(hStop, 3333) == WAIT_OBJECT_0) return FALSE;
        }
        DoDbl(tgtX[activeTgt], tgtY[activeTgt]);
        if (WaitForSingleObject(hStop, 500) == WAIT_OBJECT_0) return FALSE;

        if (GetTickCount() - t0 >= tLim) break;

        /* ─ Row phase: pick unused row at random ─ */
        int rpool[GROWS]; int rp = 0;
        for (int r = 0; r < GROWS; r++)
            if (!rowUsed[r]) rpool[rp++] = r;
        if (rp == 0) {                       /* all used — reset */
            memset(rowUsed, 0, sizeof rowUsed);
            for (int r = 0; r < GROWS; r++) rpool[rp++] = r;
        }
        int row = rpool[rp > 1 ? rand() % rp : 0];
        rowUsed[row] = TRUE;

        int slX, slY; RowBtnXY(1, row, &slX, &slY);
        DoClick(slX, slY);
        if (WaitForSingleObject(hStop, 3333) == WAIT_OBJECT_0) return FALSE;

        int srX, srY; RowBtnXY(2, row, &srX, &srY);
        DoClick(srX, srY); DoClick(srX, srY);   /* twice */
        if (WaitForSingleObject(hStop, 3333) == WAIT_OBJECT_0) return FALSE;

        DoDbl(tgtX[activeTgt], tgtY[activeTgt]);
        if (WaitForSingleObject(hStop, 500) == WAIT_OBJECT_0) return FALSE;
    }
    return TRUE;
}

/* ── Engine 4: Scout DZ (FS2) — fixed keystroke sequence ─────── */
static BOOL RunScoutDZ(void) {
    /* Click Target first to ensure game window has focus */
    DoClick(tgtX[activeTgt], tgtY[activeTgt]);
    if (WaitForSingleObject(hStop, 500) == WAIT_OBJECT_0) return FALSE;

#define E4K(vk) do { SendKey(vk); \
    if (WaitForSingleObject(hStop,3333)==WAIT_OBJECT_0) return FALSE; } while(0)

    /* Block 1: 4×8, 7, 4×8, then Target */
    for (int i = 0; i < 8; i++) E4K(VK_NUMPAD4);
    E4K(VK_NUMPAD7);
    for (int i = 0; i < 8; i++) E4K(VK_NUMPAD4);
    DoDbl(tgtX[activeTgt], tgtY[activeTgt]);
    if (WaitForSingleObject(hStop, 500) == WAIT_OBJECT_0) return FALSE;

    /* Block 2: 8×3, 4, 2×2, 6, 6, 8×2, then Target */
    for (int i = 0; i < 3; i++) E4K(VK_NUMPAD8);
    E4K(VK_NUMPAD4);
    for (int i = 0; i < 2; i++) E4K(VK_NUMPAD2);
    E4K(VK_NUMPAD6);
    E4K(VK_NUMPAD6);
    for (int i = 0; i < 2; i++) E4K(VK_NUMPAD8);
    DoDbl(tgtX[activeTgt], tgtY[activeTgt]);
    if (WaitForSingleObject(hStop, 500) == WAIT_OBJECT_0) return FALSE;

#undef E4K
    return TRUE;
}

/* ═══════════════════════════════════════════════════════════════
   ENGINE 5  — Col.Ops (FS3)
   For each active column: optionally Mirror, ShiftDown ×3, refresh.
═══════════════════════════════════════════════════════════════ */
static BOOL ColOpsPass(BOOL extended) {
    int btnY = gy - 15;
    for (int col = 0; col < GCOLS; col++) {
        if (!ColIsActive(col)) continue;
        int sdX = gx + 55 + (int)((double)col * gw / GCOLS);  /* ShiftDown  */
        int mrX = gx + 25 + (int)((double)col * gw / GCOLS);  /* Mirror     */

        if (extended) {
            DoClick(mrX, btnY);
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

/* Run twice; each round optionally followed by an Overdrive (Mirror) pass. */
static BOOL RunColOps(void) {
    BOOL ext = (engVars[9] != '0');    /* OD for FS3 Col.Ops = Overdrive */
    for (int round = 0; round < 2; round++) {
        if (!ColOpsPass(FALSE))       return FALSE;
        if (ext && !ColOpsPass(TRUE)) return FALSE;
    }
    return TRUE;
}

/* ═══════════════════════════════════════════════════════════════
   ENGINE 6  — Row Ops (FS4)
   Rows 4→1: DoubleClick(SR), click(SL), DoubleClick(SL), click(SL),
   then a Target refresh. Run once, repeated if Overdrive is on.
═══════════════════════════════════════════════════════════════ */
static BOOL RowOpsPass(void) {
    for (int row = 3; row >= 0; row--) {
        int srX, srY, slX, slY;
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

/* Engine 6: one pass, repeated once more if Overdrive is on. */
static BOOL RunRowOps(void) {
    BOOL ext = (engVars[10] != '0');   /* OD for FS4 Row Ops = Overdrive */
    /* Single click on Target to give the game window focus before the
       first action — without it the opening DoDbl lands as one click. */
    DoClick(tgtX[activeTgt], tgtY[activeTgt]);
    if (WaitForSingleObject(hStop, 500) == WAIT_OBJECT_0) return FALSE;
    if (!RowOpsPass()) return FALSE;
    if (ext && !RowOpsPass()) return FALSE;
    return TRUE;
}

/* ═══════════════════════════════════════════════════════════════
   ENGINE 7  — Row Swap (FS5)
   Calibrate two buttons (Swap 1-2, Swap 3-4); Swap 2-3 is their
   midpoint. Run a fixed 23-step swap sequence, then a refresh.
═══════════════════════════════════════════════════════════════ */
static BOOL RunRowSwap(void) {
    /* rs23 derived as midpoint of rs12 and rs34 */
    int bx[3] = { rs12x, (rs12x+rs34x)/2, rs34x };
    int by[3] = { rs12y, (rs12y+rs34y)/2, rs34y };

    /* 23-button sequence then Target (0=Swap1-2, 1=Swap2-3, 2=Swap3-4) */
    static const int SEQ[23] = {
        0,1,0,1, 2,1,2,1,
        0,1,0,1, 2,1,2,1,
        0,1,0,1, 2,1,2
    };
    for (int i = 0; i < 23; i++) {
        DoClick(bx[SEQ[i]], by[SEQ[i]]);
        if (WaitForSingleObject(hStop, 3333) == WAIT_OBJECT_0) return FALSE;
    }
    DoDbl(tgtX[activeTgt], tgtY[activeTgt]);
    if (WaitForSingleObject(hStop, 500) == WAIT_OBJECT_0) return FALSE;
    return TRUE;
}


/* ═══════════════════════════════════════════════════════════════
   AUTOMATION THREAD  — two-phase dispatcher
   ─────────────────────────────────────────────────────────────
   Phase 1: Active FS engines [3..7] run once each, in run-order rank.
   Phase 2: Active CR engine  [1..2] (AwSim/Decoy) runs indefinitely;
            if none is selected, the thread stops after Phase 1.
═══════════════════════════════════════════════════════════════ */
DWORD WINAPI AutoRun(LPVOID _u) {
    (void)_u;
    srand((unsigned)time(NULL) ^ GetCurrentThreadId());

    static const int MV_ORG[5] = { 0, 1, 2, 0, 1 };
    static const int MV_DST[5] = { 1, 2, 0, 1, 2 };

    /* ── Phase 1: FS engines in run-order value (1 = first) ─── */
    for (int ord = 1; ord <= 5; ord++) {
        for (int fsi = 3; fsi <= 7; fsi++) {
            if ((engVars[fsi] - '0') != ord) continue;
            switch (fsi) {
            case 3:  if (!RunQuickOpt()) goto stop; break;  /* Quick Opt  */
            case 4:  if (!RunScoutDZ())  goto stop; break;  /* Scout DZ   */
            case 5:  if (!RunColOps())   goto stop; break;  /* Col.Ops    */
            case 6:  if (!RunRowOps())   goto stop; break;  /* Row Ops    */
            case 7:  if (!RunRowSwap())  goto stop; break;  /* Row Swap   */
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
                if (occ[i]) aList[na++] = i;
                else        iList[ni++] = i;
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
    /* Grid capture needed only for CR engines and Engine 7 */
    {
        BOOL needsGrid = FALSE;
        for (int i = 1; i <= 2; i++) if (engVars[i] != '0') { needsGrid = TRUE; break; }
        if (engVars[3] != '0') needsGrid = TRUE;  /* Quick Opt  */
        if (engVars[5] != '0') needsGrid = TRUE;  /* Col.Ops   */
        if (engVars[6] != '0') needsGrid = TRUE;  /* Row Ops   */
        if (needsGrid && !gridSet) { pendingStart = TRUE; StartCap(1); return; }
    }
    /* Capture active target coord if not set */
    if (!tgtSet[activeTgt])   { pendingStart = TRUE; StartCap(2 + activeTgt); return; }
    /* Row button calibration for Quick Opt (E3) and Row Ops (E6) */
    if (engVars[3] != '0' || engVars[6] != '0') {
        if (!rb1Set) { pendingStart = TRUE; StartCap(7); return; }
        if (!rb2Set) { pendingStart = TRUE; StartCap(8); return; }
    }
    /* Row Swap calibration (E7 = FS5) */
    if (engVars[7] != '0') {
        if (!rs12Set) { pendingStart = TRUE; StartCap(9);  return; }
        if (!rs34Set) { pendingStart = TRUE; StartCap(10); return; }
    }
    /* Which engines are active? Verify minimums per type. */
    int crEngine = 0;
    for (int i = 1; i <= 2; i++) if (engVars[i] != '0') { crEngine = i; break; }
    BOOL hasFS = FALSE;
    for (int i = 3; i <= 7; i++) if (engVars[i] != '0') { hasFS = TRUE; break; }
    if (!crEngine && !hasFS) return;

    if (crEngine) {
        int mpc  = engVars[11] - '0';
        int need = (mpc == 1) ? 2 : 3;
        int na = 0, ni = 0;
        for (int i = 0; i < NCELLS; i++) { if (occ[i]) na++; else ni++; }
        switch (crEngine) {
        case 2:  if (na < need-1 || ni < 1) return;  break;
        default: if (na < need)             return;  break;
        }
    }
    /* Col.Ops needs at least one active column */
    if (engVars[5] != '0') {
        BOOL any = FALSE;
        for (int i = 0; i < NCELLS; i++) if (occ[i]) { any = TRUE; break; }
        if (!any) return;
    }
    /* All prerequisites met — launch thread */
    hStop   = CreateEventA(NULL, TRUE, FALSE, NULL);
    hHook   = SetWindowsHookExA(WH_MOUSE_LL, ProcHook, hI, 0);
    running = TRUE;
    InvalidateSect(0);    /* paint RIGHT CLICK / TO STOP over Start button */
    /* Drop always-on-top while running so game window can come to front */
    SetWindowPos(wMain, HWND_NOTOPMOST, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    ShowWindow(wMain, SW_MINIMIZE);   /* minimise immediately, no delay */
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
    /* Restore window, make always-on-top while idle */
    ShowWindow(wMain, SW_RESTORE);
    SetWindowPos(wMain, HWND_TOPMOST, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
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
   Drawn in CC470C over the BMP for the active target's letter.
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

/* Draw one letter's segments over the BMP in CC470C. */
static void DrawLetter(HDC dc, int t) {
    const Seg *s = LETTERS[t];
    int        n = LETTER_N[t];
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(204,71,12));   /* CC470C */
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
static const DSeg DS6[] = { {0,0,4},{1,0,0},{2,0,4},{3,0,0},{3,4,4},{4,0,0},{4,4,4},{5,0,4},{6,0,4} };
static const DSeg DS_TIC[] = { {0,2,2},{1,1,3},{2,0,4},{3,1,3},{4,2,2} };   /* dot-mode diamond */

static const DSeg *DSEGS[7] = { NULL, DS1, DS2, DS3, DS4, DS5, DS6 };
static const int   DSEG_N[7] = {    0,   7,   7,   7,   9,   7,   9 };

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

/* Draw the 5×5 white diamond for an active Overdrive button. */
static void DrawODDiamond(HDC dc, int cx, int cy) {
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

/* Small check-mark drawn inside a cell rectangle. */
static void DrawCheck(HDC dc, int x1, int y1, int x2, int y2) {
    int cx=(x1+x2)/2, cy=(y1+y2)/2;
    POINT pts[3] = {{cx-4,cy},{cx-1,cy+3},{cx+5,cy-3}};
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(100,230,100));
    HPEN op = (HPEN)SelectObject(dc, pen);
    Polyline(dc, pts, 3);
    SelectObject(dc, op);  DeleteObject(pen);
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
        {140,81,145,82},   /* 0 TOP */
        {140,87,145,88},   /* 1 MID */
        {140,93,145,94},   /* 2 BOT */
        {139,82,140,87},   /* 3 TL  */
        {145,82,146,87},   /* 4 TR  */
        {139,88,140,93},   /* 5 BL  */
        {145,88,146,93},   /* 6 BR  */
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
    HBRUSH br = CreateSolidBrush(RGB(200,40,40));
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

    /* CR indicators: AwSim (row cy=58), Decoy (row cy=117) */
    static const int CR_CY[2] = { S2_CR1_Y, S2_CR2_Y };
    for (int r = 0; r < 2; r++)
        DrawIndicator(dc, S2_CR_CX, CR_CY[r] - 3 + (dot ? 1 : 0),
                      engVars[r+1] - '0', dot);

    /* FS indicators: 5 buttons; Overdrive diamonds for rows 1-3 */
    for (int r = 0; r < 5; r++) {
        DrawIndicator(dc, S2_FS_CX, S2_CY[r] - 3 + (dot ? 1 : 0),
                      engVars[r+3] - '0', dot);
        /* Overdrive: FS2=r1=[8], FS3=r2=[9], FS4=r3=[10] */
        if (r >= 1 && r <= 3 && engVars[r+7] != '0')
            DrawODDiamond(dc, S2_OD_CX, S2_CY[r]);
    }

    /* MPC digit: 7 red bars shown when CR1 or CR2 is active */
    if (engVars[1] != '0' || engVars[2] != '0')
        DrawMpcSeg(dc, engVars[11] - '0');

    /* MPC+ flash: CC470C diamond outline, half-diag = S2_MPC_R
       Two diamonds: one over the + button (cx=42), one over the digit (cx=143) */
    if (flashMask & FLASH_MPC) {
        HPEN   pen = CreatePen(PS_SOLID, 1, RGB(204,71,12));   /* CC470C */
        HPEN   op  = (HPEN)SelectObject(dc, pen);
        HBRUSH ob  = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
        static const int CXS[2] = { S2_MPC_CX, S2_MPC_CX + 100 };
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
        HPEN   pen = CreatePen(PS_SOLID, 1, RGB(204,71,12));   /* CC470C */
        HPEN   op  = (HPEN)SelectObject(dc, pen);
        HBRUSH ob  = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, S2_DN_X1, S2_DN_Y1, S2_DN_X2+1, S2_DN_Y2+1);
        SelectObject(dc, op);  SelectObject(dc, ob);
        DeleteObject(pen);
    }

    /* Reset flash: CC470C rectangle outline */
    if (flashMask & FLASH_RST) {
        HPEN   pen = CreatePen(PS_SOLID, 1, RGB(204,71,12));   /* CC470C */
        HPEN   op  = (HPEN)SelectObject(dc, pen);
        HBRUSH ob  = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, S2_RST_X1, S2_RST_Y1, S2_RST_X2+1, S2_RST_Y2+1);
        SelectObject(dc, op);  SelectObject(dc, ob);
        DeleteObject(pen);
    }
}

/* ── Section 3: checkmarks + 6-button flash ─────────────── */
static void DrawDyn3(HDC dc) {
    for (int r = 0; r < GROWS; r++)
        for (int c = 0; c < GCOLS; c++)
            if (occ[r*GCOLS + c])
                DrawCheck(dc, S3_CX1[c], S3_RY1[r]+4, S3_CX2[c], S3_RY2[r]+4);

    /* 6 bottom button flash — 1px DCE314 rectangle outline each */
    static const DWORD BITS[6] = {
        FLASH_S3B1, FLASH_S3B2, FLASH_S3B3,
        FLASH_S3B4, FLASH_S3B5, FLASH_S3B6
    };
    for (int b = 0; b < 6; b++) {
        if (flashMask & BITS[b]) {
            HPEN   pen = CreatePen(PS_SOLID, 1, RGB(220,227,20));
            HPEN   op  = (HPEN)SelectObject(dc, pen);
            HBRUSH ob  = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
            Rectangle(dc, S3_BTN_X1[b], S3_BTN_Y1,
                          S3_BTN_X2[b]+1, S3_BTN_Y2+1);
            SelectObject(dc, op);  SelectObject(dc, ob);
            DeleteObject(pen);
        }
    }
}

/* ── Section 4: CC470C border around active target ──────── */
static void DrawDyn4(HDC dc) {
    HBRUSH br = CreateSolidBrush(RGB(204,71,12));   /* CC470C */
    int x1 = S4_X1[activeTgt];
    int x2 = S4_X2[activeTgt] + 2;   /* right edge: 1px wider */
    RECT r;
    /* Top and bottom horizontal strips span full width — corners always connect */
    r = (RECT){x1-1,  0,   x2,    2  };  FillRect(dc, &r, br);   /* top    2px */
    r = (RECT){x1,    116, x2,    118};  FillRect(dc, &r, br);   /* bottom 2px */
    r = (RECT){x1-1,  0,   x1+1,  118};  FillRect(dc, &r, br);  /* left   2px */
    r = (RECT){x2-2,  1,   x2,    118};  FillRect(dc, &r, br);  /* right  2px */
    DeleteObject(br);
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
    /* CR1 AwSim (cy=58) */
    if (mx>=S2_CR_XA && mx<=S2_CR_XB &&
        ry >= S2_CR1_Y - S2_ROW_YTOL && ry <= S2_CR1_Y + S2_ROW_YTOL) {
        EngineClick(1);  InvalidateSect(1);  SaveIni();  return;
    }
    /* CR2 Decoy (cy=117) — x-range distinguishes it from FS5 at same y */
    if (mx>=S2_CR_XA && mx<=S2_CR_XB &&
        ry >= S2_CR2_Y - S2_ROW_YTOL && ry <= S2_CR2_Y + S2_ROW_YTOL) {
        EngineClick(2);  InvalidateSect(1);  SaveIni();  return;
    }
    /* FS buttons 1-5 + Overdrive for rows 1-3 */
    for (int r = 0; r < 5; r++) {
        if (ry < S2_CY[r] - S2_ROW_YTOL || ry > S2_CY[r] + S2_ROW_YTOL) continue;
        /* Overdrive first — x=440..478, only for FS2/3/4 (r=1,2,3) */
        if (r >= 1 && r <= 3 && mx>=S2_OD_XA && mx<=S2_OD_XB) {
            EngineClick(r+7);  InvalidateSect(1);  SaveIni();  return;
        }
        /* FS button — x=185..439 */
        if (mx>=S2_FS_XA && mx<=S2_FS_XB) {
            EngineClick(r+3);  InvalidateSect(1);  SaveIni();  return;
        }
    }
}

static void ClickSect3(int mx, int ry) {
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
    /* Column toggle strip (no overlay — orange region) */
    if (ry >= S3_TOG_Y1 && ry <= S3_TOG_Y2) {
        for (int c = 0; c < GCOLS; c++) {
            if (mx >= S3_CX1[c] && mx <= S3_CX2[c]) { ColToggle(c); return; }
        }
    }
    /* 6 bottom action buttons */
    if (ry >= S3_BTN_Y1 && ry <= S3_BTN_Y2) {
        for (int b = 0; b < 6; b++) {
            if (mx < S3_BTN_X1[b] || mx > S3_BTN_X2[b]) continue;
            FlashBtn(1u << (8+b), 2);
            switch (b) {
            case 0: ApplyCycle(1); SaveIni(); break;   /* All On  */
            case 1: ApplyCycle(2); SaveIni(); break;   /* Left 5  */
            case 2: ApplyCycle(3); SaveIni(); break;   /* Right 5 */
            case 3: ApplyCycle(4); SaveIni(); break;   /* All Off */
            case 4:                                     /* Save    */
                memcpy(customOcc, occ, sizeof occ);
                cycleState = 0;
                SaveIni();
                break;
            case 5: {                                   /* Load    */
                char s[NCELLS+2] = "";
                IniGetStr("_CustomOcc", "", s, sizeof s);
                int slen = (int)strlen(s);
                for (int i = 0; i < NCELLS; i++)
                    customOcc[i] = (i < slen && s[i]=='1') ? TRUE : FALSE;
                ApplyCycle(0);
                break;
            }
            }
            return;
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
            if (prev & (FLASH_S3B1|FLASH_S3B2|FLASH_S3B3|FLASH_S3B4|FLASH_S3B5|FLASH_S3B6))
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
    /* Always-on-top when idle */
    SetWindowPos(wMain, HWND_TOPMOST, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);

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
