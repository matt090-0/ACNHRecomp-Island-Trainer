#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <cwchar>
#include <vector>
#include <stdexcept>

namespace fs=std::filesystem;
namespace {
void notice(const std::wstring& message){MessageBoxW(nullptr,message.c_str(),L"ACNHRecomp Island Trainer",MB_OK|MB_ICONINFORMATION);}
std::wstring quote(const std::wstring& text){
    std::wstring out=L"\"";unsigned slashes=0;
    for(wchar_t c:text){if(c==L'\\'){++slashes;continue;}if(c==L'\"')out.append(slashes*2+1,L'\\');else out.append(slashes,L'\\');slashes=0;out+=c;}
    out.append(slashes*2,L'\\');return out+L'\"';
}
std::wstring wide(const std::string& value){
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0);
    if(!n)return {};std::wstring out(size_t(n),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),out.data(),n);return out;
}
std::string utf8(const std::wstring& value){
    const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0,nullptr,nullptr);
    if(!n)return {};std::string out(size_t(n),'\0');
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),int(value.size()),out.data(),n,nullptr,nullptr);return out;
}
bool valid_game(const fs::path& game){
    if(!fs::is_regular_file(game/L"game/windows-x64/acnh_game.dll") || !fs::is_regular_file(game/L"romfs/Bcsv/ItemParam.bcsv"))return false;
    std::ifstream in(game/L"game.ini");std::string text((std::istreambuf_iterator<char>(in)),{});
    return text.find("FCD2BB238ABE99E925B4E452DF1F41F1")!=std::string::npos;
}
fs::path choose_game(){
    IFileOpenDialog* dialog=nullptr;
    if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))return {};
    DWORD options=0;dialog->GetOptions(&options);dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"Choose the game folder prepared by ACNHRecomp-Importer (3.0.2)");
    fs::path out;
    if(SUCCEEDED(dialog->Show(nullptr))){IShellItem* item=nullptr;if(SUCCEEDED(dialog->GetResult(&item))){
        PWSTR path=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))){out=path;CoTaskMemFree(path);}item->Release();}}
    dialog->Release();return out;
}
bool running(){
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)return true;
    PROCESSENTRY32W entry{};entry.dwSize=sizeof entry;bool found=false;
    if(Process32FirstW(snapshot,&entry))do{
        if(!_wcsicmp(entry.szExeFile,L"acnh.exe") || !_wcsicmp(entry.szExeFile,L"acnh-trainer.exe") || !_wcsnicmp(entry.szExeFile,L"acnh-bells-",11)){found=true;break;}
    }while(Process32NextW(snapshot,&entry));CloseHandle(snapshot);return found;
}
bool run_child(const fs::path& exe,std::wstring arguments,const fs::path& folder,const fs::path& log,bool wait){
    SECURITY_ATTRIBUTES security{sizeof security,nullptr,TRUE};
    HANDLE output=CreateFileW(log.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    HANDLE input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(output==INVALID_HANDLE_VALUE||input==INVALID_HANDLE_VALUE){if(output!=INVALID_HANDLE_VALUE)CloseHandle(output);if(input!=INVALID_HANDLE_VALUE)CloseHandle(input);return false;}
    STARTUPINFOW start{};start.cb=sizeof start;start.dwFlags=STARTF_USESTDHANDLES;start.hStdInput=input;start.hStdOutput=output;start.hStdError=output;
    PROCESS_INFORMATION process{};std::wstring command=L"\""+exe.wstring()+L"\" "+arguments;
    const BOOL ok=CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,folder.c_str(),&start,&process);
    CloseHandle(input);CloseHandle(output);if(!ok)return false;CloseHandle(process.hThread);
    DWORD result=0;if(wait){WaitForSingleObject(process.hProcess,INFINITE);GetExitCodeProcess(process.hProcess,&result);}CloseHandle(process.hProcess);return result==0;
}
void backup(const fs::path& folder,const fs::path& game){
    SYSTEMTIME t{};GetLocalTime(&t);wchar_t stamp[80]{};
    swprintf_s(stamp,L"%04u-%02u-%02u_%02u-%02u-%02u_%lu",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,GetCurrentProcessId());
    const auto destination=folder/L"save-backups"/stamp;fs::create_directories(destination);
    if(fs::is_directory(game/L"user/device_save"))fs::copy(game/L"user/device_save",destination/L"device_save",fs::copy_options::recursive);
    if(fs::is_regular_file(game/L"config.ini"))fs::copy_file(game/L"config.ini",destination/L"config.ini");
}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    const HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
    try{
        wchar_t module[32768]{};GetModuleFileNameW(nullptr,module,32768);const fs::path folder=fs::path(module).parent_path();
        int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);bool prepare=false,change=(GetKeyState(VK_SHIFT)&0x8000)!=0;fs::path game;
        for(int i=1;i<argc;++i){if(!_wcsicmp(argv[i],L"--game-folder")&&i+1<argc)game=argv[++i];else if(!_wcsicmp(argv[i],L"--prepare-only"))prepare=true;else if(!_wcsicmp(argv[i],L"--choose-game"))change=true;}
        LocalFree(argv);
        if(!prepare&&running()){notice(L"Animal Crossing is running. Save and close it, then start the trainer again.");return 1;}
        const auto config=folder/L"trainer-game-folder.txt";
        if(game.empty()&&!change){std::ifstream in(config,std::ios::binary);std::string line;std::getline(in,line);if(!line.empty())game=wide(line);}
        if(game.empty()||!valid_game(game)){
            if(prepare)return 2;
            game=choose_game();if(game.empty())return 2;
        }
        if(!valid_game(game)){notice(L"Choose an ACNH 3.0.2 game folder prepared for Windows by ACNHRecomp-Importer. It contains game.ini, romfs and game/windows-x64/acnh_game.dll.");return 2;}
        game=fs::canonical(game);
        // Build private names/metadata locally. No game-derived catalog is bundled with the trainer.
        if(!fs::is_regular_file(game/L"mod-menu-items.tsv")){
            const auto generator=folder/L"Build Item Catalog.exe";
            if(!fs::is_regular_file(generator)||!run_child(generator,quote(game.wstring())+L" "+quote((game/L"mod-menu-items.tsv").wstring()),folder,folder/L"catalog-build.log",true)){
                notice(L"The item catalog could not be built. See catalog-build.log beside the launcher. The game has not started.");return 3;
            }
        }
        {std::ofstream out(config,std::ios::binary);out<<utf8(game.wstring())<<'\n';if(!out)throw std::runtime_error("config");}
        if(prepare){if(SUCCEEDED(initialized))CoUninitialize();return 0;}
        const auto player=folder/L"acnh-trainer.exe";if(!fs::is_regular_file(player)){notice(L"acnh-trainer.exe is missing. Extract the whole trainer ZIP to a writable folder.");return 4;}
        HANDLE gate=CreateMutexW(nullptr,FALSE,L"Local\\ACNHRecompIslandTrainerLauncher");
        if(!gate||GetLastError()==ERROR_ALREADY_EXISTS){if(gate)CloseHandle(gate);notice(L"Another trainer launcher is starting. Wait for it to finish.");return 1;}
        if(running()){CloseHandle(gate);notice(L"Animal Crossing is already running. Save and close it first.");return 1;}
        backup(folder,game);
        SetEnvironmentVariableW(L"NX_DATA_ROOT",game.c_str());
        for(const auto* key:{L"NX_HEADLESS",L"NX_UI_OPEN",L"NX_UI_PAGE",L"NX_BELLS_TEST_ENABLE",L"NX_DUMP_EVERY_S",L"NX_LIVE_FRAME"})SetEnvironmentVariableW(key,nullptr);
        const bool ok=run_child(player,L"",folder,folder/L"trainer-run.log",false);CloseHandle(gate);
        if(!ok){notice(L"The trainer could not start. Extract the complete ZIP and check trainer-run.log.");return 5;}
    }catch(const std::exception&){notice(L"Setup or the save backup could not complete. Use a writable trainer folder and game folder. The game has not started.");return 6;}
    if(SUCCEEDED(initialized))CoUninitialize();return 0;
}
