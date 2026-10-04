// MBROLA NG - mbng::SynthProcess for Apple platforms: MBROLA in the process
// Copyright (c) 2026 Darko Milošević and the MBROLA NG contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Replaces core/src/engine/synthproc.cpp in the Apple build. The core talks
// to "the synthesis process" through this class; iOS cannot start a
// process, so here the same interface is served by MBROLA linked into the
// speech extension (mbrola_inproc.c, AGPL v3 - see the license note there).
//
// Unlike the pipe version, a chunk is synthesized piece by piece while it is
// read, so the first audio of a long sentence is there at once.
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "engine/synthproc.h"
#include "mbrola_inproc.h"

namespace mbng {

namespace {

struct Channel {
  mbng_mbrola* mbrola = nullptr;
  std::string pho;    // the chunk being synthesized
  size_t fed = 0;     // bytes of `pho` already given to MBROLA
  bool open = false;  // a chunk was sent and its end not yet reported
};

// The class declaration is shared with the pipe version and has no member
// for this state, so it is kept beside the object.
std::mutex g_mutex;
std::unordered_map<const SynthProcess*, Channel> g_channels;

Channel* channel_of(const SynthProcess* p) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_channels.find(p);
  return it == g_channels.end() ? nullptr : &it->second;  // references to map values stay valid
}

}  // namespace

bool SynthProcess::start(const std::string& /*exe*/, const std::string& database, std::string& error) {
  stop();
  char msg[512];
  int rate = 0;
  mbng_mbrola* m = mbng_mbrola_open(database.c_str(), &rate, msg, sizeof msg);
  if (!m) {
    error = std::string("voice database error: ") + (msg[0] ? msg : database.c_str());
    return false;
  }
  if (rate < 4000 || rate > 96000) {
    mbng_mbrola_close(m);
    error = "voice database error: unsupported sample rate";
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_channels[this].mbrola = m;
  }
  rate_ = rate;
  running_ = true;
  return true;
}

void SynthProcess::stop() {
  mbng_mbrola* m = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_channels.find(this);
    if (it != g_channels.end()) {
      m = it->second.mbrola;
      g_channels.erase(it);
    }
  }
  mbng_mbrola_close(m);
  running_ = false;
}

bool SynthProcess::send(const std::string& pho, std::string& error) {
  Channel* c = running_ ? channel_of(this) : nullptr;
  if (!c) {
    error = "synthesizer not running";
    return false;
  }
  if (c->open) mbng_mbrola_reset(c->mbrola);  // never happens: the core reads a chunk to its end
  c->pho = pho;
  c->fed = 0;
  c->open = true;
  return true;
}

int SynthProcess::read_frame(std::vector<int16_t>& out, std::string& error) {
  Channel* c = running_ ? channel_of(this) : nullptr;
  if (!c) {
    running_ = false;
    return -1;
  }
  if (!c->open) return 0;
  const int kBlock = 2048;
  const size_t old = out.size();
  out.resize(old + kBlock);
  for (;;) {
    char msg[512];
    int n = mbng_mbrola_read(c->mbrola, out.data() + old, kBlock, msg, sizeof msg);
    if (n > 0) {
      out.resize(old + static_cast<size_t>(n));
      return n;
    }
    if (n < 0) {  // MBROLA refused the input: the rest of the chunk is dropped
      error = msg;
      break;
    }
    if (c->fed >= c->pho.size()) break;  // all of the chunk is synthesized
    // feed whole lines, as many as surely fit MBROLA's input buffer (8 KB)
    size_t len = 0;
    while (c->fed + len < c->pho.size() && len < 2000) {
      size_t nl = c->pho.find('\n', c->fed + len);
      size_t l = (nl == std::string::npos ? c->pho.size() : nl + 1) - (c->fed + len);
      if (len > 0 && len + l > 2000) break;
      len += l;
    }
    std::string piece = c->pho.substr(c->fed, len);
    c->fed += len;
    if (mbng_mbrola_write(c->mbrola, piece.c_str()) <= 0) {
      error = "phoneme line too long";
      mbng_mbrola_reset(c->mbrola);
      break;
    }
  }
  out.resize(old);
  c->pho.clear();
  c->fed = 0;
  c->open = false;
  return 0;
}

// Not used by this implementation (no pipe).
bool SynthProcess::read_all(void*, size_t) { return false; }
bool SynthProcess::write_all(const void*, size_t) { return false; }

}  // namespace mbng
