// MBROLA NG - client of the synthesis process
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
#include "engine/synthproc.h"

#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace mbng {

#ifdef _WIN32
static std::wstring widen(const std::string& s) {
  if (s.empty()) return std::wstring();
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring w(n > 0 ? n - 1 : 0, L'\0');
  if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
  return w;
}

bool SynthProcess::start(const std::string& exe, const std::string& database, std::string& error) {
  stop();
  SECURITY_ATTRIBUTES sa = {sizeof sa, nullptr, TRUE};
  HANDLE child_in_r = nullptr, child_in_w = nullptr, child_out_r = nullptr, child_out_w = nullptr;
  if (!CreatePipe(&child_in_r, &child_in_w, &sa, 0) || !CreatePipe(&child_out_r, &child_out_w, &sa, 1 << 16)) {
    error = "cannot create pipes";
    return false;
  }
  SetHandleInformation(child_in_w, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(child_out_r, HANDLE_FLAG_INHERIT, 0);
  HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

  STARTUPINFOW si = {};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = child_in_r;
  si.hStdOutput = child_out_w;
  si.hStdError = nul;
  // Only these three handles are inherited.
  HANDLE inherit[3] = {child_in_r, child_out_w, nul};
  SIZE_T attr_size = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
  std::vector<char> attr_buf(attr_size);
  auto attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_buf.data());
  bool attr_ok = InitializeProcThreadAttributeList(attrs, 1, 0, &attr_size) &&
                 UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit,
                                           sizeof(HANDLE) * (nul != INVALID_HANDLE_VALUE ? 3 : 2), nullptr, nullptr);
  STARTUPINFOEXW six = {};
  six.StartupInfo = si;
  six.StartupInfo.cb = sizeof six;
  six.lpAttributeList = attr_ok ? attrs : nullptr;

  std::wstring wexe = widen(exe);
  std::wstring cmd = L"\"" + wexe + L"\" \"" + widen(database) + L"\"";
  PROCESS_INFORMATION pi = {};
  BOOL ok = CreateProcessW(wexe.c_str(), &cmd[0], nullptr, nullptr, TRUE,
                           CREATE_NO_WINDOW | (attr_ok ? EXTENDED_STARTUPINFO_PRESENT : 0), nullptr, nullptr,
                           reinterpret_cast<STARTUPINFOW*>(&six), &pi);
  if (attr_ok) DeleteProcThreadAttributeList(attrs);
  CloseHandle(child_in_r);
  CloseHandle(child_out_w);
  if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
  if (!ok) {
    CloseHandle(child_in_w);
    CloseHandle(child_out_r);
    error = "cannot start the synthesizer: " + exe;
    return false;
  }
  CloseHandle(pi.hThread);
  proc_ = pi.hProcess;
  in_ = child_in_w;
  out_ = child_out_r;
  running_ = true;
  char magic[4];
  uint32_t rate = 0;
  if (!read_all(magic, 4)) {
    stop();
    error = "the synthesizer did not start (voice database missing or invalid?): " + database;
    return false;
  }
  if (std::memcmp(magic, "MBNG", 4) != 0) {
    // error frame: 0xFFFFFFFF, len, message
    uint32_t n = 0;
    std::string msg;
    if (read_all(&n, 4) && n < 4096) {
      msg.resize(n);
      read_all(&msg[0], n);
    }
    stop();
    error = "voice database error: " + msg;
    return false;
  }
  if (!read_all(&rate, 4) || rate < 4000 || rate > 96000) {
    stop();
    error = "bad synthesizer greeting";
    return false;
  }
  rate_ = static_cast<int>(rate);
  return true;
}

void SynthProcess::stop() {
  if (in_) { CloseHandle(in_); in_ = nullptr; }
  if (proc_) {
    if (WaitForSingleObject(proc_, 200) != WAIT_OBJECT_0) TerminateProcess(proc_, 1);
    CloseHandle(proc_);
    proc_ = nullptr;
  }
  if (out_) { CloseHandle(out_); out_ = nullptr; }
  running_ = false;
}

bool SynthProcess::read_all(void* p, size_t n) {
  char* b = static_cast<char*>(p);
  while (n > 0) {
    DWORD got = 0;
    if (!ReadFile(out_, b, static_cast<DWORD>(n), &got, nullptr) || got == 0) return false;
    b += got;
    n -= got;
  }
  return true;
}

bool SynthProcess::write_all(const void* p, size_t n) {
  const char* b = static_cast<const char*>(p);
  while (n > 0) {
    DWORD put = 0;
    if (!WriteFile(in_, b, static_cast<DWORD>(n), &put, nullptr) || put == 0) return false;
    b += put;
    n -= put;
  }
  return true;
}

#else  // POSIX (Linux, Android)

bool SynthProcess::start(const std::string& exe, const std::string& database, std::string& error) {
  stop();
  // close-on-exec: the child gets only its stdin/stdout (dup2 clears the
  // flag on those), no other descriptor of the host process
  int in_pipe[2], out_pipe[2];
  if (pipe2(in_pipe, O_CLOEXEC) != 0) { error = "cannot create pipes"; return false; }
  if (pipe2(out_pipe, O_CLOEXEC) != 0) { close(in_pipe[0]); close(in_pipe[1]); error = "cannot create pipes"; return false; }
  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_adddup2(&fa, in_pipe[0], 0);
  posix_spawn_file_actions_adddup2(&fa, out_pipe[1], 1);
  posix_spawn_file_actions_addclose(&fa, in_pipe[1]);
  posix_spawn_file_actions_addclose(&fa, out_pipe[0]);
  posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
  char* argv[] = {const_cast<char*>(exe.c_str()), const_cast<char*>(database.c_str()), nullptr};
  pid_t pid;
  int rc = posix_spawn(&pid, exe.c_str(), &fa, nullptr, argv, environ);
  posix_spawn_file_actions_destroy(&fa);
  close(in_pipe[0]);
  close(out_pipe[1]);
  if (rc != 0) {
    close(in_pipe[1]);
    close(out_pipe[0]);
    error = "cannot start the synthesizer: " + exe;
    return false;
  }
  signal(SIGPIPE, SIG_IGN);
  pid_ = pid;
  in_ = in_pipe[1];
  out_ = out_pipe[0];
  running_ = true;
  char magic[4];
  uint32_t rate = 0;
  if (!read_all(magic, 4)) {
    stop();
    error = "the synthesizer did not start (voice database missing or invalid?): " + database;
    return false;
  }
  if (std::memcmp(magic, "MBNG", 4) != 0) {
    // error frame: 0xFFFFFFFF, len, message
    uint32_t n = 0;
    std::string msg;
    if (read_all(&n, 4) && n < 4096) {
      msg.resize(n);
      read_all(&msg[0], n);
    }
    stop();
    error = "voice database error: " + msg;
    return false;
  }
  if (!read_all(&rate, 4) || rate < 4000 || rate > 96000) {
    stop();
    error = "bad synthesizer greeting";
    return false;
  }
  rate_ = static_cast<int>(rate);
  return true;
}

void SynthProcess::stop() {
  if (in_ >= 0) { close(in_); in_ = -1; }
  if (pid_ > 0) {
    kill(pid_, SIGTERM);
    while (waitpid(pid_, nullptr, 0) < 0 && errno == EINTR) {
    }
    pid_ = -1;
  }
  if (out_ >= 0) { close(out_); out_ = -1; }
  running_ = false;
}

bool SynthProcess::read_all(void* p, size_t n) {
  char* b = static_cast<char*>(p);
  while (n > 0) {
    ssize_t got = read(out_, b, n);
    if (got < 0 && errno == EINTR) continue;
    if (got <= 0) return false;
    b += got;
    n -= static_cast<size_t>(got);
  }
  return true;
}

bool SynthProcess::write_all(const void* p, size_t n) {
  const char* b = static_cast<const char*>(p);
  while (n > 0) {
    ssize_t put = write(in_, b, n);
    if (put < 0 && errno == EINTR) continue;
    if (put <= 0) return false;
    b += put;
    n -= static_cast<size_t>(put);
  }
  return true;
}
#endif

bool SynthProcess::send(const std::string& pho, std::string& error) {
  if (!running_) { error = "synthesizer not running"; return false; }
  uint32_t n = static_cast<uint32_t>(pho.size());
  if (!write_all(&n, 4) || !write_all(pho.data(), pho.size())) {
    error = "synthesizer pipe closed";
    running_ = false;
    return false;
  }
  return true;
}

int SynthProcess::read_frame(std::vector<int16_t>& out, std::string& error) {
  uint32_t n = 0;
  if (!running_ || !read_all(&n, 4)) { running_ = false; return -1; }
  if (n == 0) return 0;
  if (n == 0xFFFFFFFFu) {
    uint32_t len = 0;
    if (!read_all(&len, 4) || len > 4096) { running_ = false; return -1; }
    std::string msg(len, '\0');
    if (len && !read_all(&msg[0], len)) { running_ = false; return -1; }
    error = msg;
    return read_frame(out, error);
  }
  if (n > (1u << 24) || (n & 1)) { running_ = false; return -1; }
  size_t old = out.size();
  out.resize(old + n / 2);
  if (!read_all(out.data() + old, n)) { running_ = false; return -1; }
  return static_cast<int>(n / 2);
}

}  // namespace mbng
