# MBROLA NG - Voice Manager for NVDA (ANALYSIS 12.5)
# Copyright (c) 2026 Darko Milošević
# SPDX-License-Identifier: GPL-2.0-or-later
#
# NVDA menu -> Tools -> "MBROLA_NG Voice Manager...": installed voices and
# voices available from the voice catalog; download with license
# acceptance, verification (size, SHA-256, database header, phonemes),
# install from file, update, remove, set as current voice.

import os
import shutil
import tempfile
import threading
import urllib.request
import zipfile

import addonHandler
import globalPluginHandler
import gui
import synthDriverHandler
import wx
from gui import guiHelper
from logHandler import log
from scriptHandler import script

addonHandler.initTranslation()

SYNTH_NAME = "mbrola_ng"


def _voicesModule():
	from synthDrivers.mbrola_ng import _voices
	return _voices


def _uiLanguage():
	try:
		import languageHandler
		return languageHandler.getLanguage()
	except Exception:
		return "en"


def _formatSize(n):
	mb = n / (1024.0 * 1024.0)
	# Translators: size of a voice in megabytes, e.g. "3.4 MB"
	text = _("{size:.1f} MB").format(size=mb)
	if _uiLanguage().startswith("hr"):
		text = text.replace(".", ",")
	return text


def _currentSynthIsOurs():
	s = synthDriverHandler.getSynth()
	return s is not None and s.name == SYNTH_NAME


# =============================================================== dialogs
class _LicenseDialog(wx.Dialog):
	def __init__(self, parent, voiceName, text):
		# Translators: title of the license dialog, {voice} is the voice name
		super().__init__(parent, title=_("License of {voice}").format(voice=voiceName))
		main = guiHelper.BoxSizerHelper(self, orientation=wx.VERTICAL)
		main.addItem(wx.StaticText(self, label=_(
			# Translators: text above the license in the license dialog
			"The voice has its own license. Read it and accept it to install the voice.")))
		self.text = main.addLabeledControl(
			# Translators: label of the license text field
			_("License &text:"), wx.TextCtrl,
			value=text, style=wx.TE_MULTILINE | wx.TE_READONLY | wx.TE_RICH2, size=(560, 300))
		buttons = guiHelper.ButtonHelper(wx.HORIZONTAL)
		# Translators: button in the license dialog
		accept = buttons.addButton(self, wx.ID_YES, _("I &accept"))
		# Translators: button in the license dialog
		buttons.addButton(self, wx.ID_NO, _("&Decline"))
		main.addDialogDismissButtons(buttons)
		accept.Bind(wx.EVT_BUTTON, lambda e: self.EndModal(wx.ID_YES))
		self.Bind(wx.EVT_BUTTON, lambda e: self.EndModal(wx.ID_NO), id=wx.ID_NO)
		self.SetEscapeId(wx.ID_NO)
		outer = wx.BoxSizer(wx.VERTICAL)
		outer.Add(main.sizer, border=guiHelper.BORDER_FOR_DIALOGS, flag=wx.ALL | wx.EXPAND, proportion=1)
		self.SetSizerAndFit(outer)
		self.CentreOnScreen()
		self.text.SetFocus()


class _ProgressDialog(wx.Dialog):
	def __init__(self, parent, title):
		super().__init__(parent, title=title)
		self.cancelled = False
		main = guiHelper.BoxSizerHelper(self, orientation=wx.VERTICAL)
		# Translators: initial status in the download progress dialog
		self.status = main.addItem(wx.StaticText(self, label=_("Connecting..."), size=(420, -1)))
		self.gauge = main.addItem(wx.Gauge(self, range=100, size=(420, -1)))
		buttons = guiHelper.ButtonHelper(wx.HORIZONTAL)
		self.cancelButton = buttons.addButton(self, wx.ID_CANCEL, _("Cancel"))
		main.addDialogDismissButtons(buttons)
		self.cancelButton.Bind(wx.EVT_BUTTON, self._onCancel)
		self.Bind(wx.EVT_CLOSE, self._onCancel)
		outer = wx.BoxSizer(wx.VERTICAL)
		outer.Add(main.sizer, border=guiHelper.BORDER_FOR_DIALOGS, flag=wx.ALL | wx.EXPAND)
		self.SetSizerAndFit(outer)
		self.CentreOnScreen()

	def _onCancel(self, evt):
		self.cancelled = True
		# Translators: status while a download is being cancelled
		self.status.SetLabel(_("Cancelling..."))

	def update(self, percent, text):
		if not self:
			return
		self.gauge.SetValue(max(0, min(100, int(percent))))
		if text and self.status.GetLabel() != text:
			self.status.SetLabel(text)


class _Cancelled(Exception):
	pass


# ================================================================ manager
class VoiceManagerDialog(wx.Dialog):
	_instance = None

	@classmethod
	def show(cls):
		if cls._instance:
			cls._instance.Raise()
			return
		gui.mainFrame.prePopup()
		cls._instance = VoiceManagerDialog(gui.mainFrame)
		cls._instance.Show()
		gui.mainFrame.postPopup()

	def __init__(self, parent):
		# Translators: title of the Voice Manager dialog
		super().__init__(parent, title=_("MBROLA_NG Voice Manager"))
		self.V = _voicesModule()
		self.catalog = {"voices": [], "languages": []}
		self.catalogSource = "none"
		self.installed = []
		self.available = []
		self._busy = False

		main = guiHelper.BoxSizerHelper(self, orientation=wx.VERTICAL)
		self.book = wx.Notebook(self)

		# ---- Installed tab
		p = wx.Panel(self.book)
		ps = guiHelper.BoxSizerHelper(p, orientation=wx.VERTICAL)
		self.installedList = ps.addLabeledControl(
			# Translators: label of the list of installed voices
			_("&Installed voices:"), wx.ListCtrl, style=wx.LC_REPORT | wx.LC_SINGLE_SEL, size=(620, 200))
		for col, width in (
			# Translators: column in the voice lists
			(_("Voice"), 220), (_("Language"), 110), (_("Version"), 80), (_("Size"), 80), (_("Location"), 120),
		):
			self.installedList.AppendColumn(col, width=width)
		# Translators: shown when no voice is installed
		self.emptyText = ps.addItem(wx.StaticText(p, label=_(
			"No voices are installed. Open the Available tab to download a voice.")))
		bh = guiHelper.ButtonHelper(wx.HORIZONTAL)
		# Translators: button in the Installed tab
		self.setCurrentButton = bh.addButton(p, label=_("Set as &current voice"))
		# Translators: button in the Installed tab
		self.updateButton = bh.addButton(p, label=_("Check for &updates"))
		# Translators: button in the Installed tab
		self.removeButton = bh.addButton(p, label=_("&Remove"))
		# Translators: button in the Installed tab
		self.fromFileButton = bh.addButton(p, label=_("Install from &file..."))
		ps.addItem(bh)
		p.SetSizer(ps.sizer)
		# Translators: name of a tab in the Voice Manager
		self.book.AddPage(p, _("Installed"))

		# ---- Available tab
		p2 = wx.Panel(self.book)
		ps2 = guiHelper.BoxSizerHelper(p2, orientation=wx.VERTICAL)
		self.availableList = ps2.addLabeledControl(
			# Translators: label of the list of voices that can be downloaded
			_("&Available voices:"), wx.ListCtrl, style=wx.LC_REPORT | wx.LC_SINGLE_SEL, size=(620, 200))
		for col, width in ((_("Voice"), 260), (_("Language"), 120), (_("Size"), 90), (_("Status"), 140)):
			self.availableList.AppendColumn(col, width=width)
		self.catalogText = ps2.addItem(wx.StaticText(p2, label=""))
		bh2 = guiHelper.ButtonHelper(wx.HORIZONTAL)
		# Translators: button in the Available tab
		self.installButton = bh2.addButton(p2, label=_("&Install"))
		ps2.addItem(bh2)
		p2.SetSizer(ps2.sizer)
		# Translators: name of a tab in the Voice Manager
		self.book.AddPage(p2, _("Available"))

		main.addItem(self.book, flag=wx.EXPAND, proportion=1)
		close = guiHelper.ButtonHelper(wx.HORIZONTAL)
		close.addButton(self, wx.ID_CLOSE, _("&Close"))
		main.addDialogDismissButtons(close)

		self.setCurrentButton.Bind(wx.EVT_BUTTON, self.onSetCurrent)
		self.updateButton.Bind(wx.EVT_BUTTON, self.onCheckUpdates)
		self.removeButton.Bind(wx.EVT_BUTTON, self.onRemove)
		self.fromFileButton.Bind(wx.EVT_BUTTON, self.onInstallFromFile)
		self.installButton.Bind(wx.EVT_BUTTON, self.onInstall)
		self.installedList.Bind(wx.EVT_LIST_ITEM_SELECTED, lambda e: self._updateButtons())
		self.availableList.Bind(wx.EVT_LIST_ITEM_SELECTED, lambda e: self._updateButtons())
		self.availableList.Bind(wx.EVT_LIST_ITEM_ACTIVATED, self.onInstall)
		self.Bind(wx.EVT_BUTTON, lambda e: self.Close(), id=wx.ID_CLOSE)
		self.Bind(wx.EVT_CLOSE, self.onClose)
		self.SetEscapeId(wx.ID_CLOSE)

		outer = wx.BoxSizer(wx.VERTICAL)
		outer.Add(main.sizer, border=guiHelper.BORDER_FOR_DIALOGS, flag=wx.ALL | wx.EXPAND, proportion=1)
		self.SetSizerAndFit(outer)
		self.CentreOnScreen()

		# offline data first, then the online catalog in the background
		self.catalog, self.catalogSource = self.V.loadCatalog(online=False)
		self.refresh()
		if not self.installed:
			self.book.SetSelection(1)
		self.installedList.SetFocus() if self.installed else self.availableList.SetFocus()
		threading.Thread(target=self._loadOnlineCatalog, daemon=True).start()

	# ----------------------------------------------------------- data/UI
	def _loadOnlineCatalog(self):
		try:
			cat, source = self.V.loadCatalog(online=True)
		except Exception:
			log.error("MBROLA NG: catalog", exc_info=True)
			return
		wx.CallAfter(self._setCatalog, cat, source)

	def _setCatalog(self, cat, source):
		if not self:
			return
		self.catalog, self.catalogSource = cat, source
		self.refresh()

	def _languageName(self, code):
		for lang in self.catalog.get("languages", []):
			if lang.get("code") == code:
				return self.V.localized(lang.get("names", {}), _uiLanguage())
		return code

	def refresh(self):
		self.installed = self.V.installedVoices(self.catalog)
		self.available = self.V.catalogVoices(self.catalog)
		ui = _uiLanguage()
		sel = self.installedList.GetFirstSelected()
		self.installedList.DeleteAllItems()
		current = None
		s = synthDriverHandler.getSynth()
		if s is not None and s.name == SYNTH_NAME:
			current = s.voice
		for v in self.installed:
			name = self.V.localized(self.V.voiceNames(v), ui)
			if v["id"] == current:
				# Translators: marks the voice NVDA is speaking with, e.g. "Hrvatski (cr1) - current"
				name = _("{voice} - current").format(voice=name)
			# Translators: location of a voice installed for this NVDA user / for all users
			where = _("system") if v["system"] else _("this user")
			self.installedList.Append((name, self._languageName(v["language"]), v["version"],
				_formatSize(v["size"]), where))
		if self.installed:
			self.installedList.Select(max(0, min(sel, len(self.installed) - 1)))
			self.installedList.Focus(max(0, min(sel, len(self.installed) - 1)))
		self.emptyText.Show(not self.installed)

		sel2 = self.availableList.GetFirstSelected()
		self.availableList.DeleteAllItems()
		installedById = {v["id"]: v for v in self.installed}
		for e in self.available:
			inst = installedById.get(e["id"])
			if not inst:
				# Translators: status of a catalog voice
				status = _("not installed")
			elif inst["version"] and str(e.get("version")) != inst["version"]:
				# Translators: status of a catalog voice
				status = _("update available")
			else:
				# Translators: status of a catalog voice
				status = _("installed")
			self.availableList.Append((self.V.localized(e.get("names", {}), ui),
				self._languageName(e.get("language")), _formatSize(self.V.voiceSize(e)), status))
		if self.available:
			self.availableList.Select(max(0, min(sel2, len(self.available) - 1)))
			self.availableList.Focus(max(0, min(sel2, len(self.available) - 1)))
		if self.catalogSource == "online":
			self.catalogText.SetLabel("")
		elif self.available:
			# Translators: shown when the online voice catalog could not be used
			self.catalogText.SetLabel(_("The online catalog is not available; showing the voices known to this version."))
		else:
			# Translators: shown when there is no voice catalog at all
			self.catalogText.SetLabel(_("No voice catalog is available."))
		self._updateButtons()
		self.Layout()

	def _selectedInstalled(self):
		i = self.installedList.GetFirstSelected()
		return self.installed[i] if 0 <= i < len(self.installed) else None

	def _selectedAvailable(self):
		i = self.availableList.GetFirstSelected()
		return self.available[i] if 0 <= i < len(self.available) else None

	def _updateButtons(self):
		v = self._selectedInstalled()
		self.setCurrentButton.Enable(bool(v) and not self._busy)
		self.updateButton.Enable(bool(v) and not v["system"] and not self._busy)
		self.removeButton.Enable(bool(v) and not v["system"] and not self._busy)
		self.fromFileButton.Enable(not self._busy)
		self.installButton.Enable(bool(self._selectedAvailable()) and not self._busy)

	def _catalogEntry(self, vid):
		for e in self.available:
			if e.get("id") == vid:
				return e
		return None

	def _message(self, text, style=wx.OK | wx.ICON_INFORMATION):
		return gui.messageBox(text, _("MBROLA_NG Voice Manager"), style, self)

	def onClose(self, evt):
		if self._busy:
			return
		VoiceManagerDialog._instance = None
		self.Destroy()

	# ------------------------------------------------------------ actions
	def onSetCurrent(self, evt):
		v = self._selectedInstalled()
		if not v:
			return
		if not _currentSynthIsOurs():
			if not synthDriverHandler.setSynth(SYNTH_NAME):
				# Translators: error message
				self._message(_("MBROLA NG could not be started. See the NVDA log for details."), wx.OK | wx.ICON_ERROR)
				return
		s = synthDriverHandler.getSynth()
		s.voice = v["id"]
		s.saveSettings()
		self.refresh()

	def onRemove(self, evt):
		v = self._selectedInstalled()
		if not v or v["system"]:
			return
		name = self.V.localized(self.V.voiceNames(v), _uiLanguage())
		if self._message(
			# Translators: confirmation before removing a voice
			_("Remove the voice {voice}?").format(voice=name), wx.YES_NO | wx.ICON_QUESTION) != wx.YES:
			return
		ours = _currentSynthIsOurs()
		others = [x for x in self.installed if x["id"] != v["id"]]
		if ours and not others:
			self._message(_(
				# Translators: shown when the only voice of the active synthesizer is to be removed
				"This is the only voice of MBROLA NG, which NVDA is speaking with now. "
				"Choose another synthesizer first, then remove the voice."), wx.OK | wx.ICON_WARNING)
			return
		synth = synthDriverHandler.getSynth() if ours else None
		try:
			if synth:
				synth.suspendVoice()
			self.V.removeVoice(v["id"])
		except Exception as e:
			log.error("MBROLA NG: remove voice", exc_info=True)
			# Translators: error message, {error} is the reason
			self._message(_("The voice could not be removed: {error}").format(error=e), wx.OK | wx.ICON_ERROR)
		finally:
			if synth:
				synth.resumeVoice()
		self.refresh()

	def onCheckUpdates(self, evt):
		v = self._selectedInstalled()
		if not v:
			return
		self._busy = True
		self._updateButtons()

		def work():
			cat, source = self.V.loadCatalog(online=True)
			wx.CallAfter(done, cat, source)

		def done(cat, source):
			self._busy = False
			self._setCatalog(cat, source)
			entry = self._catalogEntry(v["id"])
			name = self.V.localized(self.V.voiceNames(v), _uiLanguage())
			if not entry:
				# Translators: the voice is not in the catalog
				self._message(_("The voice {voice} is not in the voice catalog.").format(voice=name))
			elif v["version"] and str(entry.get("version")) == v["version"]:
				# Translators: no update for the voice
				self._message(_("The voice {voice} is up to date.").format(voice=name))
			elif self._message(
				# Translators: an update exists
				_("Version {version} of {voice} is available. Install it now?").format(
					version=entry.get("version"), voice=name), wx.YES_NO | wx.ICON_QUESTION) == wx.YES:
				self.startInstall(entry)

		threading.Thread(target=work, daemon=True).start()

	def onInstall(self, evt):
		e = self._selectedAvailable()
		if e and not self._busy:
			self.startInstall(e)

	# ------------------------------------------------------ installation
	def startInstall(self, entry):
		"""Download: license first (shown and accepted), then the files."""
		name = self.V.localized(entry.get("names", {}), _uiLanguage())
		self._busy = True
		self._updateButtons()
		# Translators: title of the download progress dialog
		progress = _ProgressDialog(self, _("Installing {voice}").format(voice=name))
		progress.Show()
		state = {"folder": None}

		def download(files, first, total):
			done = first
			for f in files:
				dest = os.path.join(state["folder"], f["name"])
				lastError = None
				for url in f.get("urls", []):
					if not url.lower().startswith("https://"):
						continue
					try:
						with urllib.request.urlopen(url, timeout=30) as r, open(dest, "wb") as out:
							got = 0
							while True:
								if progress.cancelled:
									raise _Cancelled()
								b = r.read(1 << 16)
								if not b:
									break
								out.write(b)
								got += len(b)
								pct = 100.0 * (done + got) / max(1, total)
								# Translators: download status, {done} and {total} are sizes
								wx.CallAfter(progress.update, pct, _("Downloading: {done} of {total}").format(
									done=_formatSize(done + got), total=_formatSize(total)))
						lastError = None
						break
					except _Cancelled:
						raise
					except Exception as e:
						lastError = e
				if lastError is not None or not os.path.isfile(dest):
					raise self.V.VoiceError(_("download failed: {error}").format(error=lastError))
				size = int(f.get("size", 0))
				if size and os.path.getsize(dest) != size:
					# Translators: verification error
					raise self.V.VoiceError(_("{file} has the wrong size").format(file=f["name"]))
				sha = str(f.get("sha256", ""))
				if len(sha) == 64 and self.V.sha256File(dest).lower() != sha.lower():
					# Translators: verification error
					raise self.V.VoiceError(_("{file} is damaged (checksum mismatch)").format(file=f["name"]))
				done += os.path.getsize(dest)

		licName = entry.get("license", {}).get("file")
		files = entry.get("files", [])
		licFiles = [f for f in files if f.get("name") == licName]
		otherFiles = [f for f in files if f.get("name") != licName]
		total = self.V.voiceSize(entry)

		def stage1():
			try:
				state["folder"] = self.V.tempFolder(entry["id"])
				download(licFiles, 0, total)
				text = ""
				if licName:
					with open(os.path.join(state["folder"], licName), "r", encoding="utf-8", errors="replace") as f:
						text = f.read()
				wx.CallAfter(askLicense, text)
			except Exception as e:
				wx.CallAfter(finish, e)

		def askLicense(text):
			if progress.cancelled:
				finish(_Cancelled())
				return
			summary = self.V.localized(entry.get("license", {}).get("summary", {}), _uiLanguage())
			dlg = _LicenseDialog(self, name, (summary + "\r\n\r\n" if summary else "") + text.replace("\n", "\r\n"))
			ok = dlg.ShowModal() == wx.ID_YES
			dlg.Destroy()
			if not ok:
				finish(_Cancelled())
				return
			threading.Thread(target=stage2, daemon=True).start()

		def stage2():
			try:
				first = sum(int(f.get("size", 0)) for f in licFiles)
				download(otherFiles, first, total)
				# Translators: status after the download
				wx.CallAfter(progress.update, 100, _("Verifying..."))
				db = os.path.join(state["folder"], entry["id"])
				self.V.verifyDatabase(db, entry)
				wx.CallAfter(finish, None, self.V.sha256File(db))
			except Exception as e:
				wx.CallAfter(finish, e)

		def finish(error, sha=None):
			progress.Destroy()
			if error is None:
				error = self._installStaged(entry, state["folder"], sha)
			elif state["folder"]:
				shutil.rmtree(state["folder"], ignore_errors=True)
			self._busy = False
			self.refresh()
			if isinstance(error, _Cancelled):
				return
			if error is not None:
				log.warning("MBROLA NG: voice installation failed: %s" % error)
				# Translators: error message, {error} is the reason
				self._message(_("The voice could not be installed: {error}").format(error=error), wx.OK | wx.ICON_ERROR)
				return
			self._afterInstall(entry, name)

		threading.Thread(target=stage1, daemon=True).start()

	def _installStaged(self, entry, folder, sha):
		"""Moves the verified files into place (UI thread). Returns an error or None."""
		ours = _currentSynthIsOurs()
		synth = synthDriverHandler.getSynth() if ours else None
		try:
			if synth and entry["id"] in [v["id"] for v in self.installed]:
				synth.suspendVoice()  # the database file is in use
			self.V.installFolder(entry, folder, sha)
			return None
		except Exception as e:
			log.error("MBROLA NG: install voice", exc_info=True)
			shutil.rmtree(folder, ignore_errors=True)
			return e
		finally:
			if synth:
				synth.resumeVoice()

	def _afterInstall(self, entry, name):
		if _currentSynthIsOurs():
			# Translators: shown after a voice was installed
			self._message(_("The voice {voice} is installed.").format(voice=name))
			return
		if self._message(_(
			# Translators: shown after the first voice was installed
			"The voice {voice} is installed. MBROLA NG is ready.\n\nSwitch to MBROLA NG now?").format(voice=name),
			wx.YES_NO | wx.ICON_QUESTION) == wx.YES:
			if synthDriverHandler.setSynth(SYNTH_NAME):
				s = synthDriverHandler.getSynth()
				s.voice = entry["id"]
				s.saveSettings()
			else:
				self._message(_("MBROLA NG could not be started. See the NVDA log for details."), wx.OK | wx.ICON_ERROR)
		self.refresh()

	def onInstallFromFile(self, evt):
		"""Offline install: the voice database file itself, or a zip with it."""
		with wx.FileDialog(
			self,
			# Translators: title of the file dialog
			_("Choose an MBROLA voice (database file or zip)"),
			# Translators: file type filter in the file dialog
			wildcard=_("MBROLA voices") + " (*.*)|*.*|" + _("Zip archives") + " (*.zip)|*.zip",
			style=wx.FD_OPEN | wx.FD_FILE_MUST_EXIST,
		) as fd:
			if fd.ShowModal() != wx.ID_OK:
				return
			path = fd.GetPath()
		known = {e["id"]: e for e in self.available}
		tmp = tempfile.mkdtemp(prefix="mbrola_ng_")
		try:
			candidates = []
			if zipfile.is_zipfile(path):
				with zipfile.ZipFile(path) as z:
					for info in z.infolist():
						base = os.path.basename(info.filename)
						if base in known or base.lower() == "license.txt":
							target = os.path.join(tmp, base)
							with z.open(info) as src, open(target, "wb") as dst:
								shutil.copyfileobj(src, dst)
							if base in known:
								candidates.append(target)
			elif os.path.basename(path) in known:
				candidates.append(path)
				lic = os.path.join(os.path.dirname(path), "license.txt")
				if os.path.isfile(lic):
					shutil.copy2(lic, os.path.join(tmp, "license.txt"))
			if not candidates:
				# Translators: error of the offline installation
				self._message(_("This file is not a voice known to this version of MBROLA NG ({voices}).").format(
					voices=", ".join(sorted(known)) or "-"), wx.OK | wx.ICON_ERROR)
				return
			db = candidates[0]
			entry = known[os.path.basename(db)]
			name = self.V.localized(entry.get("names", {}), _uiLanguage())
			self.V.verifyDatabase(db, entry)
			licPath = os.path.join(tmp, "license.txt")
			text = ""
			if os.path.isfile(licPath):
				with open(licPath, "r", encoding="utf-8", errors="replace") as f:
					text = f.read()
			summary = self.V.localized(entry.get("license", {}).get("summary", {}), _uiLanguage())
			dlg = _LicenseDialog(self, name, (summary + "\r\n\r\n" if summary else "") + text.replace("\n", "\r\n"))
			ok = dlg.ShowModal() == wx.ID_YES
			dlg.Destroy()
			if not ok:
				return
			folder = self.V.tempFolder(entry["id"])
			shutil.copy2(db, os.path.join(folder, entry["id"]))
			if os.path.isfile(licPath):
				shutil.copy2(licPath, os.path.join(folder, "license.txt"))
			error = self._installStaged(entry, folder, self.V.sha256File(os.path.join(folder, entry["id"])))
			self.refresh()
			if error is not None:
				self._message(_("The voice could not be installed: {error}").format(error=error), wx.OK | wx.ICON_ERROR)
			else:
				self._afterInstall(entry, name)
		except Exception as e:
			log.warning("MBROLA NG: install from file failed", exc_info=True)
			self._message(_("The voice could not be installed: {error}").format(error=e), wx.OK | wx.ICON_ERROR)
		finally:
			shutil.rmtree(tmp, ignore_errors=True)


# ========================================================= global plugin
class GlobalPlugin(globalPluginHandler.GlobalPlugin):
	def __init__(self):
		super().__init__()
		self._menuItem = None
		try:
			tools = gui.mainFrame.sysTrayIcon.toolsMenu
			self._menuItem = tools.Append(
				wx.ID_ANY,
				# Translators: item in the NVDA Tools menu
				_("MBROLA_NG &Voice Manager..."),
				# Translators: help text of the menu item
				_("Download, update and remove MBROLA NG voices"),
			)
			gui.mainFrame.sysTrayIcon.Bind(wx.EVT_MENU, lambda e: VoiceManagerDialog.show(), self._menuItem)
		except Exception:
			log.error("MBROLA NG: cannot add the Voice Manager menu item", exc_info=True)

	def terminate(self):
		try:
			if self._menuItem:
				gui.mainFrame.sysTrayIcon.toolsMenu.Remove(self._menuItem)
		except Exception:
			pass
		super().terminate()

	@script(
		# Translators: description of the command in Input gestures
		description=_("Opens the MBROLA_NG Voice Manager"),
		category="MBROLA NG",
	)
	def script_openVoiceManager(self, gesture):
		wx.CallAfter(VoiceManagerDialog.show)
