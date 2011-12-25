// copyasi.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

void Usage()
{
	_TCHAR fullName[MAX_PATH], exeName[MAX_PATH], drive[_MAX_DRIVE], path[_MAX_PATH], ext[_MAX_EXT];
	GetModuleFileName(NULL, fullName, MAX_PATH);
	_tsplitpath_s(fullName, drive, path, exeName, ext);

	_ftprintf(stderr, _T("Usage: %s [srcfile] [dest]\n"), exeName);
	_ftprintf(stderr, _T("  Copy source file to destination. \n"));
	_ftprintf(stderr, _T("  The copy will trigger shutdown events and then wait for done event to be triggered.\n"));
	_ftprintf(stderr, _T("\n"));
	_ftprintf(stderr, _T("<Switches>\n"));
	_ftprintf(stderr, _T(" -i:<path>    Source file name \n"));
	_ftprintf(stderr, _T(" -o:<path>    Output file name. If omitted, the standard plugin locations are searched\n"));
	_ftprintf(stderr, _T(" -w:<time>    Time to wait in seconds for shutdown done event to complete\n"));
	_ftprintf(stderr, _T("\n"));
}

static time_t GetFileModTime(const _TCHAR *fileName)
{
	if (fileName == NULL || fileName[0] == 0)
		return 0;
	struct _stat istat;
	if ( _tstat(fileName, &istat) == 0 )
		return istat.st_mtime;
	return 0;
}

int _tmain(int argc, _TCHAR* argv[])
{
	_TCHAR sourcePath[MAX_PATH];
	_TCHAR destPath[MAX_PATH];
	sourcePath[0] = 0;
	destPath[0] = 0;
	int waitTime = 15;

	for (int i=1;i<argc;++i)
	{
		_TCHAR* arg = argv[i];
		if ( arg == NULL || arg[0] == 0)
			continue;
		if (arg[0] == '-' || arg[0] == '/')
		{
			switch (tolower(arg[1]))
			{
			case 'i':
				{
					const _TCHAR *param = arg+2;
					if (*param == ':' || *param=='=') ++param;
					argv[i] = NULL;
					if ( param[0] == 0 && ( i+1<argc && ( argv[i+1][0] != '-' || argv[i+1][0] != '/' ) ) ) {
						param = argv[++i];
						argv[i] = NULL;
					}
					if ( param[0] == 0 )
						break;
					GetFullPathName(param, _countof(sourcePath), sourcePath, NULL);
				} break;

			case 'o':
				{
					const _TCHAR *param = arg+2;
					if (*param == ':' || *param=='=') ++param;
					argv[i] = NULL;
					if ( param[0] == 0 && ( i+1<argc && ( argv[i+1][0] != '-' || argv[i+1][0] != '/' ) ) ) {
						param = argv[++i];
						argv[i] = NULL;
					}
					if ( param[0] == 0 )
						break;
					GetFullPathName(param, _countof(destPath), destPath, NULL);
				} break;

			case 'w':
				{
					const _TCHAR *param = arg+2;
					if (*param == ':' || *param=='=') ++param;
					argv[i] = NULL;
					if ( param[0] == 0 && ( i+1<argc && ( argv[i+1][0] != '-' || argv[i+1][0] != '/' ) ) ) {
						param = argv[++i];
						argv[i] = NULL;
					}
					if ( param[0] == 0 )
						break;

					waitTime = _ttol(param);
				} break;

			default:
				_ftprintf(stderr, _T("ERROR: Unknown argument specified '%s'\n\n"), arg);
				Usage();
				return 1;
			}
		}
		else if ( sourcePath[0] == 0 )
		{
			GetFullPathName(arg, _countof(sourcePath), sourcePath, NULL);
		}
		else if ( destPath[0] == 0 )
		{
			GetFullPathName(arg, _countof(destPath), destPath, NULL);
		}
		else
		{
			_ftprintf(stderr, _T("ERROR: Unknown argument specified '%s'\n\n"), arg);
			Usage();
			return 1;
		}
	}
	if (sourcePath[0] == 0)
	{
		Usage();
		return 1;
	}
	
	// find the plugin via the registry
	if (destPath[0] == 0)
	{
		HKEY hKey = NULL;
		if ( ERROR_SUCCESS != RegOpenKeyEx(HKEY_LOCAL_MACHINE, _T("SOFTWARE\\Wow6432Node\\Bethesda Softworks\\Skyrim"), 0, KEY_QUERY_VALUE , &hKey))
		{
			if ( ERROR_SUCCESS != RegOpenKeyEx(HKEY_LOCAL_MACHINE, _T("SOFTWARE\\Bethesda Softworks\\Skyrim"), 0, KEY_QUERY_VALUE , &hKey))
			{
				hKey = NULL;
			}
		}
		if (hKey != NULL)
		{
			DWORD lpType = REG_SZ;
			_TCHAR szValue[MAX_PATH];
			DWORD dwLength = _countof(szValue);
			if ( ERROR_SUCCESS == RegQueryValueEx(hKey, _T("Installed Path"), NULL, &lpType, (LPBYTE)szValue, &dwLength ) )
			{
				_TCHAR szTempPath[MAX_PATH];
				
				_TCHAR fullName[MAX_PATH], fname[MAX_PATH], drive[_MAX_DRIVE], path[_MAX_PATH], ext[_MAX_EXT];
				_tsplitpath_s(sourcePath, drive, path, fname, ext);
				_TCHAR *paths[] = { _T("proxy"), _T("asi\\proxy"), _T("plugins\\proxy"), _T("data\\asi\\proxy") };
				for (int i=0; i<_countof(paths); ++i)
				{
					PathCombine(szTempPath, szValue, paths[i]);
					PathCombine(fullName, szTempPath, fname);
					PathAddExtension(fullName, _T(".asi"));
					if ( 0 == _taccess_s(fullName, 06) )
					{
						GetFullPathName(fullName, _countof(destPath), destPath, NULL);
						break;
					}
				}
			}
			RegCloseKey(hKey);
		}
	}
	if (destPath[0] == 0)
	{
		_ftprintf(stderr, _T("ERROR: Unable to locate destination path\n\n"));
		Usage();
		return 1;
	}
	time_t srcTime = GetFileModTime(sourcePath);
	time_t dstTime = GetFileModTime(destPath);
	if (srcTime == dstTime)
	{
		_ftprintf(stderr, _T("Source and destination have not changed. Files not copied.\n"));
		return 0;
	}


	HANDLE asiShutdownEvent = INVALID_HANDLE_VALUE;
	HANDLE asiShutdownEventDone = INVALID_HANDLE_VALUE;
	HMODULE asiFileHandle = NULL;

	_TCHAR pluginName[MAX_PATH];
	_tsplitpath_s(destPath, NULL,0, NULL,0,pluginName, _countof(pluginName), NULL, 0);

	_TCHAR szTempName[256];
	if (asiShutdownEvent == INVALID_HANDLE_VALUE)
	{
		_stprintf_s(szTempName, _T("TESV.ASI.%s.SHUTDOWN"), pluginName);
		_tcsupr_s(szTempName);
		asiShutdownEvent = OpenEvent(EVENT_ALL_ACCESS, FALSE, szTempName);

		if (asiShutdownEvent == INVALID_HANDLE_VALUE)
		{
			_ftprintf(stderr, _T("\n\n"));
		}
		
	}
	if (asiShutdownEventDone == INVALID_HANDLE_VALUE)
	{
		_stprintf_s(szTempName, _T("TESV.ASI.%s.SHUTDOWNDONE"), pluginName);		
		_tcsupr_s(szTempName);
		asiShutdownEventDone = OpenEvent(EVENT_ALL_ACCESS, FALSE, szTempName);
	}

	bool doCopy = false;
	if (asiShutdownEvent == INVALID_HANDLE_VALUE)
	{
		// no handle means proxy is not running
		doCopy = true;
	}
	else
	{
		SetEvent(asiShutdownEvent);

		DWORD dwResult = WaitForSingleObject(asiShutdownEventDone, waitTime*1000);
		if (dwResult == WAIT_OBJECT_0 || dwResult == WAIT_FAILED)// failed occurs when event is not opened on remote app
			doCopy = true;
		else if (dwResult == WAIT_TIMEOUT)
			_ftprintf(stderr, _T("Timed out waiting for shutdown to complete\n"));
		else if (dwResult == WAIT_ABANDONED)
			_ftprintf(stderr, _T("Waiting handle during shutdown was abandoned\n"));
		else 
			_ftprintf(stderr, _T("Unexpected result while waiting\n"));
	}
	if (asiShutdownEvent != INVALID_HANDLE_VALUE)
	{
		CloseHandle(asiShutdownEvent);
		asiShutdownEvent = INVALID_HANDLE_VALUE;
	}
	if (asiShutdownEventDone != INVALID_HANDLE_VALUE)
	{
		CloseHandle(asiShutdownEventDone);
		asiShutdownEventDone = INVALID_HANDLE_VALUE;
	}
	int iResult = 1;
	if (doCopy)
	{
		if ( !CopyFile(sourcePath, destPath, FALSE) )
		{
			_ftprintf(stderr, _T("Failed to copy '%s' to '%s'\n"), sourcePath, destPath);
			iResult = 1;
		}
		else
		{
			_ftprintf(stderr, _T("Copy succeeded to '%s'\n"), sourcePath, destPath);
			iResult = 0;
		}
	}
	return iResult;
}

