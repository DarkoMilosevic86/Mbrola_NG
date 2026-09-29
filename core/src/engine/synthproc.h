// MBROLA NG - client of the separate synthesis process mbrola_ng_synth
// (ANALYSIS 4.2 option A, 5.9). The only OS-specific part of the core.
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mbng {

class SynthProcess {
 public:
  SynthProcess() = default;
  ~SynthProcess() { stop(); }
  SynthProcess(const SynthProcess&) = delete;
  SynthProcess& operator=(const SynthProcess&) = delete;

  // Starts `exe <database>` and reads the greeting (sample rate).
  bool start(const std::string& exe, const std::string& database, std::string& error);
  void stop();
  bool running() const { return running_; }
  int sample_rate() const { return rate_; }

  // Sends one chunk of .pho text (must end with '#').
  bool send(const std::string& pho, std::string& error);
  // Reads one response frame: > 0 samples appended to `out`, 0 = end of
  // chunk, < 0 = pipe error. A synthesizer error message is put in
  // `error` (the chunk still ends normally).
  int read_frame(std::vector<int16_t>& out, std::string& error);

 private:
  bool read_all(void* p, size_t n);
  bool write_all(const void* p, size_t n);
  bool running_ = false;
  int rate_ = 16000;
#ifdef _WIN32
  void* proc_ = nullptr;
  void* in_ = nullptr;   // our write end of the child's stdin
  void* out_ = nullptr;  // our read end of the child's stdout
#else
  int pid_ = -1;
  int in_ = -1, out_ = -1;
#endif
};

}  // namespace mbng
