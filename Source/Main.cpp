#include "App.h"
#include "DisplayCaptureOptions.h"
#include "HeadlessRuntime.h"
#include <shellapi.h>
#include <fstream>
#include <iomanip>
#include <sstream>

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int)
{
    RunOptions options; std::wstring monitor; std::string optionError; int count=0;
    auto arguments=CommandLineToArgvW(GetCommandLineW(),&count);
    // Establish test reporting before parsing, allocating sessions or opening UI.
    bool headlessPolicy=false;
    for(int i=1;i<count;++i) if(std::wstring_view(arguments[i])==L"--hidden-test" || std::wstring_view(arguments[i])==L"--smoke-test") headlessPolicy=true;
    if(headlessPolicy)
    {
        HeadlessRuntime::Configure();
        if(!HeadlessRuntime::Configured()) HeadlessRuntime::Fail("application test policy configuration failed");
        HeadlessRuntime::Probe(HeadlessRuntime::ProbeArgument(GetCommandLineW()));
    }
    for(int i=1;i<count;++i)
    {
        const std::wstring a=arguments[i];
        if(a==L"--software") options.software=true;
        else if(a==L"--hidden-test") options.hidden=true;
        else if(a==L"--smoke-test") options.smoke=true;
        else if((a==L"--monitor" || a==L"--test-monitor") && i+1<count) monitor=arguments[++i];
        else if(a==L"--log-directory" && i+1<count) options.session=arguments[++i];
        else if(a==L"--trace-display-seconds" && i+1<count) { const auto seconds=ParseDisplayCaptureSeconds(arguments[++i]); if(seconds) options.traceSeconds=*seconds; else optionError="Trace duration must be an integer from 1 through 60."; }
        else optionError="Unknown or incomplete command-line argument.";
    }
    LocalFree(arguments);
    const bool test=options.hidden || options.smoke;
    if(options.session.empty()) { wchar_t temp[MAX_PATH]{}; GetTempPathW(MAX_PATH,temp); options.session=std::filesystem::path(temp)/"Veehiicuul3"/"LogOutput"; }
    const auto now=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()); std::tm utc{}; gmtime_s(&utc,&now);
    std::ostringstream stamp; stamp<<std::put_time(&utc,"%Y-%m-%d_%H-%M-%S")<<'_'<<GetCurrentProcessId()<<'_'<<GetTickCount64()%1000;
    options.session/=stamp.str();
    try
    {
        std::filesystem::create_directories(options.session);
        if(!optionError.empty()) throw std::invalid_argument(optionError);
        if(!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) && GetLastError()!=ERROR_ACCESS_DENIED) throw std::runtime_error("Per-monitor DPI awareness failed.");
        if(test && monitor!=L"NE18NZ2") throw std::invalid_argument("Assistant tests require explicit --test-monitor NE18NZ2. No window was opened.");
        const auto displays=EnumerateDisplays(); const auto selected=monitor.empty() ? PrimaryDisplay(displays) : ResolveDisplay(displays,monitor);
        const auto presentation=DisplayPresentation(displays[selected]);
        ValidateDisplay(displays[selected],presentation);
        std::ofstream policy(options.session/"DisplayPolicy.txt");
        policy<<presentation.width<<"x"<<presentation.height<<" DPI "<<presentation.dpiX<<","<<presentation.dpiY<<" origin "<<presentation.originX<<","<<presentation.originY<<" centered offsets "<<presentation.Image().x<<","<<presentation.Image().y;
        if(options.smoke && !InputDesktopAvailable()) { std::ofstream(options.session/"Skipped.txt")<<"Input desktop locked/unavailable. Visible checks pending."; return 77; }
        App app(instance,displays[selected],presentation,options); return app.Run();
    }
    catch(const std::exception& e)
    {
        std::ofstream(options.session/"Failure.txt")<<e.what();
        OutputDebugStringA(e.what());
        if(!test) MessageBoxA(nullptr,e.what(),"Veehiicuul3 startup error",MB_OK|MB_ICONERROR);
        return 1;
    }
}
