#pragma once
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string_view>
#include <thread>
#include <vector>
#ifdef _DEBUG
#include <crtdbg.h>
#endif

// Process-local CRT policy for automated tests only. No OS/WER/security settings.
namespace HeadlessRuntime
{
    inline constexpr int FailureExit=31;
    [[noreturn]] inline void Fail(const char* message) noexcept
    {
        std::fputs("Headless CRT failure: ",stderr);
        if(message) std::fputs(message,stderr);
        std::fputc('\n',stderr); std::fflush(stderr); std::_Exit(FailureExit);
    }
    inline void InvalidParameter(const wchar_t*,const wchar_t*,const wchar_t*,unsigned,uintptr_t) { Fail("invalid parameter"); }
#ifdef _DEBUG
    inline int Report(int type,char* message,int*)
    {
        if(type==_CRT_ASSERT || type==_CRT_ERROR) Fail(message);
        return 0;
    }
    inline int ReportWide(int type,wchar_t*,int*)
    {
        if(type==_CRT_ASSERT || type==_CRT_ERROR) Fail("wide assertion/error report");
        return 0;
    }
#endif
    inline void Configure() noexcept
    {
        _set_error_mode(_OUT_TO_STDERR);
        _set_abort_behavior(0,_WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        _set_invalid_parameter_handler(InvalidParameter);
        std::set_terminate([] { Fail("uncaught exception/terminate"); });
#ifdef _DEBUG
        for(int type:{_CRT_WARN,_CRT_ERROR,_CRT_ASSERT})
        {
            _CrtSetReportMode(type,_CRTDBG_MODE_FILE);
            _CrtSetReportFile(type,_CRTDBG_FILE_STDERR);
        }
        _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL,Report);
        _CrtSetReportHookW2(_CRT_RPTHOOK_INSTALL,ReportWide);
#endif
    }
    inline bool Configured() noexcept
    {
        if(_get_invalid_parameter_handler()!=InvalidParameter ||
           (_set_abort_behavior(0,0)&(_WRITE_ABORT_MSG | _CALL_REPORTFAULT))!=0 ||
           _set_error_mode(_REPORT_ERRMODE)!=_OUT_TO_STDERR) return false;
#ifdef _DEBUG
        for(int type:{_CRT_WARN,_CRT_ERROR,_CRT_ASSERT}) if(_CrtSetReportMode(type,_CRTDBG_REPORT_MODE)!=_CRTDBG_MODE_FILE) return false;
#endif
        return true;
    }
    // Private test switch is detected before an entry point can run its workflow.
    // All generated calls use an unquoted ASCII token after the executable name.
    inline std::wstring_view ProbeArgument(std::wstring_view command) noexcept
    {
        constexpr std::wstring_view prefix=L"--headless-crt-probe=";
        auto position=command.find(prefix);
        while(position!=std::wstring_view::npos)
        {
            if(position && (command[position-1]==L' ' || command[position-1]==L'\t'))
            {
                const auto start=position+prefix.size(); const auto end=command.find_first_of(L" \t",start);
                return command.substr(start,end==std::wstring_view::npos ? end : end-start);
            }
            position=command.find(prefix,position+prefix.size());
        }
        return {};
    }
    inline void Probe(std::wstring_view kind)
    {
        if(kind.empty()) return;
        if(!Configured()) Fail("reporting policy was not installed");
        bool known=kind==L"invalid-parameter" || kind==L"worker-invalid-parameter" || kind==L"terminate" || kind==L"abort";
#ifdef _DEBUG
        known=known || kind==L"assert" || kind==L"wide-assert" || kind==L"error" || kind==L"checked-iterator";
#endif
        if(!known) { std::fputs("Unknown headless CRT probe.\n",stderr); std::_Exit(32); }
        std::fputs("Headless CRT probe: ",stderr);
        for(const auto character:kind) std::fputc(static_cast<char>(character),stderr);
        std::fputc('\n',stderr); std::fflush(stderr);
        if(kind==L"invalid-parameter") _invalid_parameter_noinfo_noreturn();
        if(kind==L"worker-invalid-parameter") { std::thread worker([] { _invalid_parameter_noinfo_noreturn(); }); worker.join(); }
        if(kind==L"terminate") std::terminate();
        if(kind==L"abort") { std::fputs("Headless CRT abort probe.\n",stderr); std::fflush(stderr); std::abort(); }
#ifdef _DEBUG
        if(kind==L"assert") _CrtDbgReport(_CRT_ASSERT,"HeadlessProbe",0,nullptr,"Controlled assertion probe.");
        if(kind==L"wide-assert") _CrtDbgReportW(_CRT_ASSERT,L"HeadlessProbe",0,nullptr,L"Controlled assertion probe.");
        if(kind==L"error") _CrtDbgReport(_CRT_ERROR,"HeadlessProbe",0,nullptr,"Controlled error probe.");
        if(kind==L"checked-iterator") { std::vector<int> values(2); (void)(values.begin()+3); }
#endif
        std::fputs("Headless CRT probe unexpectedly continued.\n",stderr); std::_Exit(32);
    }
}
