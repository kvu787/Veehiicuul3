#include "Display.h"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <wbemidl.h>
#include <wrl/client.h>
#include <cwctype>

namespace
{
std::wstring MonitorInstance(std::wstring identity)
{
    const auto start=identity.find(L"DISPLAY");
    if(start==std::wstring::npos) return {};
    identity=identity.substr(start);
    std::replace(identity.begin(),identity.end(),L'#',L'\\');
    const auto suffix=identity.find(L"\\{");
    if(suffix!=std::wstring::npos) identity.resize(suffix);
    if(identity.ends_with(L"_0")) identity.resize(identity.size()-2);
    for(auto& c : identity) c=static_cast<wchar_t>(std::towupper(c));
    return identity;
}
std::vector<std::pair<std::wstring,std::wstring>> WmiNames()
{
    using Microsoft::WRL::ComPtr;
    std::vector<std::pair<std::wstring,std::wstring>> names;
    const auto initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(initialized) && initialized!=RPC_E_CHANGED_MODE) return names;
    {
        ComPtr<IWbemLocator> locator; ComPtr<IWbemServices> service; ComPtr<IEnumWbemClassObject> results;
        BSTR space=SysAllocString(L"ROOT\\WMI"), language=SysAllocString(L"WQL"), query=SysAllocString(L"SELECT InstanceName, UserFriendlyName FROM WmiMonitorID WHERE Active=TRUE");
        if(SUCCEEDED(CoCreateInstance(CLSID_WbemLocator,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&locator))) &&
           SUCCEEDED(locator->ConnectServer(space,nullptr,nullptr,nullptr,0,nullptr,nullptr,&service)))
        {
            (void)CoSetProxyBlanket(service.Get(),RPC_C_AUTHN_WINNT,RPC_C_AUTHZ_NONE,nullptr,RPC_C_AUTHN_LEVEL_CALL,RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE);
            if(SUCCEEDED(service->ExecQuery(language,query,WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,nullptr,&results)))
            {
                for(;;)
                {
                    ComPtr<IWbemClassObject> item; ULONG count=0;
                    if(FAILED(results->Next(2000,1,&item,&count)) || count!=1) break;
                    VARIANT identity{}, friendly{}; VariantInit(&identity); VariantInit(&friendly);
                    if(SUCCEEDED(item->Get(L"InstanceName",0,&identity,nullptr,nullptr)) && identity.vt==VT_BSTR &&
                       SUCCEEDED(item->Get(L"UserFriendlyName",0,&friendly,nullptr,nullptr)) && (friendly.vt&VT_ARRAY) && friendly.parray)
                    {
                        LONG begin=0,end=-1; SafeArrayGetLBound(friendly.parray,1,&begin); SafeArrayGetUBound(friendly.parray,1,&end);
                        std::wstring name;
                        for(LONG index=begin;index<=end;++index)
                        {
                            LONG value=0;
                            if(SUCCEEDED(SafeArrayGetElement(friendly.parray,&index,&value)) && value>0 && value<65536) name.push_back(static_cast<wchar_t>(value));
                        }
                        if(!name.empty()) names.emplace_back(MonitorInstance(identity.bstrVal),std::move(name));
                    }
                    VariantClear(&identity); VariantClear(&friendly);
                }
            }
        }
        SysFreeString(space); SysFreeString(language); SysFreeString(query);
    }
    if(SUCCEEDED(initialized)) CoUninitialize();
    return names;
}
std::wstring EdidName(std::wstring identity)
{
    // EnumDisplayDevices returns the monitor interface path, which maps to its
    // exact Enum\\DISPLAY registry instance. The EDID name is not a resolution guess.
    const auto first=identity.find(L"DISPLAY#");
    if(first==std::wstring::npos) return {};
    identity=identity.substr(first);
    const auto end=identity.find(L"#",identity.find(L"#",identity.find(L"#")+1)+1);
    if(end==std::wstring::npos) return {};
    identity=identity.substr(0,end);
    std::replace(identity.begin(),identity.end(),L'#',L'\\');
    const auto key=L"SYSTEM\\CurrentControlSet\\Enum\\"+identity+L"\\Device Parameters";
    std::array<unsigned char,1024> data{}; DWORD bytes=static_cast<DWORD>(data.size());
    if(RegGetValueW(HKEY_LOCAL_MACHINE,key.c_str(),L"EDID",RRF_RT_REG_BINARY,nullptr,data.data(),&bytes)!=ERROR_SUCCESS || bytes<128) return {};
    for(size_t offset=54;offset+18<=126;offset+=18)
    {
        if(data[offset] || data[offset+1] || data[offset+2] || data[offset+3]!=0xfc) continue;
        std::wstring name;
        for(size_t i=offset+5;i<offset+18 && data[i]!=10 && data[i]!=0;++i) name.push_back(static_cast<wchar_t>(data[i]));
        while(!name.empty() && name.back()==L' ') name.pop_back();
        return name;
    }
    return {};
}
BOOL CALLBACK MonitorCallback(HMONITOR monitor,HDC,LPRECT,LPARAM argument)
{
    MONITORINFOEXW info{}; info.cbSize=sizeof(info);
    if(!GetMonitorInfoW(monitor,&info)) return TRUE;
    Display display;
    display.device=info.szDevice; display.bounds=info.rcMonitor; display.work=info.rcWork; display.primary=(info.dwFlags&MONITORINFOF_PRIMARY)!=0;
    DISPLAY_DEVICEW child{}; child.cb=sizeof(child);
    if(EnumDisplayDevicesW(info.szDevice,0,&child,EDD_GET_DEVICE_INTERFACE_NAME))
    {
        display.operatingSystemName=child.DeviceString; display.identity=child.DeviceID; display.name=EdidName(display.identity);
    }
    if(display.name.empty()) display.name=display.operatingSystemName.empty() ? display.device : display.operatingSystemName;
    reinterpret_cast<std::vector<Display>*>(argument)->push_back(std::move(display));
    return TRUE;
}
}
std::vector<Display> EnumerateDisplays()
{
    std::vector<Display> result;
    if(!EnumDisplayMonitors(nullptr,nullptr,MonitorCallback,reinterpret_cast<LPARAM>(&result)) || result.empty()) throw std::runtime_error("No connected display could be enumerated.");
    const auto names=WmiNames();
    for(auto& display : result)
        for(const auto& [identity,name] : names) if(identity==MonitorInstance(display.identity)) { display.name=name; break; }
    return result;
}
size_t ResolveDisplay(const std::vector<Display>& displays,const std::wstring& name)
{
    size_t found=displays.size();
    for(size_t i=0;i<displays.size();++i)
        if(_wcsicmp(name.c_str(),displays[i].name.c_str())==0 || _wcsicmp(name.c_str(),displays[i].device.c_str())==0 || _wcsicmp(name.c_str(),displays[i].identity.c_str())==0)
        {
            if(found!=displays.size()) throw std::runtime_error("More than one display has that name. Use its exact DISPLAY device name.");
            found=i;
        }
    if(found==displays.size()) throw std::runtime_error("The requested monitor is not connected. No editor window was opened.");
    return found;
}
size_t PrimaryDisplay(const std::vector<Display>& displays)
{
    size_t found=displays.size();
    for(size_t i=0;i<displays.size();++i) if(displays[i].primary)
    { if(found!=displays.size()) throw std::runtime_error("Windows reported more than one main display."); found=i; }
    if(found==displays.size()) throw std::runtime_error("Windows did not identify its main display.");
    return found;
}
