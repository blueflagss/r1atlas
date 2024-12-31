// Originally based on SpectreLauncher by Barnaby https://github.com/R1Spectre/SpectreLauncher
// Mitigations from Titanfall Black Market Edition mod by p0358 https://github.com/p0358/black_market_edition

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <TlHelp32.h>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <Shlwapi.h>
#include <iostream>
#include <unordered_map>

#pragma comment(lib, "Imm32.lib")

namespace fs = std::filesystem;

extern "C"
{
	__declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 0x00000001;
	__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
}

HMODULE hLauncherModule;
HMODULE hHookModule;
HMODULE hTier0Module;

wchar_t exePath[4096];
wchar_t buffer[8192];

DWORD GetProcessByName(const std::wstring& processName)
{
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

	PROCESSENTRY32 processSnapshotEntry = { 0 };
	processSnapshotEntry.dwSize = sizeof(PROCESSENTRY32);

	if (snapshot == INVALID_HANDLE_VALUE)
		return 0;

	if (!Process32First(snapshot, &processSnapshotEntry))
		return 0;

	while (Process32Next(snapshot, &processSnapshotEntry))
	{
		if (!wcscmp(processSnapshotEntry.szExeFile, processName.c_str()))
		{
			CloseHandle(snapshot);
			return processSnapshotEntry.th32ProcessID;
		}
	}

	CloseHandle(snapshot);
	return 0;
}

bool GetExePathWide(wchar_t* dest, DWORD destSize)
{
	if (!dest)
		return NULL;
	if (destSize < MAX_PATH)
		return NULL;

	DWORD length = GetModuleFileNameW(NULL, dest, destSize);
	return length && PathRemoveFileSpecW(dest);
}

FARPROC GetLauncherMain()
{
	static FARPROC Launcher_LauncherMain;
	if (!Launcher_LauncherMain)
		Launcher_LauncherMain = GetProcAddress(hLauncherModule, "LauncherMain");
	return Launcher_LauncherMain;
}

void LibraryLoadError(DWORD dwMessageId, const wchar_t* libName, const wchar_t* location)
{
	char text[8192];
	std::string message = std::system_category().message(dwMessageId);

	sprintf_s(
		text,
		"Failed to load the %ls at \"%ls\" (%lu):\n\n%hs\n\nMake sure you followed the R1Delta installation instructions carefully "
		"before reaching out for help.",
		libName,
		location,
		dwMessageId,
		message.c_str());

	if (dwMessageId == 126 && std::filesystem::exists(location))
	{
		sprintf_s(
			text,
			"%s\n\nThe file at the specified location DOES exist, so this error indicates that one of its *dependencies* failed to be "
			"found.\n\nTry the following steps:\n1. Install Visual C++ 2022 Redistributable: "
			"https://aka.ms/vs/17/release/vc_redist.x64.exe\n2. Repair game files",
			text);
	}
	else if (!fs::exists("Titanfall.exe") && (fs::exists("..\\Titanfall.exe") || fs::exists("..\\..\\Titanfall.exe")))
	{
		auto curDir = std::filesystem::current_path().filename().string();
		auto aboveDir = std::filesystem::current_path().parent_path().filename().string();
		sprintf_s(
			text,
			"%s\n\nWe detected that in your case you have extracted the files into a *subdirectory* of your Titanfall "
			"installation.\nPlease move all the files and folders from current folder (\"%s\") into the Titanfall installation directory "
			"just above (\"%s\").\n\nPlease try out the above steps by yourself before reaching out to the community for support.",
			text,
			curDir.c_str(),
			aboveDir.c_str());
	}
	else if (!fs::exists("Titanfall.exe"))
	{
		sprintf_s(
			text,
			"%s\n\nRemember: you need to unpack the contents of this archive into your Titanfall game installation directory, not just "
			"to any random folder.",
			text);
	}
	else if (fs::exists("Titanfall.exe"))
	{
		sprintf_s(
			text,
			"%s\n\nTitanfall.exe has been found in the current directory: is the game installation corrupted or did you not unpack all "
			"R1Delta files here?",
			text);
	}

	MessageBoxA(GetForegroundWindow(), text, "R1Delta Launcher Error", 0);
}

#if 0
void EnsureOriginStarted()
{
	if (GetProcessByName(L"Origin.exe") || GetProcessByName(L"EADesktop.exe"))
		return; // already started

	// unpacked exe will crash if origin isn't open on launch, so launch it
	// get origin path from registry, code here is reversed from OriginSDK.dll
	HKEY key;
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Origin", 0, KEY_READ, &key) != ERROR_SUCCESS)
	{
		MessageBoxA(0, "Error: failed reading Origin path!", "R1Delta Launcher Error", MB_OK);
		return;
	}

	char originPath[520];
	DWORD originPathLength = 520;
	if (RegQueryValueExA(key, "ClientPath", 0, 0, (LPBYTE)&originPath, &originPathLength) != ERROR_SUCCESS)
	{
		MessageBoxA(0, "Error: failed reading Origin path!", "R1Delta Launcher Error", MB_OK);
		return;
	}

	printf("[*] Starting Origin...\n");

	PROCESS_INFORMATION pi;
	memset(&pi, 0, sizeof(pi));
	STARTUPINFOA si;
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(STARTUPINFOA);
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_MINIMIZE;
	CreateProcessA(
		originPath,
		(char*)"",
		NULL,
		NULL,
		false,
		CREATE_DEFAULT_ERROR_MODE | CREATE_NEW_PROCESS_GROUP,
		NULL,
		NULL,
		(LPSTARTUPINFOA)&si,
		&pi);

	printf("[*] Waiting for Origin...\n");

	// wait for origin to be ready, this process is created when origin is ready enough to launch game without any errors
	while (!GetProcessByName(L"OriginClientService.exe") && !GetProcessByName(L"EADesktop.exe"))
		Sleep(200);

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
}
#endif

void PrependPath()
{
	wchar_t* pPath;
	size_t len;
	errno_t err = _wdupenv_s(&pPath, &len, L"PATH");
	if (!err)
	{
		swprintf_s(buffer, L"PATH=%s\\bin\\x64_retail\\;.;%s", exePath, pPath);
		auto result = _wputenv(buffer);
		if (result == -1)
		{
			MessageBoxW(
				GetForegroundWindow(),
				L"Warning: could not prepend the current directory to app's PATH environment variable. Something may break because of "
				L"that.",
				L"R1Delta Launcher Warning",
				0);
		}
		free(pPath);
	}
	else
	{
		MessageBoxW(
			GetForegroundWindow(),
			L"Warning: could not get current PATH environment variable in order to prepend the current directory to it. Something may "
			L"break because of that.",
			L"R1Delta Launcher Warning",
			0);
	}
}


bool IsAnyIMEInstalled()
{
	auto count = GetKeyboardLayoutList(0, nullptr);
	if (count == 0)
		return false;
	auto* list = reinterpret_cast<HKL*>(_alloca(count * sizeof(HKL)));
	GetKeyboardLayoutList(count, list);
	for (int i = 0; i < count; i++)
		if (ImmIsIME(list[i]))
			return true;
	return false;
}

bool DoesCpuSupportCetShadowStack()
{
	int cpuInfo[4] = { 0, 0, 0, 0 };
	__cpuidex(cpuInfo, 7, 0);
	return (cpuInfo[2] & (1 << 7)) != 0; // Check bit 7 in ECX (cpuInfo[2])
}

std::unordered_map<PROCESS_MITIGATION_POLICY, const char*> g_mitigationPolicyNames = {
	{ ProcessASLRPolicy, "ProcessASLRPolicy" },
	{ ProcessDynamicCodePolicy, "ProcessDynamicCodePolicy" },
	{ ProcessExtensionPointDisablePolicy, "ProcessExtensionPointDisablePolicy" },
	{ ProcessControlFlowGuardPolicy, "ProcessControlFlowGuardPolicy" },
	{ ProcessSignaturePolicy, "ProcessSignaturePolicy" },
	{ ProcessImageLoadPolicy, "ProcessImageLoadPolicy" },
	{ ProcessUserShadowStackPolicy, "ProcessUserShadowStackPolicy" },
};

void SetMitigationPolicies()
{
	auto kernel32 = GetModuleHandleW(L"kernel32.dll");
	if (!kernel32)
		return;
	auto SetProcessMitigationPolicy = (decltype(&::SetProcessMitigationPolicy))GetProcAddress(kernel32, "SetProcessMitigationPolicy");
	auto GetProcessMitigationPolicy = (decltype(&::GetProcessMitigationPolicy))GetProcAddress(kernel32, "GetProcessMitigationPolicy");
	if (!SetProcessMitigationPolicy || !GetProcessMitigationPolicy)
		return;

	auto SetProcessMitigationPolicyEnsureOK = [SetProcessMitigationPolicy](PROCESS_MITIGATION_POLICY MitigationPolicy, PVOID lpBuffer, SIZE_T dwLength) {
		bool result = SetProcessMitigationPolicy(MitigationPolicy, lpBuffer, dwLength);
		if (!result)
		{
			auto lastError = GetLastError();
			if (MitigationPolicy == ProcessUserShadowStackPolicy && !DoesCpuSupportCetShadowStack())
				return;
			MessageBoxA(0, ("Failed mitigation: "
				+ (g_mitigationPolicyNames.find(MitigationPolicy) != g_mitigationPolicyNames.end() ? g_mitigationPolicyNames[MitigationPolicy] : std::to_string(MitigationPolicy))
				+ ", error: " + std::to_string(lastError) + "\n\nThis is a non-fatal error.").c_str(),
				"SetProcessMitigationPolicy failed", 0);
		}
		};

	PROCESS_MITIGATION_ASLR_POLICY ap;
	GetProcessMitigationPolicy(::GetCurrentProcess(), ProcessASLRPolicy, &ap, sizeof(ap));
	ap.EnableBottomUpRandomization = true;
	ap.EnableForceRelocateImages = true;
	ap.EnableHighEntropy = true;
	ap.DisallowStrippedImages = true; // Images that have not been built with /DYNAMICBASE and do not have relocation information will fail to load if this flag and EnableForceRelocateImages are set.
	SetProcessMitigationPolicyEnsureOK(ProcessASLRPolicy, &ap, sizeof(ap));

	/*PROCESS_MITIGATION_DYNAMIC_CODE_POLICY dcp;
	GetProcessMitigationPolicy(::GetCurrentProcess(), ProcessDynamicCodePolicy, &dcp, sizeof(dcp));
	dcp.ProhibitDynamicCode = true;
	SetProcessMitigationPolicyEnsureOK(ProcessDynamicCodePolicy, &dcp, sizeof(dcp));*/

	if (!IsAnyIMEInstalled()) // this breaks IME apparently (https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute)
	{
		PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY epdp;
		GetProcessMitigationPolicy(::GetCurrentProcess(), ProcessExtensionPointDisablePolicy, &epdp, sizeof(epdp));
		epdp.DisableExtensionPoints = true;
		SetProcessMitigationPolicyEnsureOK(ProcessExtensionPointDisablePolicy, &epdp, sizeof(epdp));
	}

	/*PROCESS_MITIGATION_CONTROL_FLOW_GUARD_POLICY cfgp;
	GetProcessMitigationPolicy(::GetCurrentProcess(), ProcessControlFlowGuardPolicy, &cfgp, sizeof(cfgp));
	cfgp.EnableControlFlowGuard = true; // This field cannot be changed via SetProcessMitigationPolicy.
	//cfgp.StrictMode = true; // this needs to be disabled to load stubs with no CRT
	SetProcessMitigationPolicyEnsureOK(ProcessControlFlowGuardPolicy, &cfgp, sizeof(cfgp));*/

	PROCESS_MITIGATION_IMAGE_LOAD_POLICY ilp;
	GetProcessMitigationPolicy(::GetCurrentProcess(), ProcessImageLoadPolicy, &ilp, sizeof(ilp));
	ilp.PreferSystem32Images = true;
	ilp.NoRemoteImages = true;
	SetProcessMitigationPolicyEnsureOK(ProcessImageLoadPolicy, &ilp, sizeof(ilp));

	PROCESS_MITIGATION_USER_SHADOW_STACK_POLICY usspp;
	GetProcessMitigationPolicy(::GetCurrentProcess(), ProcessUserShadowStackPolicy, &usspp, sizeof(usspp));
	if (usspp.EnableUserShadowStack)
	{
		//MessageBoxA(0, "Enabling shadow stack strict mode", "", 0);
		usspp.EnableUserShadowStackStrictMode = true;
		SetProcessMitigationPolicyEnsureOK(ProcessUserShadowStackPolicy, &usspp, sizeof(usspp));
	} //else MessageBoxA(0, "Shadow stack is not enabled!!!", "", 0);
}

//int main(int argc, char* argv[])
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow)
{
	//AllocConsole();
	//SetConsoleTitle(TEXT("R1Delta Launcher"));
	//FILE *pCout, *pCerr;
	//freopen_s(&pCout, "conout$", "w", stdout);
	//freopen_s(&pCerr, "conout$", "w", stderr);

	if (!GetExePathWide(exePath, sizeof(exePath)))
	{
		MessageBoxA(
			GetForegroundWindow(),
			"Failed getting game directory.\nThe game cannot continue and has to exit.",
			"R1Delta Launcher Error",
			0);
		return 1;
	}

	SetCurrentDirectory(exePath);

	{
		SetMitigationPolicies();

		PrependPath();

		printf("[*] Loading launcher.dll\n");
		swprintf_s(buffer, L"%s\\bin\\x64_retail\\launcher.dll", exePath);
		hLauncherModule = LoadLibraryExW(buffer, 0, LOAD_WITH_ALTERED_SEARCH_PATH);
		if (!hLauncherModule)
		{
			LibraryLoadError(GetLastError(), L"launcher.dll", buffer);
			return 1;
		}
	}

	printf("[*] Launching the game...\n");
	auto LauncherMain = GetLauncherMain();
	if (!LauncherMain)
	{
		MessageBoxA(
			GetForegroundWindow(),
			"Failed loading launcher.dll.\nThe game cannot continue and has to exit.",
			"R1Delta Launcher Error",
			0);
		return 1;
	}

	return ((int(/*__fastcall*/*)(HINSTANCE, HINSTANCE, LPSTR, int))LauncherMain)(
		hInstance, hPrevInstance, lpCmdLine, nCmdShow); // the parameters aren't really used anyways
}
