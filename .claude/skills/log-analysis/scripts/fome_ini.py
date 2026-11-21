#!/usr/bin/env python3
"""Map between the names people see and the names in the code, via the generated TunerStudio ini.

  label   TEXT   UI label in a TunerStudio dialog ("TPS threshold")   -> config field, dialog, tooltip
  field   NAME   config field (idlePidDeactivationTpsThreshold)       -> definition, tooltip, UI, code refs
  logcol  TEXT   datalog column ("Idle: Phase")                       -> output channel, struct field, code refs
  table   NAME   table/curve id, title or bins name                   -> its x/y/z bins

TEXT is matched exactly (case-insensitive) first, then as a case-insensitive regex.

The ini is firmware/tunerstudio/generated/fome_<board>.ini from the *current* checkout (built by
`make`). If the user's firmware is older, layouts can differ; use --rev to grep code at their commit.
Standard library only.
"""

import argparse
import glob
import os
import re
import subprocess
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))
GEN = os.path.join(REPO, "firmware", "tunerstudio", "generated")


def find_ini(board=None, ini=None, quiet=False):
	if ini:
		return ini
	if board:
		p = os.path.join(GEN, f"fome_{board}.ini")
		if os.path.exists(p):
			return p
		print(f"warning: no generated ini for board {board!r}; falling back", file=sys.stderr)
	cands = sorted(glob.glob(os.path.join(GEN, "fome_*.ini")), key=os.path.getmtime, reverse=True)
	if not cands:
		raise SystemExit(f"no generated ini in {GEN} - build firmware once (`make` in firmware/) to generate it")
	if not quiet:
		print(f"(using {os.path.relpath(cands[0], REPO)}; pass --board/--ini to choose)", file=sys.stderr)
	return cands[0]


def board_from_signature(sig):
	"""'rusEFI (FOME) master.2026.11.10.atlas.407696980' -> 'atlas'"""
	m = re.search(r"\.\d{4}\.\d{2}\.\d{2}\.([A-Za-z0-9_\-]+)\.\d+", sig or "")
	return m.group(1) if m else None


class Ini:
	def __init__(self, path):
		self.path = path
		self.sections = {}
		cur = None
		with open(path, encoding="latin-1") as f:
			for n, raw in enumerate(f, 1):
				line = raw.rstrip("\n")
				s = line.strip()
				m = re.match(r"^\[([^\]]+)\]", s)
				if m:
					cur = m.group(1)
					self.sections.setdefault(cur, [])
					continue
				if cur and s and not s.startswith(";"):
					self.sections[cur].append((n, s))
		self._dialogs = None

	# --- [Constants] / [OutputChannels] ------------------------------------------------------
	def definition(self, section, name):
		for n, s in self.sections.get(section, []):
			m = re.match(r"^(\w+)\s*=", s)
			if m and m.group(1) == name:
				return n, s
		return None

	def tooltip(self, name):
		d = self.definition("SettingContextHelp", name)
		if d:
			return d[1].split("=", 1)[1].strip().strip('"')
		return None

	# --- [UserDefined] dialogs -------------------------------------------------------------------
	def dialogs(self):
		"""List of (line, dialog id, dialog title, label, field name, condition)."""
		if self._dialogs is not None:
			return self._dialogs
		out = []
		did, dtitle = None, None
		for n, s in self.sections.get("UserDefined", []):
			m = re.match(r'^dialog\s*=\s*(\w+)\s*,\s*"([^"]*)"', s)
			if m:
				did, dtitle = m.group(1), m.group(2)
				continue
			m = re.match(r'^field\s*=\s*"([^"]*)"\s*(?:,\s*(\w+))?\s*(?:,\s*\{(.*)\})?', s)
			if m:
				out.append((n, did, dtitle, m.group(1), m.group(2), m.group(3)))
				continue
			m = re.match(r'^panel\s*=\s*(\w+)', s)
			if m:
				out.append((n, did, dtitle, f"[panel {m.group(1)}]", None, None))
		self._dialogs = out
		return out

	def parents(self, dialog_id):
		"""Dialogs that embed this one as a panel, to show where a dialog lives."""
		return sorted({d[1] for d in self.dialogs() if d[3] == f"[panel {dialog_id}]"})

	def menu_titles(self, dialog_id):
		out = []
		for n, s in self.sections.get("Menu", []):
			m = re.match(r'^(?:sub)?[Mm]enu\s*=\s*(\w+)\s*,\s*"([^"]*)"', s)
			if m and m.group(1) == dialog_id:
				out.append(m.group(2).replace("&", ""))
		return out

	# --- tables / curves -------------------------------------------------------------------
	def editors(self):
		"""List of dicts: kind, id, title, xBins, yBins, zBins (names only)."""
		out = []
		for section, kind in (("TableEditor", "table"), ("CurveEditor", "curve")):
			cur = None
			for n, s in self.sections.get(section, []):
				m = re.match(r'^(table|curve)\s*=\s*(\w+)\s*,\s*(?:(\w+)\s*,\s*)?"([^"]*)"', s)
				if m:
					cur = {"kind": kind, "id": m.group(2), "title": m.group(4), "line": n}
					out.append(cur)
					continue
				m = re.match(r'^([xyz]Bins)\s*=\s*(\w+)', s)
				if m and cur is not None:
					cur[m.group(1)] = m.group(2)
		return out

	def editor_for(self, name):
		for e in self.editors():
			if name in (e.get("zBins"), e["id"]) or (e["kind"] == "curve" and e.get("yBins") == name):
				return e
		return None

	# --- [Datalog] ---------------------------------------------------------------------------
	def datalog(self):
		out = []
		for n, s in self.sections.get("Datalog", []):
			m = re.match(r'^entry\s*=\s*(\w+)\s*,\s*"([^"]*)"', s)
			if m:
				out.append((n, m.group(1), m.group(2)))
		return out


def match(items, text, key):
	low = text.lower()
	exact = [i for i in items if key(i) and key(i).lower() == low]
	if exact:
		return exact
	rx = re.compile(text, re.I)
	return [i for i in items if key(i) and rx.search(key(i))]


def git_grep(name, rev=None, limit=25):
	cmd = ["git", "-C", REPO, "grep", "-n", "-w", name]
	if rev:
		cmd.append(rev)
	cmd += ["--", "firmware", "unit_tests", ":!firmware/generated", ":!firmware/ext", ":!firmware/tunerstudio",
			":!*.ini", ":!firmware/CHANGELOG.md", ":!*_generated.h", ":!*.txt"]
	r = subprocess.run(cmd, capture_output=True, text=True)
	lines = [l for l in r.stdout.splitlines() if l]
	if rev:
		lines = [l.split(":", 1)[1] if l.startswith(rev + ":") else l for l in lines]
	code = [l for l in lines if not l.startswith("unit_tests/")]
	tests = [l for l in lines if l.startswith("unit_tests/")]
	return code[:limit], len(code), len(tests)


def livedata_txt_files():
	"""Definitions of live data structs: output_channels.txt plus every struct listed in LiveData.yaml."""
	files = [os.path.join(REPO, "firmware", "console", "binary", "output_channels.txt")]
	ymlp = os.path.join(REPO, "firmware", "integration", "LiveData.yaml")
	if os.path.exists(ymlp):
		name = None
		for line in open(ymlp):
			m = re.match(r"\s*-\s*name:\s*(\S+)", line)
			if m:
				name = m.group(1)
			m = re.match(r"\s*folder:\s*(\S+)", line)
			if m and name:
				p = os.path.join(REPO, "firmware", m.group(1), name + ".txt")
				if os.path.exists(p) and p not in files:
					files.append(p)
	return files


def find_struct_field(member):
	hits = []
	rx = re.compile(r"\b" + re.escape(member) + r"\b")
	for p in livedata_txt_files():
		for n, line in enumerate(open(p, encoding="latin-1"), 1):
			if rx.search(line) and not line.strip().startswith("!"):
				hits.append(f"{os.path.relpath(p, REPO)}:{n}: {line.strip()}")
	return list(dict.fromkeys(hits))


def show_refs(name, rev, near=None):
	code, ncode, ntests = git_grep(name, rev)
	if near:
		# refs next to the struct definition are usually the ones that matter
		code.sort(key=lambda l: not l.startswith(near))
	where = f" at {rev}" if rev else ""
	print(f"  code refs{where}: {ncode} in firmware, {ntests} in unit_tests")
	for l in code:
		print(f"    {l}")
	if ncode > len(code):
		print(f"    ... {ncode - len(code)} more (git grep -nw {name})")


# ---------------------------------------------------------------------------------------------


def print_dialog(ini, did, indent, depth=0):
	for n, d, dtitle, label, field, cond in ini.dialogs():
		if d != did:
			continue
		if label.startswith("[panel ") and depth < 4:
			sub = label[7:-1]
			subtitle = next((x[2] for x in ini.dialogs() if x[1] == sub), "")
			print(f'{indent}panel {sub} "{subtitle}"')
			print_dialog(ini, sub, indent + "  ", depth + 1)
		elif field:
			print(f'{indent}"{label}" = {field}' + (f"   when {{{cond}}}" if cond else ""))


def cmd_label(ini, args):
	hits = match(ini.dialogs(), args.text, key=lambda d: d[3])
	for n, did, dtitle, label, field, cond in hits[:args.limit]:
		where = f'dialog {did} "{dtitle}"'
		menus = ini.menu_titles(did)
		if not menus:
			for p in ini.parents(did):
				menus += [f"{t} (via {p})" for t in ini.menu_titles(p)] or [f"panel of {p}"]
		print(f'"{label}" -> {field or "(no field: heading/text)"}')
		print(f"  in {where}" + (f"; menu: {', '.join(menus)}" if menus else ""))
		if cond:
			print(f"  visible/enabled when: {{{cond}}}")
		if field:
			tip = ini.tooltip(field)
			if tip:
				print(f"  tooltip: {tip}")
	dtitles = {}
	for d in ini.dialogs():
		dtitles.setdefault(d[1], d[2])
	for n, s in ini.sections.get("Menu", []):
		m = re.match(r'^(?:sub)?[Mm]enu\s*=\s*(\w+)\s*,\s*"([^"]*)"', s)
		if m and m.group(1) in dtitles:
			dtitles[m.group(1)] = f'{dtitles[m.group(1)]}" / menu "{m.group(2).replace("&", "")}'
	dhits = match(list(dtitles.items()), args.text, key=lambda kv: kv[1])
	for did, title in dhits[:args.limit]:
		print(f'dialog {did} "{title}":')
		print_dialog(ini, did, "  ")
	thits = match(ini.editors(), args.text, key=lambda e: e["title"])
	for e in thits[:args.limit]:
		print(f'{e["kind"]} "{e["title"]}" ({e["id"]}): ' +
			  ", ".join(f"{k}={e[k]}" for k in ("xBins", "yBins", "zBins") if k in e))
	if not hits and not thits and not dhits:
		print(f"no dialog field or table titled like {args.text!r}")


def cmd_field(ini, args):
	name = args.name
	d = ini.definition("Constants", name)
	if not d:
		cands = [s.split("=")[0].strip() for _, s in ini.sections.get("Constants", [])
				 if re.search(name, s.split("=")[0], re.I)]
		raise SystemExit(f"no config field {name!r}" + (f"; similar: {', '.join(cands[:20])}" if cands else ""))
	print(f"{name}")
	print(f"  ini: {d[1]}")
	tip = ini.tooltip(name)
	if tip:
		print(f"  tooltip: {tip}")
	for n, did, dtitle, label, field, cond in ini.dialogs():
		if field == name:
			print(f'  UI: "{label}" in dialog {did} "{dtitle}"' + (f" when {{{cond}}}" if cond else ""))
	for e in ini.editors():
		for k in ("xBins", "yBins", "zBins"):
			if e.get(k) == name:
				print(f'  {e["kind"]} "{e["title"]}" ({e["id"]}) uses it as {k}')
	cfg = os.path.join(REPO, "firmware", "integration", "fome_config.txt")
	rx = re.compile(r"\b" + re.escape(name) + r"\b")
	for n, line in enumerate(open(cfg, encoding="latin-1"), 1):
		if rx.search(line):
			print(f"  fome_config.txt:{n}: {line.strip()[:200]}")
	show_refs(name, args.rev)


def cmd_logcol(ini, args):
	hits = match(ini.datalog(), args.text, key=lambda e: e[2])
	for n, ch, label in hits[:args.limit]:
		print(f'"{label}" -> output channel {ch}')
		d = ini.definition("OutputChannels", ch)
		if d:
			print(f"  ini: {d[1]}")
			if "{" in d[1]:
				print("  (computed in the ini from other channels - the ECU does not send this value directly)")
		# Channel names are <output_name>_<member> for prefixed structs; try progressively shorter suffixes
		parts = ch.split("_")
		member = ch
		defs = []
		for k in range(len(parts)):
			cand = "_".join(parts[k:])
			defs = find_struct_field(cand)
			if defs:
				member = cand
				break
		for l in defs[:5]:
			print(f"  defined: {l}")
		if defs:
			show_refs(member, args.rev, near=os.path.dirname(defs[0].split(":")[0]))
		elif not d or "{" not in d[1]:
			print("  (struct field not found; grep for the channel name)")
	if not hits:
		print(f"no datalog column labelled like {args.text!r}")


def cmd_table(ini, args):
	es = [e for e in ini.editors() if args.name in (e["id"], e.get("xBins"), e.get("yBins"), e.get("zBins"))]
	es += [e for e in match(ini.editors(), args.name, key=lambda e: e["title"]) if e not in es] if not es else []
	for e in es[:args.limit]:
		print(f'{e["kind"]} "{e["title"]}" ({e["id"]}): ' +
			  ", ".join(f"{k}={e[k]}" for k in ("xBins", "yBins", "zBins") if k in e))
	if not es:
		print(f"no table or curve matching {args.name!r}")


def main():
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("--board", help="board name, e.g. atlas, proteus_f7 (picks fome_<board>.ini)")
	ap.add_argument("--ini", help="explicit ini path")
	ap.add_argument("--rev", help="git revision to grep code at (the user's firmware hash)")
	ap.add_argument("--limit", type=int, default=15)
	common = argparse.ArgumentParser(add_help=False)
	common.add_argument("--board", default=argparse.SUPPRESS)
	common.add_argument("--ini", default=argparse.SUPPRESS)
	common.add_argument("--rev", default=argparse.SUPPRESS)
	common.add_argument("--limit", type=int, default=argparse.SUPPRESS)
	sub = ap.add_subparsers(dest="cmd", required=True)
	p = sub.add_parser("label", parents=[common])
	p.add_argument("text")
	p.set_defaults(fn=cmd_label)
	p = sub.add_parser("field", parents=[common])
	p.add_argument("name")
	p.set_defaults(fn=cmd_field)
	p = sub.add_parser("logcol", parents=[common])
	p.add_argument("text")
	p.set_defaults(fn=cmd_logcol)
	p = sub.add_parser("table", parents=[common])
	p.add_argument("name")
	p.set_defaults(fn=cmd_table)
	args = ap.parse_args()
	ini = Ini(find_ini(args.board, args.ini))
	args.fn(ini, args)


if __name__ == "__main__":
	main()
