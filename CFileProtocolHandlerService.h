#pragma once
#include "framework.h"
#include <string>
#include <tuple>
#include <memory>
#include <algorithm>

#include <shellapi.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

namespace {

	using namespace std::string_literals;

	std::wstring getModulePath()
	{
		WCHAR modulePath[MAX_PATH] = {};
		::GetModuleFileName(nullptr, modulePath, _countof(modulePath));
		return modulePath;
	}

	class CFileProtocolHandlerService
	{
		inline static const LPCWSTR softwareClassKey = LR"(Software\Classes)";
		inline static const LPCWSTR shellCommandKey = LR"(shell\open\command)";

		std::wstring protocolName;

		auto OpenProtocolKey()
		{
			auto subKey = (softwareClassKey + L"\\"s + protocolName);

			HKEY hKey;
			auto result = ::RegOpenKeyEx(HKEY_CURRENT_USER, subKey.c_str(), 0, KEY_READ, &hKey);
			return std::make_pair(result,
				std::unique_ptr<std::remove_pointer<HKEY>::type, decltype (&::RegCloseKey)>{hKey, & ::RegCloseKey});
		}
	public:
		explicit CFileProtocolHandlerService(LPCWSTR lpProtocolName)
			: protocolName(lpProtocolName)
		{
		}
		HRESULT OpenFile(LPCWSTR commandline)
		{
			auto protocolNameWithColon = protocolName + L":";
			if (_wcsnicmp(commandline, protocolNameWithColon.c_str(), protocolNameWithColon.length()) == 0)
			{
				auto filespec = commandline + protocolNameWithColon.length();

				WCHAR path[MAX_PATH];
				{
					WCHAR dospath[MAX_PATH];
					DWORD cchPath = _countof(dospath);
					// '\'はエスケープされているので、アンエスケープする
					if (S_OK != ::UrlUnescape((PWSTR)filespec, dospath, &cchPath, URL_ESCAPE_AS_UTF8))
					{
						return S_FALSE;
					}

					::PathCanonicalize(path, dospath);
				}

				if (wcslen(path) < 3) // 3文字くらいはあるはず。
				{
					return S_FALSE;
				}

				if (path[wcslen(path) - 1] == L'/') //末尾の/を取り除く
				{
					path[wcslen(path) - 1] = L'\0';
				}

				if (0 == wcsncmp(L"//", path, 2))
				{
					using namespace std;
					std::replace(begin(path), end(path), L'/', L'\\');
				}

				if (!::PathIsNetworkPath(path))
				{
					return S_FALSE;
				}

				OutputDebugString(path);

				SHELLEXECUTEINFO execInfo =
				{
					sizeof(SHELLEXECUTEINFO),
					SEE_MASK_CONNECTNETDRV | SEE_MASK_FLAG_DDEWAIT | SEE_MASK_NOCLOSEPROCESS,
					nullptr,
					L"open",
					path,
					nullptr, // lpParameters
					nullptr, // lpDirectory
					SW_NORMAL,
				};
				if (!::ShellExecuteEx(&execInfo))
				{
					return HRESULT_FROM_WIN32(::GetLastError());
				}

				if (execInfo.hProcess)
				{
					if (::WaitForInputIdle(execInfo.hProcess, 5 * 1000))
					{
						::AllowSetForegroundWindow(GetProcessId(nullptr));

						struct
						{
							DWORD const targetPid;
							HWND targetHWnd;
						} targetInfo = { ::GetProcessId(execInfo.hProcess) };
						::EnumWindows([](HWND hWnd, LPARAM lparam)-> BOOL
							{
								auto pTargetInfo = reinterpret_cast<decltype(&targetInfo)>(lparam);
								DWORD const pid = ::GetWindowThreadProcessId(hWnd, nullptr);
								if (pid == pTargetInfo->targetPid)
								{
									pTargetInfo->targetHWnd = hWnd;
									return TRUE;
								}
								return FALSE;
							}, reinterpret_cast<LRESULT>(&targetInfo));

						if (targetInfo.targetHWnd)
						{
							::BringWindowToTop(targetInfo.targetHWnd);
						}
					}
					::CloseHandle(execInfo.hProcess);
				}

				return S_OK;
			}

			return S_FALSE;
		}
		auto ProtocolName() const
		{
			return protocolName;
		}
		bool IsValidProtocolName() const
		{
			return protocolName.length() > 0 && protocolName[0] != L'.';
		}

		HRESULT IsRegistered()
		{
			auto const key = OpenProtocolKey();
			return key.first == ERROR_FILE_NOT_FOUND ? S_FALSE : HRESULT_FROM_WIN32(key.first);
		}

		std::pair<HRESULT, std::wstring> GetRegisteredPath()
		{
			DWORD dwType = REG_SZ;
			auto const key = OpenProtocolKey();
			WCHAR command[MAX_PATH + 100];
			DWORD cbData = sizeof(command);
			auto const result = ::RegGetValue(key.second.get(),
				shellCommandKey, nullptr, RRF_RT_REG_SZ, &dwType, command, &cbData);
			return std::make_pair(result, command);
		}
		HRESULT Register()
		{
			std::wstring command = getModulePath();
			command.insert(0, 1, L'"');
			command.append(L"\" %1");

			auto const subKey = (softwareClassKey + L"\\"s + protocolName);

			auto description = L"URL: FileProtocolHandler";

			LSTATUS result;

			result = ::RegSetKeyValue(HKEY_CURRENT_USER,
				subKey.c_str(), nullptr, REG_SZ, description,
				static_cast<DWORD>(std::wcslen(description) * sizeof(WCHAR)));

			if (result != ERROR_SUCCESS)
				return HRESULT_FROM_WIN32(result);

			result = ::RegSetKeyValue(HKEY_CURRENT_USER,
				subKey.c_str(), L"URL Protocol", REG_SZ, nullptr, 0);

			if (result != ERROR_SUCCESS)
				return HRESULT_FROM_WIN32(result);

			result = ::RegSetKeyValue(HKEY_CURRENT_USER,
				(subKey + L"\\"s + shellCommandKey).c_str(), nullptr, REG_SZ, command.c_str(),
				static_cast<DWORD>(command.length() * sizeof(WCHAR)));

			return HRESULT_FROM_WIN32(result);
		}
		HRESULT Unregister()
		{
			auto subKey = (softwareClassKey + L"\\"s + protocolName);

			auto const  result = ::RegDeleteTree(HKEY_CURRENT_USER, subKey.c_str());
			return HRESULT_FROM_WIN32(result);
		}
	};

}