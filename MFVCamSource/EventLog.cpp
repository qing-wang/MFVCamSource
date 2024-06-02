// EventLog.cpp: Implementierung der Klasse CEventLog.
//
//////////////////////////////////////////////////////////////////////
#include "pch.h"
#include <windows.h>
#include <stdint.h>
#include "EventLog.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

//////////////////////////////////////////////////////////////////////
// Konstruktion/Destruktion
//////////////////////////////////////////////////////////////////////

EventLog::EventLog()
{
	m_hLog = NULL;
}

EventLog::~EventLog()
{
	if (m_hLog != NULL)
	{
		DeregisterEventSource(m_hLog);
		m_hLog = NULL;
	}
}

BOOL EventLog::Initialize(std::string csApp)
{
	if (AddEventSource(csApp, 3 ) != 0)
	{
#if 0
		std::string cs = fmt::format("Unable to register EventLog access for application{}.", csApp);
		cs += "  Please log in with admin rights to do this.";
		cs += "  \nApplication will run without event logging";
#endif
	}
	m_hLog = ::RegisterEventSourceA( NULL, csApp.c_str());
	return TRUE;
}

DWORD EventLog::AddEventSource(std::string csName, DWORD dwCategoryCount)
{
	HKEY	hRegKey = NULL; 
	DWORD	dwError = 0;
	char	szPath[ MAX_PATH ];
	//
	sprintf_s(szPath, "SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\%s", csName);
	//
	dwError = RegCreateKeyA( HKEY_LOCAL_MACHINE, szPath, &hRegKey );
	//
	GetModuleFileNameA( NULL, szPath, MAX_PATH );
	//
	dwError = RegSetValueExA( hRegKey, "EventMessageFile", 0, REG_EXPAND_SZ, (PBYTE) szPath, strlen( szPath) + 1); 
	if (dwError == 0)
    {
		DWORD dwTypes = EVENTLOG_ERROR_TYPE | EVENTLOG_WARNING_TYPE | EVENTLOG_INFORMATION_TYPE; 
		dwError = RegSetValueExA( hRegKey, "TypesSupported",	0, REG_DWORD, (LPBYTE) &dwTypes, sizeof dwTypes );
		//
		if(dwError == 0 && dwCategoryCount > 0 ) 
		{
			dwError = RegSetValueExA( hRegKey, "CategoryMessageFile", 0, REG_EXPAND_SZ, (PBYTE) szPath, (strlen( szPath) + 1));
			if (dwError == 0)
				dwError = RegSetValueExA( hRegKey, "CategoryCount", 0, REG_DWORD, (PBYTE) &dwCategoryCount, sizeof dwCategoryCount );
		}
	}	
	RegCloseKey( hRegKey );
	return dwError;
}

DWORD EventLog::RemoveEventSource(std::string csApp)
{
	DWORD dwError = 0;
	char szPath[ MAX_PATH ];
	//
	sprintf_s(szPath, "SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\%s", csApp);
	return RegDeleteKeyA( HKEY_LOCAL_MACHINE, szPath );
}

BOOL EventLog::Fire(WORD wType, WORD wCategory, DWORD dwEventID, const char *message)
{
	if (m_hLog == NULL)
		return FALSE;
	//
	LPCSTR messages[1];
	messages[0] = message;
	BOOL bRet = ReportEventA(m_hLog, wType, wCategory, dwEventID,	NULL, 1, 0, messages, NULL);
	return bRet;
}
