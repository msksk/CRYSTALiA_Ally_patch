/*
 * mekuiro d3d9 fix (final, minimal)
 *
 * Purpose:
 *   The game engine (Pal.dll) calls ShowWindow() during initialization, which
 *   can synchronously deliver WM_SIZE(SIZE_MAXIMIZED).  That handler posts
 *   message 0x806A to the render thread id stored in the engine object.  At
 *   that moment the render thread has not been created yet, so the id is 0.
 *   PostThreadMessageA(0, ...) fails, and the engine retries forever inside
 *   the WM_SIZE handler.  PalInit never returns, PalRun never starts the
 *   render thread, and the game stays black.
 *
 * Fix:
 *   This DLL is loaded as the local d3d9.dll.  On Direct3DCreate9 it finds
 *   USER32!PostThreadMessageA in Pal.dll's import table and hooks it.
 *   Posts to thread id 0 are reported as success (they can never be delivered
 *   anyway), which lets the WM_SIZE handler finish.  PalInit returns, PalRun
 *   creates the render thread, and rendering starts normally.
 *
 *   No game file is modified.  Delete this d3d9.dll to restore the original
 *   state (the game did not ship a d3d9.dll).
 *
 * Build (32-bit):
 *   zig cc --target=x86-windows-gnu -shared -O2 -o d3d9.dll d3d9proxy_final.c d3d9.def
 */

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <string.h>

typedef BOOL (WINAPI *PFN_PostThreadMessageA)(DWORD, UINT, WPARAM, LPARAM);
typedef IDirect3D9* (WINAPI *PFN_Direct3DCreate9)(UINT);
typedef HRESULT (WINAPI *PFN_Direct3DCreate9Ex)(UINT, IDirect3D9Ex**);

static HMODULE g_real_d3d9 = NULL;
static void* g_real_Direct3DCreate9 = NULL;
static PFN_PostThreadMessageA g_prev_PostThreadMessageA = NULL;
static volatile LONG g_pal_hook_state = 0; /* 0 = not tried, 1 = installed, 2 = failed */

static void log_error(const char* msg)
{
    WCHAR tmp[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, tmp);
    if (n == 0 || n >= MAX_PATH)
        return;
    if (n + 22 >= MAX_PATH)
        return;

    lstrcatW(tmp, L"mekuiro_d3d9fix.log");

    HANDLE h = CreateFileW(tmp, FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;

    DWORD written = 0;
    WriteFile(h, msg, (DWORD)strlen(msg), &written, NULL);
    WriteFile(h, "\r\n", 2, &written, NULL);
    CloseHandle(h);
}

static BOOL WINAPI hook_PostThreadMessageA(DWORD idThread, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    /*
     * Thread id 0 is never a valid thread id.  PostThreadMessageA would always
     * fail with ERROR_INVALID_THREAD_ID.  The engine treats the return value as
     * "the message was queued"; report success so its retry loop can finish.
     * Messages to real threads are passed through unchanged.
     */
    if (idThread == 0)
        return TRUE;

    if (g_prev_PostThreadMessageA)
        return g_prev_PostThreadMessageA(idThread, Msg, wParam, lParam);

    return FALSE;
}

static BOOL patch_iat_slot(void** slot, PFN_PostThreadMessageA real_ptm)
{
    DWORD oldProtect = 0;
    DWORD tmp = 0;
    void* cur = *slot;

    if (cur == (void*)hook_PostThreadMessageA)
        return TRUE;

    if (cur != (void*)real_ptm)
    {
        /*
         * Some other wrapper already hooked this slot.  Chain to it only if it
         * looks like executable code; otherwise leave it untouched.
         */
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(cur, &mbi, sizeof(mbi)))
        {
            log_error("Pal.dll: PostThreadMessageA IAT slot does not match user32; skipped");
            return FALSE;
        }
        if (!(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
        {
            log_error("Pal.dll: PostThreadMessageA IAT slot is not executable; skipped");
            return FALSE;
        }
    }

    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        log_error("Pal.dll: VirtualProtect failed; skipped");
        return FALSE;
    }

    g_prev_PostThreadMessageA = (PFN_PostThreadMessageA)cur;
    *slot = (void*)hook_PostThreadMessageA;

    VirtualProtect(slot, sizeof(void*), oldProtect, &tmp);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
    return TRUE;
}

static BOOL install_pal_hook(void)
{
    HMODULE pal;
    HMODULE user32;
    FARPROC real_ptm;
    BYTE* base;
    IMAGE_DOS_HEADER* dos;
    IMAGE_NT_HEADERS* nt;
    IMAGE_DATA_DIRECTORY* imp_dir;
    IMAGE_IMPORT_DESCRIPTOR* desc;

    if (g_pal_hook_state == 1)
        return TRUE;
    if (g_pal_hook_state == 2)
        return FALSE;

    pal = GetModuleHandleA("Pal.dll");
    if (!pal)
    {
        log_error("Pal.dll is not loaded; hook skipped");
        g_pal_hook_state = 2;
        return FALSE;
    }

    user32 = GetModuleHandleA("user32.dll");
    if (!user32)
    {
        log_error("user32.dll is not loaded; hook skipped");
        g_pal_hook_state = 2;
        return FALSE;
    }

    real_ptm = GetProcAddress(user32, "PostThreadMessageA");
    if (!real_ptm)
    {
        log_error("GetProcAddress(PostThreadMessageA) failed; hook skipped");
        g_pal_hook_state = 2;
        return FALSE;
    }

    base = (BYTE*)pal;
    dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    {
        log_error("Pal.dll has no valid DOS header; hook skipped");
        g_pal_hook_state = 2;
        return FALSE;
    }

    nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
    {
        log_error("Pal.dll has no valid NT header; hook skipped");
        g_pal_hook_state = 2;
        return FALSE;
    }

    imp_dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (imp_dir->VirtualAddress == 0 || imp_dir->Size == 0)
    {
        log_error("Pal.dll has no import directory; hook skipped");
        g_pal_hook_state = 2;
        return FALSE;
    }

    desc = (IMAGE_IMPORT_DESCRIPTOR*)(base + imp_dir->VirtualAddress);
    for (; desc->Name != 0; desc++)
    {
        const char* dll_name = (const char*)(base + desc->Name);
        IMAGE_THUNK_DATA* original;
        IMAGE_THUNK_DATA* first;

        if (_stricmp(dll_name, "USER32.dll") != 0)
            continue;

        if (desc->OriginalFirstThunk == 0 || desc->FirstThunk == 0)
            continue;

        original = (IMAGE_THUNK_DATA*)(base + desc->OriginalFirstThunk);
        first = (IMAGE_THUNK_DATA*)(base + desc->FirstThunk);
        if (!original || !first)
            continue;

        for (; original->u1.AddressOfData != 0; original++, first++)
        {
            if (IMAGE_SNAP_BY_ORDINAL(original->u1.Ordinal))
                continue;

            const char* name = (const char*)(base + original->u1.AddressOfData + 2);
            if (strcmp(name, "PostThreadMessageA") == 0)
            {
                if (!patch_iat_slot((void**)&first->u1.Function, (PFN_PostThreadMessageA)real_ptm))
                {
                    g_pal_hook_state = 2;
                    return FALSE;
                }
                g_pal_hook_state = 1;
                return TRUE;
            }
        }
    }

    /*
     * Fallback: find the slot by its resolved value.  This also covers DLLs
     * whose OriginalFirstThunk is not present.
     */
    desc = (IMAGE_IMPORT_DESCRIPTOR*)(base + imp_dir->VirtualAddress);
    for (; desc->Name != 0; desc++)
    {
        const char* dll_name = (const char*)(base + desc->Name);
        IMAGE_THUNK_DATA* first;

        if (_stricmp(dll_name, "USER32.dll") != 0)
            continue;

        if (desc->FirstThunk == 0)
            continue;

        first = (IMAGE_THUNK_DATA*)(base + desc->FirstThunk);
        if (!first)
            continue;

        for (; first->u1.Function != 0; first++)
        {
            if (first->u1.Function == (ULONG_PTR)real_ptm)
            {
                if (!patch_iat_slot((void**)&first->u1.Function, (PFN_PostThreadMessageA)real_ptm))
                {
                    g_pal_hook_state = 2;
                    return FALSE;
                }
                g_pal_hook_state = 1;
                return TRUE;
            }
        }
    }

    log_error("Pal.dll: PostThreadMessageA import was not found; hook skipped");
    g_pal_hook_state = 2;
    return FALSE;
}

static BOOL load_real_d3d9(void)
{
    WCHAR sys[MAX_PATH];
    WCHAR path[MAX_PATH];
    UINT n;

    if (g_real_d3d9 && g_real_Direct3DCreate9)
        return TRUE;

    n = GetSystemDirectoryW(sys, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
    {
        log_error("GetSystemDirectoryW failed; real d3d9 not loaded");
        return FALSE;
    }

    if (n + 10 >= MAX_PATH)
    {
        log_error("System directory path is too long; real d3d9 not loaded");
        return FALSE;
    }

    lstrcpyW(path, sys);
    lstrcatW(path, L"\\d3d9.dll");

    g_real_d3d9 = LoadLibraryW(path);
    if (!g_real_d3d9)
    {
        log_error("LoadLibraryW(system d3d9.dll) failed");
        return FALSE;
    }

    g_real_Direct3DCreate9 = (void*)GetProcAddress(g_real_d3d9, "Direct3DCreate9");
    if (!g_real_Direct3DCreate9)
    {
        log_error("GetProcAddress(Direct3DCreate9) failed");
        return FALSE;
    }

    return TRUE;
}

__declspec(dllexport) IDirect3D9* WINAPI Direct3DCreate9(UINT sdk_version)
{
    install_pal_hook();

    if (!load_real_d3d9())
        return NULL;

    return ((PFN_Direct3DCreate9)g_real_Direct3DCreate9)(sdk_version);
}

__declspec(dllexport) HRESULT WINAPI Direct3DCreate9Ex(UINT sdk_version, IDirect3D9Ex** out_d3d)
{
    PFN_Direct3DCreate9Ex fn;

    install_pal_hook();

    if (!load_real_d3d9())
        return E_FAIL;

    fn = (PFN_Direct3DCreate9Ex)GetProcAddress(g_real_d3d9, "Direct3DCreate9Ex");
    if (!fn)
        return E_NOTIMPL;

    return fn(sdk_version, out_d3d);
}

__declspec(dllexport) int WINAPI D3DPERF_BeginEvent(D3DCOLOR color, LPCWSTR name)
{
    (void)color;
    (void)name;
    return 0;
}

__declspec(dllexport) int WINAPI D3DPERF_EndEvent(void)
{
    return 0;
}

__declspec(dllexport) void WINAPI D3DPERF_SetMarker(D3DCOLOR color, LPCWSTR name)
{
    (void)color;
    (void)name;
}

__declspec(dllexport) void WINAPI D3DPERF_SetRegion(D3DCOLOR color, LPCWSTR name)
{
    (void)color;
    (void)name;
}

__declspec(dllexport) BOOL WINAPI D3DPERF_QueryRepeatFrame(void)
{
    return FALSE;
}

__declspec(dllexport) void WINAPI D3DPERF_SetOptions(DWORD options)
{
    (void)options;
}

__declspec(dllexport) DWORD WINAPI D3DPERF_GetStatus(void)
{
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);

    return TRUE;
}
