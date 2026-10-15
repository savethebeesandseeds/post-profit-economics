// SPDX-License-Identifier: MIT
#include "files.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include "windows_files.hpp"
#include <shlobj.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace ph::exchange::platform {
namespace fs=std::filesystem;
namespace {
void require(bool condition,const char* message) {
  if (!condition) throw std::invalid_argument(message);
}
#ifdef _WIN32
std::string read_handle(HANDLE handle,std::size_t limit) {
  std::string bytes;
  char buffer[4096];
  for (;;) {
    DWORD read=0;
    require(::ReadFile(handle,buffer,sizeof buffer,&read,nullptr)!=0,"cannot read selected file");
    if (!read) break;
    require(bytes.size()+read<=limit,"file exceeds supported size");
    bytes.append(buffer,read);
  }
  return bytes;
}
bool within(const fs::path& child,const fs::path& parent) {
  auto c=child.begin(), p=parent.begin();
  for (;p!=parent.end();++p,++c)
    if(c==child.end() || ::CompareStringOrdinal(c->c_str(),-1,p->c_str(),-1,TRUE)!=CSTR_EQUAL)
      return false;
  return true;
}
#endif
}
std::string read_regular_file(const fs::path& path,std::size_t limit) {
#ifdef _WIN32
  const windows::PinnedPath pinned(path);
  const auto file=windows::open_regular(pinned,GENERIC_READ,FILE_SHARE_READ,limit,false);
  return read_handle(file.get(),limit);
#else
  require(fs::is_regular_file(fs::symlink_status(path)),"expected a regular input file");
  require(fs::file_size(path)<=limit,"file exceeds supported size");
  std::ifstream in(path,std::ios::binary);
  require(bool(in),"cannot open selected input file");
  std::string bytes;
  char buffer[4096];
  while (in) {
    in.read(buffer,sizeof buffer);
    bytes.append(buffer,static_cast<std::size_t>(in.gcount()));
    require(bytes.size()<=limit,"file grew beyond supported size");
  }
  require(in.eof(),"cannot finish reading selected input file");
  return bytes;
#endif
}
void validate_existing_path(const fs::path& path) {
#ifdef _WIN32
  if(fs::is_directory(path)) {
    // Pin/check the directory itself as the parent of an unused basename.
    const windows::PinnedPath pinned(path/L"exchange-path-check");
  } else {
    const windows::PinnedPath pinned(path);
    const auto file=windows::open_regular(pinned,FILE_READ_ATTRIBUTES,FILE_SHARE_READ,
                                          2*1024*1024,false);
    (void)file;
  }
#else
  (void)path; // POSIX paths are checked at their contained-file boundaries.
#endif
}
std::string read_key_file(const fs::path& path) {
  std::string value;
#ifdef _WIN32
  const windows::PinnedPath pinned(path,true);
  const auto file=windows::open_regular(pinned,GENERIC_READ,FILE_SHARE_READ,66,true);
  value=read_handle(file.get(),66);
#else
  const int fd=::open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
  require(fd>=0,"cannot open encryption key file");
  struct Close { int fd; ~Close(){ ::close(fd); } } close{fd};
  struct stat info{};
  require(::fstat(fd,&info)==0 && S_ISREG(info.st_mode) && info.st_size>=64 && info.st_size<=66,
          "encryption key must be a regular file containing 64 hexadecimal characters");
  char buffer[67];
  for (;;) {
    const auto count=::read(fd,buffer,sizeof buffer);
    if(count<0 && errno==EINTR) continue;
    require(count>=0,"cannot read encryption key file");
    if(!count) break;
    value.append(buffer,static_cast<std::size_t>(count));
    require(value.size()<=66,"invalid encryption key length");
  }
#endif
  if(!value.empty() && value.back()=='\n') {
    value.pop_back();
    if(!value.empty() && value.back()=='\r') value.pop_back();
  }
  require(value.size()==64 && std::all_of(value.begin(),value.end(),[](char c) {
    return (c>='0' && c<='9') || (c>='a' && c<='f') || (c>='A' && c<='F');
  }),"encryption key must contain 64 hexadecimal characters");
  return value;
}
fs::path key_file_for_config(const fs::path& config,const std::string& workspace,
                            const std::string& configured_key) {
#ifdef _WIN32
  const auto cfg=windows::local_path(config);
  require(cfg.filename()==L"exchange.cfg","configuration must be named exchange.cfg");
  validate_existing_path(cfg);
  auto operation=fs::u8path(workspace);
  if(operation.is_relative()) operation=cfg.parent_path()/operation;
  operation=windows::local_path(operation);
  validate_existing_path(operation);
  require(fs::equivalent(operation,cfg.parent_path()),
          "configuration must be directly inside its selected workspace");
  auto key_text=configured_key;
  constexpr char profile_token[]="%USERPROFILE%";
  fs::path key;
  if(key_text.rfind(profile_token,0)==0) {
    const auto tail=key_text.substr(sizeof(profile_token)-1);
    require(!tail.empty() && (tail.front()=='/' || tail.front()=='\\'),
            "USERPROFILE must be followed by a path separator");
    PWSTR profile=nullptr;
    require(SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Profile,0,nullptr,&profile)),
            "cannot resolve Windows user profile");
    key=fs::path(profile)/fs::u8path(tail.substr(1));
    ::CoTaskMemFree(profile);
    require(tail.find('%')==std::string::npos,"unsupported key-file variable");
  } else {
    require(key_text.find('%')==std::string::npos,"unsupported key-file variable");
    key=fs::u8path(key_text);
    require(key.is_absolute(),"key file must be absolute or begin with USERPROFILE");
  }
  key=windows::local_path(key);
  // Parent handles and a one-link regular file prevent namespace/alias escapes.
  validate_existing_path(key);
  const auto canonical_key=fs::canonical(key), canonical_root=fs::canonical(operation);
  require(!within(canonical_key,canonical_root),"keep the key outside the operation workspace");
  // Directory identity also catches short-name/case aliases of the workspace.
  for(auto ancestor=key.parent_path(); !ancestor.empty(); ancestor=ancestor.parent_path()) {
    require(!fs::equivalent(ancestor,operation),"keep the key outside the operation workspace");
    if(ancestor==ancestor.root_path()) break;
  }
  return key;
#else
  (void)config;(void)workspace;(void)configured_key;
  throw std::invalid_argument("host key-path resolution is only available on Windows; supply the mounted key path");
#endif
}
void write_report(const fs::path& temporary,const fs::path& destination,const std::string& page) {
#ifdef _WIN32
  // Explicit plaintext report export only; never used to publish database files.
  const windows::PinnedPath pending(temporary), target(destination);
  windows::Handle file(::CreateFileW(windows::native_path(pending.path()).c_str(),GENERIC_WRITE|READ_CONTROL,
      0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
  require(bool(file),"pending report already exists or cannot be created; preserved for inspection");
  windows::check_regular(file.get(),page.size(),false);
  DWORD written=0;
  require(page.size()<=MAXDWORD && ::WriteFile(file.get(),page.data(),static_cast<DWORD>(page.size()),&written,nullptr)
          && written==page.size() && ::FlushFileBuffers(file.get()),
          "report write failed; pending file preserved");
  file.reset();
  const auto attr=::GetFileAttributesW(windows::native_path(target.path()).c_str());
  require(attr==INVALID_FILE_ATTRIBUTES || !(attr&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)),
          "report destination must be an ordinary file");
  require(::MoveFileExW(windows::native_path(pending.path()).c_str(),windows::native_path(target.path()).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0,
          "report publication failed; pending file preserved");
#else
  FILE* out=std::fopen(temporary.c_str(),"wbx");
  require(out!=nullptr,"pending report already exists or cannot be created; preserved for inspection");
  const bool written=std::fwrite(page.data(),1,page.size(),out)==page.size();
  const bool closed=std::fclose(out)==0;
  require(written&&closed,"report write failed; pending file preserved");
  fs::rename(temporary,destination);
#endif
}
}
