// SPDX-License-Identifier: MIT
#include "platform/files.hpp"
#include "platform/windows_files.hpp"
#include <shlobj.h>
#include <functional>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;
namespace platform=ph::exchange::platform;
namespace win=platform::windows;
namespace {
int checks=0;
void check(bool ok,const char* label) { ++checks; if(!ok) throw std::runtime_error(label); }
void rejected(const std::function<void()>& operation,const char* label) {
  bool failed=false; try { operation(); } catch(const std::exception&) { failed=true; }
  check(failed,label);
}
void mkdir(const fs::path& path) {
  win::PrivateSecurity security;
  check(::CreateDirectoryW(win::native_path(path).c_str(),security.get())!=0,"create isolated private fixture directory");
}
void write(const fs::path& path,const std::string& bytes) {
  win::PrivateSecurity security;
  win::Handle file(::CreateFileW(win::native_path(path).c_str(),GENERIC_WRITE,0,
      security.get(),CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
  if(!file) win::fail("create isolated fixture file");
  check(bool(file),"create isolated fixture file");
  DWORD count=0;
  check(::WriteFile(file.get(),bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)!=0
      && count==bytes.size(),"write isolated fixture");
}
}
int main() {
  fs::path fixture;
  try {
    wchar_t buffer[32768];
    const auto length=::GetTempPathW(32768,buffer);
    check(length>0 && length<32768,"temporary directory");
    fixture=win::local_path(fs::path(buffer)/(L"exchange-files-"+std::to_wstring(::GetCurrentProcessId())+L"-"+std::to_wstring(::GetTickCount64())));
    mkdir(fixture); const auto env=fixture/L"operation";mkdir(env);
    const auto keys=fixture/L"keys";mkdir(keys);
    const auto cfg=env/L"exchange.cfg";write(cfg,"{}");
    const auto key=keys/L"cl\u00e9-secret.key";
    const std::string value(64,'a');
    write(key,value);
    check(platform::read_key_file(key)==value,"read private Unicode key");
    write(key,value+"\n");check(platform::read_key_file(key)==value,"LF key");
    write(key,value+"\r\n");check(platform::read_key_file(key)==value,"CRLF key");
    write(key,value+"\r");rejected([&]{platform::read_key_file(key);},"lone CR rejected");
    write(key,std::string(64,'g'));rejected([&]{platform::read_key_file(key);},"nonhex key rejected");
    write(key,"short");rejected([&]{platform::read_key_file(key);},"short key rejected");
    write(key,value);
    check(platform::key_file_for_config(cfg,".",key.u8string())==key,"central config resolves key");
    check(platform::key_file_for_config(cfg,env.u8string(),key.u8string())==key,"absolute workspace");
    rejected([&]{platform::key_file_for_config(cfg,keys.u8string(),key.u8string());},"workspace mismatch");
    rejected([&]{platform::key_file_for_config(cfg,".","relative.key");},"relative key rejected");
    rejected([&]{platform::key_file_for_config(cfg,".","%TEMP%/secret.key");},"unsupported expansion");
    const auto inside=env/L"secret.key";write(inside,value);
    rejected([&]{platform::key_file_for_config(cfg,".",inside.u8string());},"key inside operation rejected");
    PWSTR profile=nullptr;
    check(SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Profile,0,nullptr,&profile)),"profile lookup");
    const fs::path profile_path(profile);::CoTaskMemFree(profile);
    const auto relative=key.lexically_relative(profile_path);
    if(!relative.empty() && *relative.begin()!=L"..") {
      check(platform::key_file_for_config(cfg,".","%USERPROFILE%/"+relative.generic_u8string())==key,
            "profile token resolution");
    }
    const auto input=env/L"donn\u00e9es.json";write(input,"{\"ok\":true}");
    check(platform::read_regular_file(input,16)=="{\"ok\":true}","Unicode input");
    rejected([&]{platform::read_regular_file(input,4);},"input limit");
    const auto report=env/L"report.html",pending=env/L"report.html.pending";
    platform::write_report(pending,report,"first");
    platform::write_report(pending,report,"second");
    check(platform::read_regular_file(report,64)=="second","explicit report replacement");
    write(pending,"keep");
    rejected([&]{platform::write_report(pending,report,"third");},"pending preserved");
    check(platform::read_regular_file(report,64)=="second","failed report preserves destination");
    fs::remove_all(fixture);
    std::cout<<"Windows configuration/key/report checks: "<<checks<<" passed.\n";
    return 0;
  } catch(const std::exception& error) {
    std::cerr<<"Windows file test failed: "<<error.what()<<"\n";
    // Failed isolated fixture remains for inspection. Never touches operational data.
    return 1;
  }
}
