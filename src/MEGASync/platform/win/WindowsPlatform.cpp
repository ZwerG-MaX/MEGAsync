#include "WindowsPlatform.h"
#include <Shlobj.h>
#include <Shlwapi.h>
#include <tchar.h>
#include <Aclapi.h>
#include <AccCtrl.h>
#include <comdef.h>
#include <wincred.h>
#include <taskschd.h>
#include <Sddl.h>
#include <time.h>
#include <iostream>

#if QT_VERSION >= 0x050200
#include <QtWin>
#endif

// Windows headers don't define this for WinXP despite the documentation says that they should
// and it indeed works
#ifndef SHFOLDERCUSTOMSETTINGS
#include <pshpack8.h>

// Used by SHGetSetFolderCustomSettings
typedef struct
{
    DWORD           dwSize;
    DWORD           dwMask;                 // IN/OUT  Which Attributes to Get/Set
    SHELLVIEWID*    pvid;                   // OUT - if dwReadWrite is FCS_READ, IN - otherwise
    // The folder's WebView template path
    LPWSTR          pszWebViewTemplate;     // OUT - if dwReadWrite is FCS_READ, IN - otherwise
    DWORD           cchWebViewTemplate;     // IN - Specifies the size of the buffer pointed to by pszWebViewTemplate
                                            // Ignored if dwReadWrite is FCS_READ
    LPWSTR           pszWebViewTemplateVersion;  // currently IN only
    // Infotip for the folder
    LPWSTR          pszInfoTip;             // OUT - if dwReadWrite is FCS_READ, IN - otherwise
    DWORD           cchInfoTip;             // IN - Specifies the size of the buffer pointed to by pszInfoTip
                                            // Ignored if dwReadWrite is FCS_READ
    // CLSID that points to more info in the registry
    CLSID*          pclsid;                 // OUT - if dwReadWrite is FCS_READ, IN - otherwise
    // Other flags for the folder. Takes FCS_FLAG_* values
    DWORD           dwFlags;                // OUT - if dwReadWrite is FCS_READ, IN - otherwise


    LPWSTR           pszIconFile;           // OUT - if dwReadWrite is FCS_READ, IN - otherwise
    DWORD            cchIconFile;           // IN - Specifies the size of the buffer pointed to by pszIconFile
                                            // Ignored if dwReadWrite is FCS_READ

    int              iIconIndex;            // OUT - if dwReadWrite is FCS_READ, IN - otherwise

    LPWSTR           pszLogo;               // OUT - if dwReadWrite is FCS_READ, IN - otherwise
    DWORD            cchLogo;               // IN - Specifies the size of the buffer pointed to by pszIconFile
                                            // Ignored if dwReadWrite is FCS_READ
} SHFOLDERCUSTOMSETTINGS, *LPSHFOLDERCUSTOMSETTINGS;

#include <poppack.h>        /* Return to byte packing */

// Gets/Sets the Folder Custom Settings for pszPath based on dwReadWrite. dwReadWrite can be FCS_READ/FCS_WRITE/FCS_FORCEWRITE
SHSTDAPI SHGetSetFolderCustomSettings(_Inout_ LPSHFOLDERCUSTOMSETTINGS pfcs, _In_ PCWSTR pszPath, DWORD dwReadWrite);
#endif

WinShellDispatcherTask* WindowsPlatform::shellDispatcherTask = NULL;

using namespace std;
using namespace mega;

void WindowsPlatform::initialize(int argc, char *argv[])
{
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
}

void WindowsPlatform::prepareForSync()
{
    Preferences *preferences = Preferences::instance();

    QProcess p;
    p.start(QString::fromUtf8("net use"));
    p.waitForFinished(2000);
    QString output = QString::fromUtf8(p.readAllStandardOutput().constData());
    QString e = QString::fromUtf8(p.readAllStandardError().constData());
    if (e.size())
    {
        MegaApi::log(MegaApi::LOG_LEVEL_ERROR, "Error for \"net use\" command:");
        MegaApi::log(MegaApi::LOG_LEVEL_ERROR, e.toUtf8().constData());
    }

    QStringList data = output.split(QString::fromUtf8("\n"));
    if (data.size() > 1)
    {
        for (int i = 1; i < data.size(); i++)
        {
            QString drive = data.at(i).trimmed();
            if (drive.size() && drive.contains(QChar::fromAscii(':')))
            {
                int index = drive.indexOf(QString::fromUtf8(":"));
                if (index >= 2 && drive[index - 2] == QChar::fromAscii(' '))
                {
                    drive = drive.mid(index - 1);
                    QStringList parts = drive.split(QString::fromUtf8(" "), QString::SkipEmptyParts);
                    if (parts.size() >= 2 && parts.at(1).startsWith(QString::fromUtf8("\\")))
                    {
                        QString driveName = parts.at(0);
                        QString networkName = parts.at(1);
                        MegaApi::log(MegaApi::LOG_LEVEL_INFO, QString::fromUtf8("Network drive detected: %1 (%2)")
                                    .arg(networkName).arg(driveName).toUtf8().constData());

                        QStringList localFolders = preferences->getLocalFolders();
                        for (int i = 0; i < localFolders.size(); i++)
                        {
                            QString localFolder = localFolders.at(i);
                            MegaApi::log(MegaApi::LOG_LEVEL_INFO, QString::fromUtf8("Checking local synced folder: %1").arg(localFolder).toUtf8().constData());
                            if (localFolder.startsWith(driveName) || localFolder.startsWith(networkName))
                            {
                                MegaApi::log(MegaApi::LOG_LEVEL_INFO, QString::fromUtf8("Automatically mounting network drive")
                                             .toUtf8().constData());

                                QProcess p;
                                QString command = QString::fromUtf8("net use %1 %2").arg(driveName).arg(networkName);
                                p.start(command);
                                p.waitForFinished(2000);
                                QString output = QString::fromUtf8(p.readAllStandardOutput().constData());
                                QString e = QString::fromUtf8(p.readAllStandardError().constData());
                                MegaApi::log(MegaApi::LOG_LEVEL_INFO, QString::fromUtf8("Output for \"%1\" command:").arg(command).toUtf8().constData());
                                MegaApi::log(MegaApi::LOG_LEVEL_INFO, output.toUtf8().constData());
                                if (e.size())
                                {
                                    MegaApi::log(MegaApi::LOG_LEVEL_INFO, QString::fromUtf8("Error for \"%1\" command:").arg(command).toUtf8().constData());
                                    MegaApi::log(MegaApi::LOG_LEVEL_ERROR, e.toUtf8().constData());
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
}

bool WindowsPlatform::enableTrayIcon(QString executable)
{
    HRESULT hr;
    ITrayNotify *m_ITrayNotify;
    ITrayNotifyNew *m_ITrayNotifyNew;

    hr = CoCreateInstance (
          __uuidof (TrayNotify),
          NULL,
          CLSCTX_LOCAL_SERVER,
          __uuidof (ITrayNotify),
          (PVOID *) &m_ITrayNotify);
    if (hr != S_OK)
    {
        hr = CoCreateInstance (
          __uuidof (TrayNotify),
          NULL,
          CLSCTX_LOCAL_SERVER,
          __uuidof (ITrayNotifyNew),
          (PVOID *) &m_ITrayNotifyNew);
        if (hr != S_OK)
        {
            return false;
        }
    }

    WinTrayReceiver *receiver = NULL;
    if (m_ITrayNotify)
    {
        receiver = new WinTrayReceiver(m_ITrayNotify, executable);
    }
    else
    {
        receiver = new WinTrayReceiver(m_ITrayNotifyNew, executable);
    }

    hr = receiver->start();
    if (hr != S_OK)
    {
        return false;
    }

    return true;
}

void WindowsPlatform::notifyItemChange(std::string *localPath, int)
{
    if (!localPath || !localPath->size())
    {
        return;
    }

    std::string path = *localPath;
    if (!memcmp(path.data(), L"\\\\?\\", 8))
    {
        path = path.substr(8);
    }

    path.append("", 1);
    if (path.length() >= MAX_PATH)
    {
        return;
    }

    SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATH, path.data(), NULL);
}

//From http://msdn.microsoft.com/en-us/library/windows/desktop/bb776891.aspx
HRESULT WindowsPlatform::CreateLink(LPCWSTR lpszPathObj, LPCWSTR lpszPathLink, LPCWSTR lpszDesc,
                                 LPCWSTR pszIconfile, int iIconindex)
{
    HRESULT hres;
    IShellLink* psl;

    // Get a pointer to the IShellLink interface. It is assumed that CoInitialize
    // has already been called.
    hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl);
    if (SUCCEEDED(hres))
    {
        IPersistFile* ppf;

        // Set the path to the shortcut target and add the description.
        psl->SetPath(lpszPathObj);
        psl->SetDescription(lpszDesc);
        if (pszIconfile)
        {
            psl->SetIconLocation(pszIconfile, iIconindex);
        }

        // Query IShellLink for the IPersistFile interface, used for saving the
        // shortcut in persistent storage.
        hres = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf);

        if (SUCCEEDED(hres))
        {
            // Save the link by calling IPersistFile::Save.
            hres = ppf->Save(lpszPathLink, TRUE);
            ppf->Release();
        }
        psl->Release();
    }
    return hres;
}

DWORD MEGARegDeleteKeyEx(HKEY hKey, LPCWSTR lpSubKey, REGSAM samDesired, DWORD Reserved)
{
    typedef DWORD (WINAPI *RegDeleteKeyExWPtr)(HKEY hKey,
                                                     LPCWSTR lpSubKey,
                                                     REGSAM samDesired,
                                                     DWORD Reserved);

    RegDeleteKeyExWPtr RegDeleteKeyExWFunc;
    HMODULE hMod = GetModuleHandleW(L"advapi32.dll");
    if (!hMod || !(RegDeleteKeyExWFunc = (RegDeleteKeyExWPtr)GetProcAddress(hMod, "RegDeleteKeyExW")))
    {
        return RegDeleteKey(hKey, lpSubKey);
    }

    return RegDeleteKeyExWFunc(hKey, lpSubKey, samDesired, Reserved);
}

bool DeleteRegKey(HKEY key, LPTSTR subkey, REGSAM samDesired)
{
    TCHAR keyPath[MAX_PATH];
    DWORD maxSize;
    HKEY hKey;

    LONG result = MEGARegDeleteKeyEx(key, subkey, samDesired, 0);
    if (result == ERROR_SUCCESS)
    {
        return true;
    }

    result = RegOpenKeyEx(key, subkey, 0, samDesired | KEY_READ, &hKey);
    if (result != ERROR_SUCCESS)
    {
        return (result == ERROR_FILE_NOT_FOUND);
    }

    int len =  _tcslen(subkey);
    if (!len || len >= (MAX_PATH - 1) || _tcscpy_s(keyPath, MAX_PATH, subkey))
    {
        RegCloseKey(hKey);
        return false;
    }

    LPTSTR endPos = keyPath + len - 1;
    if (*(endPos++) != TEXT('\\'))
    {
        *(endPos++) =  TEXT('\\');
        *endPos =  TEXT('\0');
        len++;
    }

    do
    {
        maxSize = MAX_PATH - len;
    } while (RegEnumKeyEx(hKey, 0, endPos, &maxSize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS
             && DeleteRegKey(key, keyPath, samDesired));

    RegCloseKey(hKey);
    return (MEGARegDeleteKeyEx(key, subkey, samDesired, 0) == ERROR_SUCCESS);
}

bool DeleteRegValue(HKEY key, LPTSTR subkey, LPTSTR value, REGSAM samDesired)
{
    HKEY hKey;
    LONG result = RegOpenKeyEx(key, subkey, 0, samDesired | KEY_SET_VALUE, &hKey);
    if (result != ERROR_SUCCESS)
    {
        return (result == ERROR_FILE_NOT_FOUND);
    }

    result = RegDeleteValue(hKey, value);
    RegCloseKey(hKey);
    return (result == ERROR_SUCCESS);
}

bool SetRegistryKeyAndValue(HKEY hkey, PCWSTR subkey, DWORD dwType,
                            PCWSTR valuename, const BYTE *pszData, DWORD cbData,
                            REGSAM samDesired)
{
    HRESULT hr;
    HKEY hKey = NULL;
    hr = HRESULT_FROM_WIN32(RegCreateKeyEx(hkey, subkey, 0, NULL,
                                           REG_OPTION_NON_VOLATILE,
                                           KEY_WRITE | samDesired,
                                           NULL, &hKey, NULL));
    if (!SUCCEEDED(hr))
    {
        return false;
    }

    hr = HRESULT_FROM_WIN32(RegSetValueEx(hKey, valuename, 0, dwType, pszData, cbData));
    RegCloseKey(hKey);
    return SUCCEEDED(hr);
}

typedef BOOL (WINAPI *LPFN_ISWOW64PROCESS) (HANDLE, PBOOL);
LPFN_ISWOW64PROCESS fnIsWow64Process;

BOOL IsWow64()
{
    BOOL bIsWow64 = TRUE;
    fnIsWow64Process = (LPFN_ISWOW64PROCESS) GetProcAddress(
        GetModuleHandle(TEXT("kernel32")),"IsWow64Process");
    if (fnIsWow64Process)
    {
        fnIsWow64Process(GetCurrentProcess(),&bIsWow64);
    }
    return bIsWow64;
}

bool CheckLeftPaneIcon(wchar_t *path, bool remove)
{
    HKEY hKey = NULL;
    LONG result = RegOpenKeyEx(HKEY_CURRENT_USER,
                               L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Desktop\\NameSpace",
                               0, KEY_READ | KEY_ENUMERATE_SUB_KEYS, &hKey);
    if (result != ERROR_SUCCESS)
    {
        return false;
    }

    DWORD retCode = ERROR_SUCCESS;
    WCHAR uuid[MAX_PATH];
    DWORD uuidlen = sizeof(uuid);

    for (int i = 0; retCode == ERROR_SUCCESS; i++)
    {
        uuidlen = sizeof(uuid);
        uuid[0] = '\0';
        retCode = RegEnumKeyEx(hKey, i, uuid, &uuidlen, NULL, NULL, NULL, NULL);
        if (retCode == ERROR_SUCCESS)
        {
            HKEY hSubKey;
            TCHAR subKeyPath[MAX_PATH];
            swprintf_s(subKeyPath, MAX_PATH, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Desktop\\NameSpace\\%s", uuid);
            result = RegOpenKeyEx(HKEY_CURRENT_USER, subKeyPath, 0, KEY_READ, &hSubKey);
            if (result != ERROR_SUCCESS)
            {
                continue;
            }

            DWORD type;
            TCHAR value[MAX_PATH];
            DWORD valuelen = sizeof(value);
            result = RegQueryValueEx(hSubKey, L"", NULL, &type, (LPBYTE)value, &valuelen);
            RegCloseKey(hSubKey);
            if (result != ERROR_SUCCESS || type != REG_SZ || valuelen != 10 || memcmp(value, L"MEGA", 10))
            {
                continue;
            }

            if (path)
            {                
                bool found = false;

                swprintf_s(subKeyPath, MAX_PATH, L"Software\\Classes\\CLSID\\%s\\Instance\\InitPropertyBag", uuid);
                result = RegOpenKeyEx(HKEY_CURRENT_USER, subKeyPath, 0, KEY_READ, &hSubKey);
                if (result == ERROR_SUCCESS)
                {
                    valuelen = sizeof(value);
                    result = RegQueryValueEx(hSubKey, L"TargetFolderPath", NULL, &type, (LPBYTE)value, &valuelen);
                    RegCloseKey(hSubKey);
                    if (result == ERROR_SUCCESS && type == REG_EXPAND_SZ && !_wcsicmp(value, path))
                    {
                        found = true;
                    }
                }

                if (!found)
                {
                    swprintf_s(subKeyPath, MAX_PATH, L"Software\\Classes\\CLSID\\%s\\Instance\\InitPropertyBag", uuid);
                    result = RegOpenKeyEx(HKEY_CURRENT_USER, subKeyPath, 0, KEY_WOW64_64KEY | KEY_READ, &hSubKey);
                    if (result == ERROR_SUCCESS)
                    {
                        valuelen = sizeof(value);
                        result = RegQueryValueEx(hSubKey, L"TargetFolderPath", NULL, &type, (LPBYTE)value, &valuelen);
                        RegCloseKey(hSubKey);
                        if (result == ERROR_SUCCESS && type == REG_EXPAND_SZ && !_wcsicmp(value, path))
                        {
                            found = true;
                        }
                    }
                }

                if (!found)
                {
                    continue;
                }
            }

            if (remove)
            {
                wchar_t buffer[MAX_PATH];
                swprintf_s(buffer, MAX_PATH, L"Software\\Classes\\CLSID\\%s", uuid);

                if (IsWow64())
                {
                    DeleteRegKey(HKEY_CURRENT_USER, buffer, KEY_WOW64_64KEY);
                }
                DeleteRegKey(HKEY_CURRENT_USER, buffer, 0);

                DeleteRegValue(HKEY_CURRENT_USER,
                               (LPTSTR)L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\NewStartPanel",
                               uuid, 0);
                swprintf_s(buffer, MAX_PATH, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Desktop\\NameSpace\\%s", uuid);
                DeleteRegKey(HKEY_CURRENT_USER, buffer, 0);
            }
            RegCloseKey(hKey);
            return true;
        }
    }
    RegCloseKey(hKey);
    return false;
}

void WindowsPlatform::addSyncToLeftPane(QString syncPath, QString syncName, QString uuid)
{
    typedef LONG MEGANTSTATUS;
    typedef struct _MEGAOSVERSIONINFOW {
        DWORD dwOSVersionInfoSize;
        DWORD dwMajorVersion;
        DWORD dwMinorVersion;
        DWORD dwBuildNumber;
        DWORD dwPlatformId;
        WCHAR  szCSDVersion[ 128 ];     // Maintenance string for PSS usage
    } MEGARTL_OSVERSIONINFOW, *PMEGARTL_OSVERSIONINFOW;

    typedef MEGANTSTATUS (WINAPI* RtlGetVersionPtr)(PMEGARTL_OSVERSIONINFOW);
    MEGARTL_OSVERSIONINFOW version = { 0 };
    HMODULE hMod = GetModuleHandleW(L"ntdll.dll");
    if (!hMod)
    {
        return;
    }

    RtlGetVersionPtr RtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hMod, "RtlGetVersion");
    if (!RtlGetVersion)
    {
        return;
    }

    RtlGetVersion(&version);
    if (version.dwMajorVersion < 10)
    {
        return;
    }

    DWORD value;
    QString sValue;
    QString key = QString::fromUtf8("Software\\Classes\\CLSID\\%1").arg(uuid);
    REGSAM samDesired = 0;
    int it = 1;

    if (IsWow64())
    {
        samDesired = KEY_WOW64_64KEY;
        it++;
    }

    for (int i = 0; i < it; i++)
    {
        sValue = syncName;
        SetRegistryKeyAndValue(HKEY_CURRENT_USER, (LPTSTR)key.utf16(),
                               REG_SZ, L"", (const BYTE *)sValue.utf16(), sValue.size() * 2,
                               samDesired);

        sValue = MegaApplication::applicationFilePath();
        SetRegistryKeyAndValue(HKEY_CURRENT_USER, (LPTSTR)(key + QString::fromUtf8("\\DefaultIcon")).utf16(),
                               REG_EXPAND_SZ, L"", (const BYTE *)sValue.utf16(), sValue.size() * 2,
                               samDesired);

        value = 0x1;
        SetRegistryKeyAndValue(HKEY_CURRENT_USER, (LPTSTR)key.utf16(),
                               REG_DWORD, L"System.IsPinnedToNameSpaceTree", (const BYTE *)&value, sizeof(DWORD),
                               samDesired);

        value = 0x0;
        SetRegistryKeyAndValue(HKEY_CURRENT_USER, (LPTSTR)key.utf16(),
                               REG_DWORD, L"SortOrderIndex", (const BYTE *)&value, sizeof(DWORD),
                               samDesired);

        sValue = QString::fromUtf8("%systemroot%\\system32\\shell32.dll");
        SetRegistryKeyAndValue(HKEY_CURRENT_USER, (LPTSTR)(key + QString::fromUtf8("\\InProcServer32")).utf16(),
                               REG_EXPAND_SZ, L"", (const BYTE *)sValue.utf16(), sValue.size() * 2,
                               samDesired);

        sValue = QString::fromUtf8("{0E5AAE11-A475-4c5b-AB00-C66DE400274E}");
        SetRegistryKeyAndValue(HKEY_CURRENT_USER, (LPTSTR)(key + QString::fromUtf8("\\Instance")).utf16(),
                               REG_SZ, L"CLSID", (const BYTE *)sValue.utf16(), sValue.size() * 2,
                               samDesired);

        value = 0x10;
        SetRegistryKeyAndValue(HKEY_CURRENT_USER, (LPTSTR)(key + QString::fromUtf8("\\Instance\\InitPropertyBag")).utf16(),
                               REG_DWORD, L"Attributes", (const BYTE *)&value, sizeof(DWORD),
                               samDesired);

        sValue = QDir::toNativeSeparators(syncPath);
        SetRegistryKeyAndValue(HKEY_CURRENT_USER, (LPTSTR)(key + QString::fromUtf8("\\Instance\\InitPropertyBag")).utf16(),
                               REG_EXPAND_SZ, L"TargetFolderPath", (const BYTE *)sValue.utf16(), sValue.size() * 2,
                               samDesired);

        value = 0x28;
        SetRegistryKeyAndValue(HKEY_CURRENT_USER, (LPTSTR)(key + QString::fromUtf8("\\ShellFolder")).utf16(),
                               REG_DWORD, L"FolderValueFlags", (const BYTE *)&value, sizeof(DWORD),
                               samDesired);

        value = 0xF080004D;
        SetRegistryKeyAndValue(HKEY_CURRENT_USER, (LPTSTR)(key + QString::fromUtf8("\\ShellFolder")).utf16(),
                               REG_DWORD, L"Attributes", (const BYTE *)&value, sizeof(DWORD),
                               samDesired);

        samDesired = 0;
    }

    sValue = QString::fromUtf8("MEGA");
    SetRegistryKeyAndValue(HKEY_CURRENT_USER, (LPTSTR) QString::fromUtf8("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Desktop\\NameSpace\\%1").arg(uuid).utf16(),
                           REG_SZ, L"", (const BYTE *)sValue.utf16(), sValue.size() * 2,
                           samDesired);

    value = 0x1;
    SetRegistryKeyAndValue(HKEY_CURRENT_USER, (LPTSTR) QString::fromUtf8("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\NewStartPanel").utf16(),
                           REG_DWORD, (LPTSTR)uuid.utf16(), (const BYTE *)&value, sizeof(DWORD),
                           samDesired);
}

void WindowsPlatform::removeSyncFromLeftPane(QString, QString, QString uuid)
{
    REGSAM samDesired = 0;
    int it = 1;

    if (IsWow64())
    {
        samDesired = KEY_WOW64_64KEY;
        it++;
    }

    QString key = QString::fromUtf8("Software\\Classes\\CLSID\\%1").arg(uuid);
    for (int i = 0; i < it; i++)
    {
        DeleteRegKey(HKEY_CURRENT_USER, (LPTSTR)key.utf16(), samDesired);
        samDesired = 0;
    }

    key = QString::fromUtf8("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Desktop\\NameSpace\\%1").arg(uuid);
    DeleteRegKey(HKEY_CURRENT_USER, (LPTSTR)key.utf16(), 0);

    key = QString::fromUtf8("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\NewStartPanel");
    DeleteRegValue(HKEY_CURRENT_USER, (LPTSTR)key.utf16(), (LPTSTR)uuid.utf16(), 0);
}

void WindowsPlatform::removeAllSyncsFromLeftPane()
{
    bool deleted = true;
    while (deleted)
    {
        deleted &= CheckLeftPaneIcon(NULL, true);
    }
}

bool WindowsPlatform::makePubliclyReadable(LPTSTR fileName)
{
    bool result = false;
    PACL pOldDACL = NULL, pNewDACL = NULL;
    PSECURITY_DESCRIPTOR pSD = NULL;
    DWORD sidSize = SECURITY_MAX_SID_SIZE;
    EXPLICIT_ACCESS ea;

    ZeroMemory(&ea, sizeof(EXPLICIT_ACCESS));
    ea.grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE;
    ea.grfAccessMode = GRANT_ACCESS;
    ea.grfInheritance = NO_INHERITANCE;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    if (fileName
            && (GetNamedSecurityInfo(fileName, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &pOldDACL, NULL, &pSD) == ERROR_SUCCESS)
            && (ea.Trustee.ptstrName = (LPWSTR)LocalAlloc(LMEM_FIXED, sidSize))
            && CreateWellKnownSid(WinBuiltinUsersSid, NULL, ea.Trustee.ptstrName, &sidSize)
            && (SetEntriesInAcl(1, &ea, pOldDACL, &pNewDACL) == ERROR_SUCCESS)
            && (SetNamedSecurityInfo(fileName, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, pNewDACL, NULL) == ERROR_SUCCESS))
    {
        result = true;
    }

    if (ea.Trustee.ptstrName != NULL)
    {
        LocalFree(ea.Trustee.ptstrName);
    }
    if(pSD != NULL)
    {
        LocalFree((HLOCAL) pSD);
    }
    if(pNewDACL != NULL)
    {
        LocalFree((HLOCAL) pNewDACL);
    }
    return result;
}

bool WindowsPlatform::startOnStartup(bool value)
{
    WCHAR path[MAX_PATH];
    HRESULT res = SHGetFolderPathW(NULL, CSIDL_STARTUP, NULL, 0, path);
    if (res != S_OK)
    {
        return false;
    }

    QString startupPath = QString::fromWCharArray(path);
    startupPath += QString::fromAscii("\\MEGAsync.lnk");

    if (value)
    {
        if (QFile(startupPath).exists())
        {
            return true;
        }

        WCHAR wDescription[]=L"Start MEGAsync";
        WCHAR *wStartupPath = (WCHAR *)startupPath.utf16();

        QString exec = MegaApplication::applicationFilePath();
        exec = QDir::toNativeSeparators(exec);
        WCHAR *wExecPath = (WCHAR *)exec.utf16();

        res = CreateLink(wExecPath, wStartupPath, wDescription);

        if (res != S_OK)
        {
            return false;
        }
        return true;
    }
    else
    {
        QFile::remove(startupPath);
        return true;
    }
}

bool WindowsPlatform::isStartOnStartupActive()
{
    WCHAR path[MAX_PATH];
    HRESULT res = SHGetFolderPathW(NULL, CSIDL_STARTUP, NULL, 0, path);
    if (res != S_OK)
    {
        return false;
    }

    QString startupPath = QString::fromWCharArray(path);
    startupPath += QString::fromAscii("\\MEGAsync.lnk");
    if (QFileInfo(startupPath).isSymLink())
    {
        return true;
    }
    return false;
}

void WindowsPlatform::showInFolder(QString pathIn)
{
    if (!QFile(pathIn).exists())
    {
        return;
    }

    QString param;
    param = QString::fromUtf8("/select,");
    param += QString::fromAscii("\"\"") + QDir::toNativeSeparators(pathIn) + QString::fromAscii("\"\"");
    QProcess::startDetached(QString::fromAscii("explorer ") + param);
}

void WindowsPlatform::startShellDispatcher(MegaApplication *receiver)
{
    if (shellDispatcherTask)
    {
        return;
    }
    shellDispatcherTask = new WinShellDispatcherTask(receiver);
    shellDispatcherTask->start();
}

void WindowsPlatform::stopShellDispatcher()
{
    if (shellDispatcherTask)
    {
        shellDispatcherTask->exitTask();
        shellDispatcherTask->wait();
        delete shellDispatcherTask;
        shellDispatcherTask = NULL;
    }
}

void WindowsPlatform::syncFolderAdded(QString syncPath, QString syncName, QString syncID)
{
    if (syncPath.startsWith(QString::fromAscii("\\\\?\\")))
    {
        syncPath = syncPath.mid(4);
    }

    if (!syncPath.size())
    {
        return;
    }

    QDir syncDir(syncPath);
    if (!syncDir.exists())
    {
        return;
    }

    if (!Preferences::instance()->leftPaneIconsDisabled())
    {
        addSyncToLeftPane(syncPath, syncName, syncID);
    }

    DWORD dwVersion = GetVersion();
    DWORD dwMajorVersion = (DWORD)(LOBYTE(LOWORD(dwVersion)));
    int iconIndex = (dwMajorVersion<6) ? 2 : 3;

    QString infoTip = QCoreApplication::translate("WindowsPlatform", "MEGA synced folder");
    SHFOLDERCUSTOMSETTINGS fcs = {0};
    fcs.dwSize = sizeof(SHFOLDERCUSTOMSETTINGS);
    fcs.dwMask = FCSM_ICONFILE | FCSM_INFOTIP;
    fcs.pszIconFile = (LPWSTR)MegaApplication::applicationFilePath().utf16();
    fcs.iIconIndex = iconIndex;
    fcs.pszInfoTip = (LPWSTR)infoTip.utf16();
    SHGetSetFolderCustomSettings(&fcs, (LPCWSTR)syncPath.utf16(), FCS_FORCEWRITE);

    WCHAR path[MAX_PATH];
    HRESULT res = SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, path);
    if (res != S_OK)
    {
        return;
    }

    QString linksPath = QString::fromWCharArray(path);
    linksPath += QString::fromAscii("\\Links");
    QFileInfo info(linksPath);
    if (!info.isDir())
    {
        return;
    }

    QString linkPath = linksPath + QString::fromAscii("\\") + syncName + QString::fromAscii(".lnk");
    if (QFile(linkPath).exists())
    {
        return;
    }

    WCHAR wDescription[]=L"MEGAsync synchronized folder";
    linkPath = QDir::toNativeSeparators(linkPath);
    WCHAR *wLinkPath = (WCHAR *)linkPath.utf16();

    syncPath = QDir::toNativeSeparators(syncPath);
    WCHAR *wSyncPath = (WCHAR *)syncPath.utf16();

    QString exec = MegaApplication::applicationFilePath();
    exec = QDir::toNativeSeparators(exec);
    WCHAR *wExecPath = (WCHAR *)exec.utf16();
    res = CreateLink(wSyncPath, wLinkPath, wDescription, wExecPath);

    SHChangeNotify(SHCNE_CREATE, SHCNF_PATH | SHCNF_FLUSHNOWAIT, wLinkPath, NULL);

    WCHAR *wLinksPath = (WCHAR *)linksPath.utf16();
    SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATH | SHCNF_FLUSHNOWAIT, wLinksPath, NULL);
    SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATH | SHCNF_FLUSHNOWAIT, syncPath.utf16(), NULL);
}

void WindowsPlatform::syncFolderRemoved(QString syncPath, QString syncName, QString syncID)
{
    if (!syncPath.size())
    {
        return;
    }

    removeSyncFromLeftPane(syncPath, syncName, syncID);

    if (syncPath.startsWith(QString::fromAscii("\\\\?\\")))
    {
        syncPath = syncPath.mid(4);
    }

    SHFOLDERCUSTOMSETTINGS fcs = {0};
    fcs.dwSize = sizeof(SHFOLDERCUSTOMSETTINGS);
    fcs.dwMask = FCSM_ICONFILE | FCSM_INFOTIP;
    SHGetSetFolderCustomSettings(&fcs, (LPCWSTR)syncPath.utf16(), FCS_FORCEWRITE);

    WCHAR path[MAX_PATH];
    HRESULT res = SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, path);
    if (res != S_OK)
    {
        return;
    }

    QString linksPath = QString::fromWCharArray(path);
    linksPath += QString::fromAscii("\\Links");
    QFileInfo info(linksPath);
    if (!info.isDir())
    {
        return;
    }

    QString linkPath = linksPath + QString::fromAscii("\\") + syncName + QString::fromAscii(".lnk");

    QFile::remove(linkPath);

    WCHAR *wLinkPath = (WCHAR *)linkPath.utf16();
    SHChangeNotify(SHCNE_DELETE, SHCNF_PATH | SHCNF_FLUSHNOWAIT, wLinkPath, NULL);

    WCHAR *wLinksPath = (WCHAR *)linksPath.utf16();
    SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATH | SHCNF_FLUSHNOWAIT, wLinksPath, NULL);
    SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATH | SHCNF_FLUSHNOWAIT, syncPath.utf16(), NULL);
}

void WindowsPlatform::notifyRestartSyncFolders()
{

}

void WindowsPlatform::notifyAllSyncFoldersAdded()
{

}

void WindowsPlatform::notifyAllSyncFoldersRemoved()
{

}

QByteArray WindowsPlatform::encrypt(QByteArray data, QByteArray key)
{
    DATA_BLOB dataIn;
    DATA_BLOB dataOut;
    DATA_BLOB entropy;

    dataIn.pbData = (BYTE *)data.constData();
    dataIn.cbData = data.size();
    entropy.pbData = (BYTE *)key.constData();
    entropy.cbData = key.size();

    if (!CryptProtectData(&dataIn, L"", &entropy, NULL, NULL, 0, &dataOut))
    {
        return data;
    }

    QByteArray result((const char *)dataOut.pbData, dataOut.cbData);
    LocalFree(dataOut.pbData);
    return result;
}

QByteArray WindowsPlatform::decrypt(QByteArray data, QByteArray key)
{
    DATA_BLOB dataIn;
    DATA_BLOB dataOut;
    DATA_BLOB entropy;

    dataIn.pbData = (BYTE *)data.constData();
    dataIn.cbData = data.size();
    entropy.pbData = (BYTE *)key.constData();
    entropy.cbData = key.size();

    if (!CryptUnprotectData(&dataIn, NULL, &entropy, NULL, NULL, 0, &dataOut))
        return data;

    QByteArray result((const char *)dataOut.pbData, dataOut.cbData);
    LocalFree(dataOut.pbData);
    return result;
}

QByteArray WindowsPlatform::getLocalStorageKey()
{
    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
    {
        return QByteArray();
    }

    DWORD dwBufferSize = 0;
    GetTokenInformation(hToken, TokenUser, NULL, 0, &dwBufferSize);
    if (!dwBufferSize)
    {
        CloseHandle(hToken);
        return QByteArray();
    }

    PTOKEN_USER userToken = (PTOKEN_USER)new char[dwBufferSize];
    if (!GetTokenInformation(hToken, TokenUser, userToken, dwBufferSize, &dwBufferSize) ||
            !IsValidSid(userToken->User.Sid))
    {
        CloseHandle(hToken);
        delete userToken;
        return QByteArray();
    }

    DWORD dwLength =  GetLengthSid(userToken->User.Sid);
    QByteArray result((char *)userToken->User.Sid, dwLength);
    CloseHandle(hToken);
    delete userToken;
    return result;
}

QString WindowsPlatform::getDefaultOpenApp(QString extension)
{
    DWORD length = 0;
    ASSOCSTR type = ASSOCSTR_CONTENTTYPE;
    QString extensionWithDot = QString::fromUtf8(".") + extension;
    QString mimeType;

    HRESULT ret = AssocQueryString(0, type, (LPCWSTR)extensionWithDot.utf16(),
                                 NULL, NULL, &length);
    if (ret == S_FALSE)
    {
        WCHAR *buffer = new WCHAR[length];
        ret = AssocQueryString(0, type, (LPCWSTR)extensionWithDot.utf16(),
                               NULL, buffer, &length);
        if (ret == S_OK)
        {
            mimeType = QString::fromUtf16((ushort *)buffer);
        }
        delete [] buffer;
    }

    if (!mimeType.startsWith(QString::fromUtf8("video")))
    {
        return QString();
    }

    type = ASSOCSTR_EXECUTABLE;
    ret = AssocQueryString(0, type, (LPCWSTR)extensionWithDot.utf16(),
                                 NULL, NULL, &length);
    if (ret == S_FALSE)
    { 
        WCHAR *buffer = new WCHAR[length];
        ret = AssocQueryString(0, type, (LPCWSTR)extensionWithDot.utf16(),
                               NULL, buffer, &length);
        if (ret == S_OK)
        {
            QString result = QString::fromUtf16((ushort *)buffer);
            delete [] buffer;
            return result;
        }
        delete [] buffer;
    }

    WCHAR buff[MAX_PATH];
    if (SHGetFolderPath(0, CSIDL_PROGRAM_FILESX86, NULL, SHGFP_TYPE_CURRENT, buff) == S_OK)
    {
        QString path = QString::fromUtf16((ushort *)buff);
        path.append(QString::fromUtf8("\\Windows Media Player\\wmplayer.exe"));
        if (QFile(path).exists())
        {
            return path;
        }
    }
    return QString();
}

void WindowsPlatform::enableDialogBlur(QDialog *dialog)
{
    // this feature doesn't work well yet
    return;

    bool win10 = false;
    HWND hWnd = (HWND)dialog->winId();
    dialog->setAttribute(Qt::WA_TranslucentBackground, true);
    dialog->setAttribute(Qt::WA_NoSystemBackground, true);

    const HINSTANCE hModule = LoadLibrary(TEXT("user32.dll"));
    if (hModule)
    {
        struct ACCENTPOLICY
        {
            int nAccentState;
            int nFlags;
            int nColor;
            int nAnimationId;
        };
        struct WINCOMPATTRDATA
        {
            int nAttribute;
            PVOID pData;
            ULONG ulDataSize;
        };
        typedef BOOL(WINAPI*pSetWindowCompositionAttribute)(HWND, WINCOMPATTRDATA*);
        const pSetWindowCompositionAttribute SetWindowCompositionAttribute = (pSetWindowCompositionAttribute)GetProcAddress(hModule, "SetWindowCompositionAttribute");
        if (SetWindowCompositionAttribute)
        {
            ACCENTPOLICY policy = { 3, 0, 0xFFFFFFFF, 0 };
            WINCOMPATTRDATA data = { 19, &policy, sizeof(ACCENTPOLICY) };
            SetWindowCompositionAttribute(hWnd, &data);
            win10 = true;
        }
        FreeLibrary(hModule);
    }

#if QT_VERSION >= 0x050200
    if (!win10)
    {
        QtWin::setCompositionEnabled(true);
        QtWin::extendFrameIntoClientArea(dialog, -1, -1, -1, -1);
        QtWin::enableBlurBehindWindow(dialog);
    }
#endif
}

void WindowsPlatform::activateBackgroundWindow(QDialog *window)
{
    DWORD currentThreadId = GetCurrentThreadId();
    DWORD foregroundThreadId;
    HWND foregroundWindow;
    bool threadAttached = false;

    if (QGuiApplication::applicationState() != Qt::ApplicationActive
        && (foregroundWindow = GetForegroundWindow())
        && (foregroundThreadId = GetWindowThreadProcessId(foregroundWindow, NULL))
        && (foregroundThreadId != currentThreadId))
    {
        threadAttached = AttachThreadInput(foregroundThreadId, currentThreadId, TRUE);
    }
    window->showMinimized();
    window->showNormal();
    if (threadAttached)
    {
        AttachThreadInput(foregroundThreadId, currentThreadId, FALSE);
    }
}

LPTSTR WindowsPlatform::getCurrentSid()
{
    HANDLE hTok = NULL;
    LPBYTE buf = NULL;
    DWORD  dwSize = 0;
    LPTSTR stringSID = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hTok))
    {
        GetTokenInformation(hTok, TokenUser, NULL, 0, &dwSize);
        if (dwSize)
        {
            buf = (LPBYTE)LocalAlloc(LPTR, dwSize);
            if (GetTokenInformation(hTok, TokenUser, buf, dwSize, &dwSize))
            {
                ConvertSidToStringSid(((PTOKEN_USER)buf)->User.Sid, &stringSID);
            }
            LocalFree(buf);
        }
        CloseHandle(hTok);
    }
    return stringSID;
}

bool WindowsPlatform::registerUpdateJob()
{
    ITaskService *pService = NULL;
    ITaskFolder *pRootFolder = NULL;
    ITaskFolder *pMEGAFolder = NULL;
    ITaskDefinition *pTask = NULL;
    IRegistrationInfo *pRegInfo = NULL;
    IPrincipal *pPrincipal = NULL;
    ITaskSettings *pSettings = NULL;
    IIdleSettings *pIdleSettings = NULL;
    ITriggerCollection *pTriggerCollection = NULL;
    ITrigger *pTrigger = NULL;
    IDailyTrigger *pCalendarTrigger = NULL;
    IRepetitionPattern *pRepetitionPattern = NULL;
    IActionCollection *pActionCollection = NULL;
    IAction *pAction = NULL;
    IExecAction *pExecAction = NULL;
    IRegisteredTask *pRegisteredTask = NULL;
    time_t currentTime;
    struct tm* currentTimeInfo;
    WCHAR currentTimeString[128];
    _bstr_t taskBaseName = L"MEGAsync Update Task ";
    LPTSTR stringSID = NULL;
    bool success = false;

    stringSID = getCurrentSid();
    if (!stringSID)
    {
        MegaApi::log(MegaApi::LOG_LEVEL_ERROR, "Unable to get the current SID");
        return false;
    }

    time(&currentTime);
    currentTimeInfo = localtime(&currentTime);
    wcsftime(currentTimeString, 128,  L"%Y-%m-%dT%H:%M:%S", currentTimeInfo);
    _bstr_t taskName = taskBaseName + stringSID;
    _bstr_t userId = stringSID;
    LocalFree(stringSID);
    QString MEGAupdaterPath = QDir::toNativeSeparators(QDir(MegaApplication::applicationDirPath()).filePath(QString::fromUtf8("MEGAupdater.exe")));

    if (SUCCEEDED(CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, 0, NULL))
            && SUCCEEDED(CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, IID_ITaskService, (void**)&pService))
            && SUCCEEDED(pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t()))
            && SUCCEEDED(pService->GetFolder(_bstr_t( L"\\"), &pRootFolder)))
    {
        if (pRootFolder->CreateFolder(_bstr_t(L"MEGA"), _variant_t(L""), &pMEGAFolder) == 0x800700b7)
        {
            pRootFolder->GetFolder(_bstr_t(L"MEGA"), &pMEGAFolder);
        }

        if (pMEGAFolder
                && SUCCEEDED(pService->NewTask(0, &pTask))
                && SUCCEEDED(pTask->get_RegistrationInfo(&pRegInfo))
                && SUCCEEDED(pRegInfo->put_Author(_bstr_t(L"MEGA Limited")))
                && SUCCEEDED(pTask->get_Principal(&pPrincipal))
                && SUCCEEDED(pPrincipal->put_Id(_bstr_t(L"Principal1")))
                && SUCCEEDED(pPrincipal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN))
                && SUCCEEDED(pPrincipal->put_RunLevel(TASK_RUNLEVEL_LUA))
                && SUCCEEDED(pPrincipal->put_UserId(userId))
                && SUCCEEDED(pTask->get_Settings(&pSettings))
                && SUCCEEDED(pSettings->put_StartWhenAvailable(VARIANT_TRUE))
                && SUCCEEDED(pSettings->put_DisallowStartIfOnBatteries(VARIANT_FALSE))
                && SUCCEEDED(pSettings->get_IdleSettings(&pIdleSettings))
                && SUCCEEDED(pIdleSettings->put_StopOnIdleEnd(VARIANT_FALSE))
                && SUCCEEDED(pIdleSettings->put_RestartOnIdle(VARIANT_FALSE))
                && SUCCEEDED(pIdleSettings->put_WaitTimeout(_bstr_t()))
                && SUCCEEDED(pIdleSettings->put_IdleDuration(_bstr_t()))
                && SUCCEEDED(pTask->get_Triggers(&pTriggerCollection))
                && SUCCEEDED(pTriggerCollection->Create(TASK_TRIGGER_DAILY, &pTrigger))
                && SUCCEEDED(pTrigger->QueryInterface(IID_IDailyTrigger, (void**) &pCalendarTrigger))
                && SUCCEEDED(pCalendarTrigger->put_Id(_bstr_t(L"Trigger1")))
                && SUCCEEDED(pCalendarTrigger->put_DaysInterval(1))
                && SUCCEEDED(pCalendarTrigger->put_StartBoundary(_bstr_t(currentTimeString)))
                && SUCCEEDED(pCalendarTrigger->get_Repetition(&pRepetitionPattern))
                && SUCCEEDED(pRepetitionPattern->put_Duration(_bstr_t(L"P1D")))
                && SUCCEEDED(pRepetitionPattern->put_Interval(_bstr_t(L"PT2H")))
                && SUCCEEDED(pRepetitionPattern->put_StopAtDurationEnd(VARIANT_FALSE))
                && SUCCEEDED(pTask->get_Actions(&pActionCollection))
                && SUCCEEDED(pActionCollection->Create(TASK_ACTION_EXEC, &pAction))
                && SUCCEEDED(pAction->QueryInterface(IID_IExecAction, (void**)&pExecAction))
                && SUCCEEDED(pExecAction->put_Path(_bstr_t((wchar_t *)MEGAupdaterPath.utf16()))))
        {
            if (SUCCEEDED(pMEGAFolder->RegisterTaskDefinition(taskName, pTask,
                    TASK_CREATE_OR_UPDATE, _variant_t(), _variant_t(),
                    TASK_LOGON_INTERACTIVE_TOKEN, _variant_t(L""),
                    &pRegisteredTask)))
            {
                success = true;
                MegaApi::log(MegaApi::LOG_LEVEL_ERROR, "Update task registered OK");
            }
            else
            {
                MegaApi::log(MegaApi::LOG_LEVEL_ERROR, "Error registering update task");
            }
        }
        else
        {
            MegaApi::log(MegaApi::LOG_LEVEL_ERROR, "Error creating update task");
        }
    }
    else
    {
        MegaApi::log(MegaApi::LOG_LEVEL_ERROR, "Error getting root task folder");
    }

    if (pRegisteredTask)
    {
        pRegisteredTask->Release();
    }
    if (pTrigger)
    {
        pTrigger->Release();
    }
    if (pTriggerCollection)
    {
        pTriggerCollection->Release();
    }
    if (pIdleSettings)
    {
        pIdleSettings->Release();
    }
    if (pSettings)
    {
        pSettings->Release();
    }
    if (pPrincipal)
    {
        pPrincipal->Release();
    }
    if (pRegInfo)
    {
        pRegInfo->Release();
    }
    if (pCalendarTrigger)
    {
        pCalendarTrigger->Release();
    }
    if (pAction)
    {
        pAction->Release();
    }
    if (pActionCollection)
    {
        pActionCollection->Release();
    }
    if (pRepetitionPattern)
    {
        pRepetitionPattern->Release();
    }
    if (pExecAction)
    {
        pExecAction->Release();
    }
    if (pTask)
    {
        pTask->Release();
    }
    if (pMEGAFolder)
    {
        pMEGAFolder->Release();
    }
    if (pRootFolder)
    {
        pRootFolder->Release();
    }
    if (pService)
    {
        pService->Release();
    }

    return success;
}

void WindowsPlatform::execBackgroundWindow(QDialog *window)
{
    DWORD currentThreadId = GetCurrentThreadId();
    DWORD foregroundThreadId;
    HWND foregroundWindow;
    bool threadAttached = false;

    if (QGuiApplication::applicationState() != Qt::ApplicationActive
        && (foregroundWindow = GetForegroundWindow())
        && (foregroundThreadId = GetWindowThreadProcessId(foregroundWindow, NULL))
        && (foregroundThreadId != currentThreadId))
    {
        threadAttached = AttachThreadInput(foregroundThreadId, currentThreadId, TRUE);
    }
    window->exec();
    if (threadAttached)
    {
        AttachThreadInput(foregroundThreadId, currentThreadId, FALSE);
    }
}

void WindowsPlatform::uninstall()
{
    removeAllSyncsFromLeftPane();

    ITaskService *pService = NULL;
    ITaskFolder *pRootFolder = NULL;
    ITaskFolder *pMEGAFolder = NULL;
    _bstr_t taskBaseName = L"MEGAsync Update Task ";
    LPTSTR stringSID = NULL;

    stringSID = getCurrentSid();
    if (!stringSID)
    {
        return;
    }
    _bstr_t taskName = taskBaseName + stringSID;

    if (SUCCEEDED(CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, 0, NULL))
            && SUCCEEDED(CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, IID_ITaskService, (void**)&pService))
            && SUCCEEDED(pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t()))
            && SUCCEEDED(pService->GetFolder(_bstr_t( L"\\"), &pRootFolder)))
    {
        if (pRootFolder->CreateFolder(_bstr_t(L"MEGA"), _variant_t(L""), &pMEGAFolder) == 0x800700b7)
        {
            pRootFolder->GetFolder(_bstr_t(L"MEGA"), &pMEGAFolder);
        }

        if (pMEGAFolder)
        {
            pMEGAFolder->DeleteTask(taskName, 0);
            pMEGAFolder->Release();
        }
        pRootFolder->Release();
    }

    if (pService)
    {
        pService->Release();
    }
}

bool WindowsPlatform::shouldRunHttpServer()
{
    return true;
}

bool WindowsPlatform::shouldRunHttpsServer()
{
    return true;
}
