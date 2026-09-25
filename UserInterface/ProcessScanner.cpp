#include "StdAfx.h"
#include "ProcessScanner.h"
#include <algorithm>
#include <string>
#include <tlhelp32.h>

static std::vector<CRCPair> gs_kVct_crcPair;
static CRITICAL_SECTION gs_csData;
static HANDLE gs_evReqExit=NULL;
static HANDLE gs_evResExit=NULL;
static HANDLE gs_hThread=NULL;
static volatile LONG gs_lForbiddenProcessDetected = 0;

static bool ContainsInsensitive(const char* text, const char* needle)
{
	if (!text || !needle)
		return false;

	std::string strText = text;
	std::string strNeedle = needle;

	std::transform(strText.begin(), strText.end(), strText.begin(), ::tolower);
	std::transform(strNeedle.begin(), strNeedle.end(), strNeedle.begin(), ::tolower);

	return strText.find(strNeedle) != std::string::npos;
}

static BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam)
{
	char szTitle[256];
	szTitle[0] = '\0';

	GetWindowTextA(hWnd, szTitle, sizeof(szTitle));

	if (szTitle[0] == '\0')
		return TRUE;

	const char* forbiddenTitles[] =
	{
		"Cheat Engine",
		"Memory Viewer",
		"Pointer Scanner",
		"Structure Dissect",
		"Dissect Code",
		"Auto Assemble",

		"Extreme Injector",
		"Xenos",
		"GH Injector",
		"HLBOT",
		"HL BOT",
		"M2Bob",
		"Lalaker",
		"Kagura",
		"Waithack",
		"DamageHack",
		"BonusSwitcher",
		"Metin2Mod"
	};

	const int count = sizeof(forbiddenTitles) / sizeof(forbiddenTitles[0]);

	for (int i = 0; i < count; ++i)
	{
		if (ContainsInsensitive(szTitle, forbiddenTitles[i]))
		{
			InterlockedExchange(&gs_lForbiddenProcessDetected, 1);
			return FALSE;
		}
	}

	return TRUE;
}

static bool IsForbiddenProcessName(const char* szExeName)
{
	if (!szExeName || !szExeName[0])
		return false;

	const char* forbiddenProcesses[] =
	{
		// Cheat Engine
		"cheatengine.exe",
		"cheatengine-x86_64.exe",
		"cheatengine-i386.exe",
		"cheatengine-x86_64-sse4-avx2.exe",
		"Cheat Engine.exe",
		"CheatEngine75.exe",

		// Injectors
		"Extreme Injector.exe",
		"Extreme_Injector_v3.exe",
		"Xenos.exe",
		"Xenos64.exe",
		"GH Injector.exe",
		"KInject.exe",

		// Metin2 bots / hacks
		"M2Bob.exe",
		"M2Bob_Launcher.exe",
		"Lalaker1.exe",
		"Lalaker_Multihack.exe",
		"Kagura.exe",
		"Waithack.exe",
		"DamageHack.exe",
		"Switchbot.exe",
		"BonusSwitcher.exe",
		"Metin2Mod.exe",
		"Metin2Mod_Launch.exe",
		"HLBOT.exe",
		"HL BOT.exe",

		// Bypass tools
		"Bypass.exe",
		"AntiCheat_Bypass.exe",
		"NoShield.exe",
		"Anticheat_Killer.exe"
	};

	const int count = sizeof(forbiddenProcesses) / sizeof(forbiddenProcesses[0]);

	for (int i = 0; i < count; ++i)
	{
		if (_stricmp(szExeName, forbiddenProcesses[i]) == 0)
			return true;
	}

	if (_strnicmp(szExeName, "CheatEngine", 11) == 0)
		return true;

	if (_strnicmp(szExeName, "Extreme_Injector", 16) == 0)
		return true;

	return false;
}

static bool IsDebuggerDetected()
{
	if (IsDebuggerPresent())
		return true;

	BOOL bRemoteDebugger = FALSE;

	if (CheckRemoteDebuggerPresent(GetCurrentProcess(), &bRemoteDebugger))
	{
		if (bRemoteDebugger)
			return true;
	}

	return false;
}

void ScanProcessList(std::map<DWORD, DWORD>& rkMap_crcProc, std::vector<CRCPair>* pkVct_crcPair)
{
	SYSTEM_INFO si;
	memset(&si, 0, sizeof(si));
	GetSystemInfo(&si);

	PROCESSENTRY32 pro;
    pro.dwSize = sizeof(PROCESSENTRY32);

    LPPROCESSENTRY32 Entry;
    Entry = &pro;

    HANDLE process = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);

    BOOL bOK = Process32First(process, Entry);

    while(bOK)
    {
		const char* szExeName = Entry->szExeFile;

		if (IsForbiddenProcessName(szExeName))
		{
			InterlockedExchange(&gs_lForbiddenProcessDetected, 1);
		}
		HANDLE hProc = OpenProcess(PROCESS_VM_READ, FALSE, Entry->th32ProcessID);
		if (hProc)
		{
			HANDLE hModuleSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, Entry->th32ProcessID);
			if (hModuleSnap != INVALID_HANDLE_VALUE) 
			{
				MODULEENTRY32	me32;
				memset(&me32, 0, sizeof(me32));
				me32.dwSize = sizeof(MODULEENTRY32);

				BOOL bRet = Module32First(hModuleSnap, &me32);
				while (bRet) 
				{
					DWORD crcExtPath=GetCRC32((const char*)me32.szExePath, strlen(me32.szExePath));

					std::map<DWORD, DWORD>::iterator f=rkMap_crcProc.find(crcExtPath);
					if (rkMap_crcProc.end()==f)
					{
						DWORD crcProc=GetFileCRC32(me32.szExePath);
						rkMap_crcProc.insert(std::make_pair(crcExtPath, crcProc));
						pkVct_crcPair->push_back(std::make_pair(crcProc, (const char*)me32.szExePath));						
					}
					Sleep(1);
						
					ZeroMemory(&me32, sizeof(MODULEENTRY32));
					me32.dwSize = sizeof(MODULEENTRY32);		
					bRet = Module32Next(hModuleSnap, &me32);
				}

				CloseHandle(hModuleSnap);
			}

			CloseHandle(hProc);		
		}

	
        bOK = Process32Next(process, Entry);
    }
    CloseHandle(process);
}

void ProcessScanner_ReleaseQuitEvent()
{
	SetEvent(gs_evReqExit);
}

void ProcessScanner_Destroy()
{
	ProcessScanner_ReleaseQuitEvent();

	WaitForSingleObject(gs_evResExit, INFINITE);	
	CloseHandle(gs_evReqExit);
	CloseHandle(gs_evResExit);
	DeleteCriticalSection(&gs_csData);
}

bool ProcessScanner_PopProcessQueue(std::vector<CRCPair>* pkVct_crcPair)
{
	EnterCriticalSection(&gs_csData);
	*pkVct_crcPair=gs_kVct_crcPair;
	gs_kVct_crcPair.clear();
	LeaveCriticalSection(&gs_csData);

	if (pkVct_crcPair->empty())
		return false;

	return true;	
}

void ProcessScanner_Thread(void* pv)
{	
	DWORD dwDelay = 1000;

	std::map<DWORD, DWORD>	kMap_crcProc;
	std::vector<CRCPair>	kVct_crcPair;
	
	while (WAIT_OBJECT_0 != WaitForSingleObject(gs_evReqExit, dwDelay))
	{
		kVct_crcPair.clear();
		ScanProcessList(kMap_crcProc, &kVct_crcPair);
		EnumWindows(EnumWindowsProc, 0);
		if (IsDebuggerDetected())
		{
			InterlockedExchange(&gs_lForbiddenProcessDetected, 1);
		}
		EnterCriticalSection(&gs_csData);
		gs_kVct_crcPair.insert(gs_kVct_crcPair.end(), kVct_crcPair.begin(), kVct_crcPair.end());
		LeaveCriticalSection(&gs_csData);				

		dwDelay = 1000;
	}

	SetEvent(gs_evResExit);
}


bool ProcessScanner_Create()
{
	InitializeCriticalSection(&gs_csData);
	gs_evReqExit=CreateEvent(NULL, FALSE, FALSE, "ProcessScanner_ReqExit");
	gs_evResExit=CreateEvent(NULL, FALSE, FALSE, "ProcessScanner_ResExit");

	gs_hThread=(HANDLE)_beginthread(ProcessScanner_Thread, 64*1024, NULL);
	if (INVALID_HANDLE_VALUE==gs_hThread)
	{
		LogBox("CreateThread Error");
		return false;
	}

	if (!SetThreadPriority(gs_hThread, THREAD_PRIORITY_LOWEST))
	{
		LogBox("SetThreadPriority Error");
		return false;
	}

	return true;
}

bool ProcessScanner_IsForbiddenProcessDetected()
{
	return InterlockedCompareExchange(&gs_lForbiddenProcessDetected, 0, 0) != 0;
}