// MBROLA NG - SAPI 5 engine (ANALYSIS section 11)
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Lives in MBROLA_NG.dll next to the C API; it only drives the C API.
// Nothing is registered when the DLL is merely loaded (the NVDA add-on uses
// the same DLL through the C API): COM class and voice tokens are written
// only by DllRegisterServer, which the Windows installer calls ("regsvr32"),
// once per bitness, so every token lands in the registry view of its DLL.
//
// Voice tokens (11.5) are made from the voices installed in
// %ProgramData%\MBROLA NG\voices\<id>\ (<id> database + <id>.voice) whose
// language (<dll folder>\..\languages\<code>.dat) is installed.
#ifdef _WIN32

#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>
#include <olectl.h>
#include <shlobj.h>
#include <sperror.h>

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cwchar>
#include <fstream>
#include <map>
#include <new>
#include <string>
#include <vector>

#include "mbrola_ng.h"

namespace {

// {BBA01711-0D0F-4406-81B4-E5D4833986C0}
const CLSID CLSID_MbngEngine = {0xbba01711, 0x0d0f, 0x4406, {0x81, 0xb4, 0xe5, 0xd4, 0x83, 0x39, 0x86, 0xc0}};
const wchar_t kClsidText[] = L"{BBA01711-0D0F-4406-81B4-E5D4833986C0}";
const wchar_t kEngineName[] = L"MBROLA NG SAPI 5 engine";
const wchar_t kTokensKey[] = L"SOFTWARE\\Microsoft\\Speech\\Voices\\Tokens";
const wchar_t kTokenPrefix[] = L"MBROLA-NG-";

std::atomic<long> g_objects{0};  // live COM objects + server locks

// ------------------------------------------------------------------ helpers
HMODULE this_module() {
  HMODULE m = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(&this_module), &m);
  return m;
}

std::wstring module_path() {
  std::wstring p(MAX_PATH, L'\0');
  for (;;) {
    DWORD n = GetModuleFileNameW(this_module(), &p[0], static_cast<DWORD>(p.size()));
    if (n == 0) return std::wstring();
    if (n < p.size()) {
      p.resize(n);
      return p;
    }
    p.resize(p.size() * 2);
  }
}

std::wstring parent_dir(const std::wstring& path) {
  size_t k = path.find_last_of(L"\\/");
  return k == std::wstring::npos ? std::wstring() : path.substr(0, k);
}

std::string to_utf8(const std::wstring& w) {
  if (w.empty()) return std::string();
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
  return s;
}

std::wstring from_utf8(const std::string& s) {
  if (s.empty()) return std::wstring();
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
  return w;
}

bool file_exists(const std::wstring& p) {
  DWORD a = GetFileAttributesW(p.c_str());
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring voices_root() {
  PWSTR pd = nullptr;
  std::wstring r;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &pd))) r = std::wstring(pd) + L"\\MBROLA NG\\voices";
  CoTaskMemFree(pd);
  if (r.empty()) {
    wchar_t env[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"ProgramData", env, MAX_PATH);
    if (n > 0 && n < MAX_PATH) r = std::wstring(env) + L"\\MBROLA NG\\voices";
  }
  return r;
}

// <id>.voice: "key = value" lines, UTF-8 (ANALYSIS 10.3)
std::map<std::string, std::string> read_voice_file(const std::wstring& path) {
  std::map<std::string, std::string> kv;
  std::ifstream f(path.c_str(), std::ios::binary);
  std::string line;
  while (std::getline(f, line)) {
    if (line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
    size_t eq = line.find('=');
    size_t b = line.find_first_not_of(" \t\r");
    if (eq == std::string::npos || b == std::string::npos || line[b] == '#') continue;
    auto trim = [](std::string s) {
      size_t x = s.find_first_not_of(" \t\r"), y = s.find_last_not_of(" \t\r");
      return x == std::string::npos ? std::string() : s.substr(x, y - x + 1);
    };
    kv[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
  }
  return kv;
}

// "hr" -> 0x041A, "en" -> 0x0409 (the default region of the language)
LCID lcid_of_language(const std::string& code) {
  if (code.empty()) return 0;
  wchar_t full[LOCALE_NAME_MAX_LENGTH] = {0};
  if (!ResolveLocaleName(from_utf8(code).c_str(), full, LOCALE_NAME_MAX_LENGTH)) return 0;
  LCID id = LocaleNameToLCID(full, 0);
  return id == LOCALE_CUSTOM_UNSPECIFIED ? 0 : id;
}

std::wstring hex_lcid(LCID id) {
  wchar_t b[16];
  swprintf(b, 16, L"%X", static_cast<unsigned>(id & 0xFFFF));
  return b;
}

LSTATUS set_str(HKEY k, const wchar_t* name, const std::wstring& v) {
  return RegSetValueExW(k, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()),
                        static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t)));
}

void delete_our_tokens() {
  HKEY k;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kTokensKey, 0, KEY_READ | KEY_WRITE, &k) != ERROR_SUCCESS) return;
  std::vector<std::wstring> ours;
  wchar_t name[256];
  for (DWORD i = 0;; ++i) {
    DWORD n = 256;
    if (RegEnumKeyExW(k, i, name, &n, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
    if (wcsncmp(name, kTokenPrefix, wcslen(kTokenPrefix)) == 0) ours.push_back(name);
  }
  for (const auto& t : ours) RegDeleteTreeW(k, t.c_str());
  RegCloseKey(k);
}

HRESULT register_voice(const std::wstring& id, const std::wstring& folder, const std::wstring& lang_dir) {
  auto v = read_voice_file(folder + L"\\" + id + L".voice");
  std::string lang = v["language"];
  std::wstring dat = lang_dir + L"\\" + from_utf8(lang) + L".dat";
  if (lang.empty() || !file_exists(dat)) return S_FALSE;  // no language for it in this installation
  LCID lcid = 0;
  if (!v["sapi_lcid"].empty()) lcid = static_cast<LCID>(std::strtoul(v["sapi_lcid"].c_str(), nullptr, 16));
  if (!lcid) lcid = lcid_of_language(lang);
  if (!lcid) return S_FALSE;

  std::wstring name_en = from_utf8(v["name_en"].empty() ? v["id"] : v["name_en"]);
  if (name_en.empty()) name_en = id;
  std::wstring display = L"MBROLA NG " + name_en;

  HKEY tok;
  std::wstring key = std::wstring(kTokensKey) + L"\\" + kTokenPrefix + id;
  if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &tok, nullptr) != ERROR_SUCCESS)
    return SELFREG_E_CLASS;
  set_str(tok, nullptr, display);
  // localized names: value named by the LANGID of the UI language (SpGetDescription)
  for (const auto& kv : v) {
    if (kv.first.compare(0, 5, "name_") != 0 || kv.second.empty()) continue;
    LCID ui = lcid_of_language(kv.first.substr(5));
    if (ui) set_str(tok, hex_lcid(ui).c_str(), L"MBROLA NG " + from_utf8(kv.second));
  }
  set_str(tok, L"CLSID", kClsidText);
  set_str(tok, L"VoicePath", folder + L"\\" + id);
  set_str(tok, L"LanguagePath", dat);
  set_str(tok, L"BasePitch", from_utf8(v["base_pitch"]));
  set_str(tok, L"PhonemeMap", from_utf8(v["phoneme_map"]));

  HKEY attr;
  if (RegCreateKeyExW(tok, L"Attributes", 0, nullptr, 0, KEY_WRITE, nullptr, &attr, nullptr) == ERROR_SUCCESS) {
    std::string g = v["gender"];
    set_str(attr, L"Name", display);
    set_str(attr, L"Gender", g == "female" ? L"Female" : g == "male" ? L"Male" : L"Neutral");
    set_str(attr, L"Age", v["age"] == "child" ? L"Child" : v["age"] == "senior" ? L"Senior" : L"Adult");
    set_str(attr, L"Language", hex_lcid(lcid));
    set_str(attr, L"Vendor", L"MBROLA NG");
    set_str(attr, L"Version", from_utf8(v["version"]));
    RegCloseKey(attr);
  }
  RegCloseKey(tok);
  return S_OK;
}

// ------------------------------------------------------------------ engine
double rate_percent(long r) {  // SAPI -10..+10 -> 33 % .. 300 % (Microsoft convention)
  if (r < -10) r = -10;
  if (r > 10) r = 10;
  return 100.0 * std::pow(3.0, r / 10.0);
}

double pitch_percent(long p) {  // -10..+10 -> 50 % .. 200 %
  if (p < -24) p = -24;
  if (p > 24) p = 24;
  return 100.0 * std::pow(2.0, p / 10.0);
}

class Engine : public ISpTTSEngine, public ISpObjectWithToken {
 public:
  Engine() { ++g_objects; }
  ~Engine() {
    if (eng_) mbng_destroy(eng_);
    if (token_) token_->Release();
    --g_objects;
  }

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == __uuidof(ISpTTSEngine))
      *ppv = static_cast<ISpTTSEngine*>(this);
    else if (riid == __uuidof(ISpObjectWithToken))
      *ppv = static_cast<ISpObjectWithToken*>(this);
    else {
      *ppv = nullptr;
      return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
  STDMETHODIMP_(ULONG) Release() override {
    ULONG r = --refs_;
    if (r == 0) delete this;
    return r;
  }

  // ISpObjectWithToken
  STDMETHODIMP SetObjectToken(ISpObjectToken* token) override {
    if (!token) return E_INVALIDARG;
    if (token_) return SPERR_ALREADY_INITIALIZED;
    token->AddRef();
    token_ = token;
    std::string voice = token_string(L"VoicePath"), lang = token_string(L"LanguagePath");
    std::string pmap = token_string(L"PhonemeMap");
    std::string synth = to_utf8(parent_dir(module_path()) + L"\\mbrola_ng_synth.exe");
    mbng_config c = {};
    c.struct_size = sizeof c;
    c.language_path = lang.c_str();
    c.voice_path = voice.c_str();
    c.synth_path = synth.c_str();
    c.offset_unit = MBNG_OFFSET_UTF16;
    c.base_pitch = std::atoi(token_string(L"BasePitch").c_str());
    c.phoneme_map = pmap.c_str();
    int err = 0;
    eng_ = mbng_create(&c, &err);
    if (!eng_) {
      OutputDebugStringA(("MBROLA NG SAPI: " + std::string(mbng_last_error(nullptr)) + "\n").c_str());
      return E_FAIL;
    }
    int rate = 16000;
    mbng_get_audio_format(eng_, &rate, nullptr, nullptr);
    sample_rate_ = rate;
    mbng_get_param(eng_, MBNG_PARAM_PUNCTUATION, &default_punct_);
    return S_OK;
  }
  STDMETHODIMP GetObjectToken(ISpObjectToken** ppToken) override {
    if (!ppToken) return E_POINTER;
    *ppToken = token_;
    if (token_) token_->AddRef();
    return token_ ? S_OK : S_FALSE;
  }

  // ISpTTSEngine
  STDMETHODIMP GetOutputFormat(const GUID*, const WAVEFORMATEX*, GUID* fmt_id, WAVEFORMATEX** fmt) override {
    if (!fmt_id || !fmt) return E_POINTER;
    WAVEFORMATEX* w = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(WAVEFORMATEX)));
    if (!w) return E_OUTOFMEMORY;
    w->wFormatTag = WAVE_FORMAT_PCM;
    w->nChannels = 1;
    w->nSamplesPerSec = sample_rate_;
    w->wBitsPerSample = 16;
    w->nBlockAlign = 2;
    w->nAvgBytesPerSec = sample_rate_ * 2;
    w->cbSize = 0;
    *fmt_id = SPDFID_WaveFormatEx;
    *fmt = w;
    return S_OK;
  }

  STDMETHODIMP Speak(DWORD flags, REFGUID, const WAVEFORMATEX*, const SPVTEXTFRAG* frags,
                     ISpTTSEngineSite* site) override {
    if (!site) return E_INVALIDARG;
    if (!eng_) return SPERR_UNINITIALIZED;
    try {
      return speak(flags, frags, site);
    } catch (...) {  // no exception may leave a COM method (11.6)
      return E_FAIL;
    }
  }

 private:
  std::string token_string(const wchar_t* name) {
    LPWSTR s = nullptr;
    std::string r;
    if (token_ && SUCCEEDED(token_->GetStringValue(name, &s)) && s) r = to_utf8(s);
    CoTaskMemFree(s);
    return r;
  }

  void apply_site_params(ISpTTSEngineSite* site, USHORT frag_volume) {
    long rate = 0;
    USHORT vol = 100;
    site->GetRate(&rate);
    site->GetVolume(&vol);
    mbng_set_param(eng_, MBNG_PARAM_RATE, static_cast<int>(rate_percent(rate) + 0.5));
    mbng_set_param(eng_, MBNG_PARAM_VOLUME, vol * frag_volume / 100);
  }

  HRESULT speak(DWORD flags, const SPVTEXTFRAG* frags, ISpTTSEngineSite* site) {
    std::vector<mbng_segment> segs;
    std::vector<std::wstring> bookmarks;
    auto add = [&segs](int type, const WCHAR* text, ULONG len, ULONG offset, int value) {
      mbng_segment s = {};
      s.type = type;
      s.text16 = reinterpret_cast<const uint16_t*>(text);
      s.length = static_cast<int32_t>(len);
      s.offset_base = static_cast<int32_t>(offset);
      s.value = value;
      segs.push_back(s);
    };
    int cur_rate = 100, cur_pitch = 100;
    for (const SPVTEXTFRAG* f = frags; f; f = f->pNext) {
      const SPVSTATE& st = f->State;
      int rate = static_cast<int>(rate_percent(st.RateAdj) + 0.5);
      int pitch = static_cast<int>(pitch_percent(st.PitchAdj.MiddleAdj) + 0.5);
      if (rate != cur_rate) add(MBNG_SEG_RATE, nullptr, 0, 0, cur_rate = rate);
      if (pitch != cur_pitch) add(MBNG_SEG_PITCH, nullptr, 0, 0, cur_pitch = pitch);
      switch (st.eAction) {
        case SPVA_Speak:
        case SPVA_Pronounce:  // no phone converter for our languages: read the text
          if (f->pTextStart && f->ulTextLen) add(MBNG_SEG_TEXT, f->pTextStart, f->ulTextLen, f->ulTextSrcOffset, 0);
          break;
        case SPVA_SpellOut:
          add(MBNG_SEG_SPELL_ON, nullptr, 0, 0, 0);
          if (f->pTextStart && f->ulTextLen) add(MBNG_SEG_TEXT, f->pTextStart, f->ulTextLen, f->ulTextSrcOffset, 0);
          add(MBNG_SEG_SPELL_OFF, nullptr, 0, 0, 0);
          break;
        case SPVA_Silence:
          add(MBNG_SEG_BREAK, nullptr, 0, 0, static_cast<int>(st.SilenceMSecs));
          break;
        case SPVA_Bookmark:
          bookmarks.emplace_back(f->pTextStart ? std::wstring(f->pTextStart, f->ulTextLen) : std::wstring());
          add(MBNG_SEG_MARK, nullptr, 0, 0, static_cast<int>(bookmarks.size() - 1));
          break;
        default:  // SPVA_Section, SPVA_ParseUnknownTag
          break;
      }
    }
    if (segs.empty()) return S_OK;

    mbng_set_param(eng_, MBNG_PARAM_PUNCTUATION, (flags & SPF_NLP_SPEAK_PUNC) ? 3 : default_punct_);
    apply_site_params(site, frags->State.Volume);
    if (mbng_begin(eng_, segs.data(), static_cast<int>(segs.size())) != MBNG_OK) return E_FAIL;

    ULONGLONG interest = 0;
    site->GetEventInterest(&interest);
    const int block = sample_rate_ / 20;  // 50 ms: GetActions() is polled between blocks (11.3)
    std::vector<int16_t> pcm(block);
    mbng_event evs[64];
    long skip_target = 0, skipped = 0;  // sentences to skip / skipped so far
    int64_t dropped = 0;                // samples not written because of a skip
    int64_t written = 0;                // samples written to the site

    for (;;) {
      DWORD act = site->GetActions();
      if (act & SPVES_ABORT) {
        mbng_cancel(eng_);
        break;
      }
      if (act & SPVES_SKIP) {
        SPVSKIPTYPE type;
        long n = 0;
        if (SUCCEEDED(site->GetSkipInfo(&type, &n)) && type == SPVST_SENTENCE && n > 0) {
          skip_target = n;
          skipped = 0;
        } else {
          site->CompleteSkip(0);
        }
      }
      if (act & (SPVES_RATE | SPVES_VOLUME)) apply_site_params(site, frags->State.Volume);

      int ne = 0;
      int n = mbng_read(eng_, pcm.data(), block, evs, 64, &ne);
      if (n < 0) {
        OutputDebugStringA(("MBROLA NG SAPI: " + std::string(mbng_last_error(eng_)) + "\n").c_str());
        return E_FAIL;
      }
      bool skipping = skip_target > 0;
      std::vector<SPEVENT> out;
      for (int i = 0; i < ne; ++i) {
        const mbng_event& e = evs[i];
        if (e.type == MBNG_EVENT_SENTENCE && skip_target > 0 && ++skipped >= skip_target) {
          site->CompleteSkip(skipped);
          skip_target = 0;
        }
        if (skipping) continue;
        SPEVENT se = {};
        int64_t at = e.sample - dropped;
        se.ullAudioStreamOffset = static_cast<ULONGLONG>(at < written ? written : at) * 2;
        if (e.type == MBNG_EVENT_WORD) {
          se.eEventId = SPEI_WORD_BOUNDARY;
          se.lParam = e.text_offset;
          se.wParam = e.text_length;
        } else if (e.type == MBNG_EVENT_SENTENCE) {
          se.eEventId = SPEI_SENTENCE_BOUNDARY;
          se.lParam = e.text_offset;
          se.wParam = e.text_length;
        } else if (e.type == MBNG_EVENT_MARK && e.value >= 0 && e.value < static_cast<int>(bookmarks.size())) {
          se.eEventId = SPEI_TTS_BOOKMARK;
          se.elParamType = SPET_LPARAM_IS_STRING;
          se.lParam = reinterpret_cast<LPARAM>(bookmarks[e.value].c_str());
          se.wParam = std::wcstol(bookmarks[e.value].c_str(), nullptr, 10);
        } else {
          continue;
        }
        if (interest & SPFEI(se.eEventId)) out.push_back(se);
      }
      if (!out.empty()) site->AddEvents(out.data(), static_cast<ULONG>(out.size()));
      if (n == 0) {
        if (ne == 0) break;
        continue;
      }
      if (skipping) {
        dropped += n;
        continue;
      }
      ULONG done = 0;
      if (FAILED(site->Write(pcm.data(), static_cast<ULONG>(n) * 2, &done))) {
        mbng_cancel(eng_);
        break;
      }
      written += n;
    }
    if (skip_target > 0) site->CompleteSkip(skipped);  // the text ended first
    return S_OK;
  }

  std::atomic<ULONG> refs_{1};
  ISpObjectToken* token_ = nullptr;
  mbng_engine* eng_ = nullptr;
  int sample_rate_ = 16000;
  int default_punct_ = 1;
};

class Factory : public IClassFactory {
 public:
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IClassFactory) {
      *ppv = static_cast<IClassFactory*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return 2; }  // static object
  STDMETHODIMP_(ULONG) Release() override { return 1; }
  STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (outer) return CLASS_E_NOAGGREGATION;
    Engine* e = new (std::nothrow) Engine;
    if (!e) return E_OUTOFMEMORY;
    HRESULT hr = e->QueryInterface(riid, ppv);
    e->Release();
    return hr;
  }
  STDMETHODIMP LockServer(BOOL lock) override {
    if (lock) ++g_objects;
    else --g_objects;
    return S_OK;
  }
};

Factory g_factory;

}  // namespace

// ------------------------------------------------------ COM server exports
// (exported through mbrola_ng.def)
extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv) {
  if (!ppv) return E_POINTER;
  *ppv = nullptr;
  if (clsid != CLSID_MbngEngine) return CLASS_E_CLASSNOTAVAILABLE;
  return g_factory.QueryInterface(riid, ppv);
}

extern "C" HRESULT __stdcall DllCanUnloadNow() { return g_objects == 0 ? S_OK : S_FALSE; }

extern "C" HRESULT __stdcall DllUnregisterServer() {
  delete_our_tokens();
  RegDeleteTreeW(HKEY_LOCAL_MACHINE, (std::wstring(L"SOFTWARE\\Classes\\CLSID\\") + kClsidText).c_str());
  return S_OK;
}

extern "C" HRESULT __stdcall DllRegisterServer() {
  std::wstring dll = module_path();
  if (dll.empty()) return SELFREG_E_CLASS;
  HKEY k, srv;
  std::wstring key = std::wstring(L"SOFTWARE\\Classes\\CLSID\\") + kClsidText;
  if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS)
    return SELFREG_E_CLASS;
  set_str(k, nullptr, kEngineName);
  LSTATUS st = RegCreateKeyExW(k, L"InprocServer32", 0, nullptr, 0, KEY_WRITE, nullptr, &srv, nullptr);
  RegCloseKey(k);
  if (st != ERROR_SUCCESS) return SELFREG_E_CLASS;
  set_str(srv, nullptr, dll);
  set_str(srv, L"ThreadingModel", L"Both");
  RegCloseKey(srv);

  // voice tokens: exactly the installed voices (10.8: never a silent voice)
  delete_our_tokens();
  std::wstring root = voices_root();
  std::wstring lang_dir = parent_dir(parent_dir(dll)) + L"\\languages";
  WIN32_FIND_DATAW fd = {};
  HANDLE h = root.empty() ? INVALID_HANDLE_VALUE : FindFirstFileW((root + L"\\*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return S_OK;
  HRESULT hr = S_OK;
  do {
    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
    std::wstring id = fd.cFileName, folder = root + L"\\" + id;
    if (!file_exists(folder + L"\\" + id)) continue;
    if (FAILED(register_voice(id, folder, lang_dir))) hr = SELFREG_E_CLASS;
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  return hr;
}

#endif  // _WIN32
