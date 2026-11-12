# MBROLA NG - RPM recipe (openSUSE Build Service: Fedora, openSUSE)
# Copyright (c) 2026 Darko Milošević
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Version is set from the git tag by the set_version service (see _service).
Name:           mbrola-ng
Version:        0.2.1
Release:        0
Summary:        MBROLA NG text-to-speech for Speech Dispatcher and Orca
# MBROLA NG: GPL-2.0-or-later; mbrola_ng_synth and external/mbrola: AGPL-3.0-only;
# languages/en/lexicon.txt (CMU Pronouncing Dictionary): BSD-2-Clause;
# languages/*/emoji.txt (Unicode CLDR): Unicode-3.0
License:        GPL-2.0-or-later AND AGPL-3.0-only AND BSD-2-Clause AND Unicode-3.0
URL:            https://github.com/Mbrola-NG/Mbrola-NG
Source0:        %{name}-%{version}.tar.xz

BuildRequires:  cmake >= 3.20
BuildRequires:  gcc-c++
BuildRequires:  python3
BuildRequires:  pkgconfig(speech-dispatcher)

Requires:       python3
Requires:       speech-dispatcher >= 0.11

%description
MBROLA NG is a text-to-speech engine for the MBROLA diphone voices. This package
contains the Speech Dispatcher output module, so Orca and every other program
that speaks through Speech Dispatcher can use it, the language data (Croatian,
English, Spanish) and mbrola-ng-voices, which downloads the voices.

The MBROLA voices themselves are not part of the package: each has its own
license (use only with MBROLA, no commercial use). Install them with
"mbrola-ng-voices install <language>".

%prep
%autosetup -n %{name}-%{version}

%build
%cmake -DCMAKE_BUILD_TYPE=Release \
       -DCMAKE_INSTALL_DOCDIR=%{_docdir}/%{name}
%cmake_build

%install
%cmake_install
# the module goes where Speech Dispatcher's pkg-config file says (it differs
# between distributions): list it from what was installed
find %{buildroot} -type f -name sd_mbrola_ng | sed "s|^%{buildroot}||" > %{_builddir}/sd-module.list
test -s %{_builddir}/sd-module.list

%check
%ctest

%files -f %{_builddir}/sd-module.list
%{_bindir}/mbrola-ng-voices
%{_libexecdir}/mbrola-ng
%{_datadir}/mbrola-ng
%{_docdir}/%{name}
%config(noreplace) %{_sysconfdir}/speech-dispatcher/modules/mbrola_ng.conf

%changelog
* Wed Nov 11 2026 Darko Milošević <daremc86@gmail.com> - 0.2.1-0
- Packaged for the openSUSE Build Service
