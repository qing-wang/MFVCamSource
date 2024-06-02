#pragma once
#include <stdio.h>
#include <windows.h>
#include <string>

class EventLog
{
public:
	EventLog();
	virtual ~EventLog();
	//
	HANDLE GetHandle() { return m_hLog; }
	//
	BOOL Fire(WORD wType, WORD wCategory, DWORD dwEventID, const char *message);
	//
	BOOL Initialize(std::string csApp);
	DWORD AddEventSource(std::string csName, DWORD dwCategoryCount = 0);
	DWORD RemoveEventSource(std::string csApp);
protected:
	HANDLE m_hLog;
};
