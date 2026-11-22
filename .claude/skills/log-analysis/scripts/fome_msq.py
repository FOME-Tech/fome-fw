#!/usr/bin/env python3
"""Read TunerStudio tune files (.msq) without dumping them into context.

  info  MSQ                 firmware signature, git hash, board, write date
  get   MSQ NAME...         values; tables and curves are printed against their axes
  grep  MSQ REGEX           config field names matching a regex, with values (scalars) or shape
  find  MSQ "UI LABEL"      UI label / dialog / menu title -> fields and their values in this tune
  diff  A.msq B.msq         what changed between two tunes

Uses the generated ini (see fome_ini.py) to find table axes and UI labels; the board is taken from the
tune's signature. Standard library only.
"""

import argparse
import os
import re
import sys
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fome_ini  # noqa: E402


class Msq:
	def __init__(self, path):
		self.path = path
		root = ET.parse(path).getroot()
		ns = {"m": root.tag.split("}")[0][1:]} if root.tag.startswith("{") else {}
		pfx = "m:" if ns else ""
		self.version = root.find(f"{pfx}versionInfo", ns)
		self.bib = root.find(f"{pfx}bibliography", ns)
		self.c = {}
		for page in root.findall(f"{pfx}page", ns):
			for el in list(page):
				name = el.get("name")
				if not name:
					continue
				self.c[name] = Const(el)

	@property
	def signature(self):
		return self.version.get("signature", "") if self.version is not None else ""

	@property
	def firmware(self):
		return self.version.get("firmwareInfo", "") if self.version is not None else ""

	def board(self):
		return fome_ini.board_from_signature(self.signature)

	def git_hash(self):
		m = re.search(r"(?:@|%40)([0-9a-f]{7,40})", self.firmware)
		return m.group(1) if m else None


class Const:
	def __init__(self, el):
		self.units = el.get("units", "")
		self.rows = int(float(el.get("rows", "1")))
		self.cols = int(float(el.get("cols", "1")))
		self.digits = el.get("digits")
		text = (el.text or "").strip()
		self.is_array = el.get("rows") is not None or el.get("cols") is not None
		if self.is_array:
			vals = []
			for tok in text.split():
				try:
					vals.append(float(tok))
				except ValueError:
					vals.append(tok)
			self.values = vals
		else:
			self.value = text.strip('"')

	def scalar_str(self):
		v = self.value
		try:
			f = float(v)
			v = str(int(f)) if f == int(f) else f"{f:g}"
		except ValueError:
			v = f'"{v}"'
		return f"{v} {self.units}".strip()

	def grid(self):
		return [self.values[r * self.cols:(r + 1) * self.cols] for r in range(self.rows)]


def num(v):
	if isinstance(v, float):
		return str(int(v)) if v == int(v) else f"{v:g}"
	return str(v)


def print_table(rows):
	w = [max(len(r[k]) for r in rows) for k in range(len(rows[0]))]
	for r in rows:
		print("  " + "  ".join(c.rjust(w[k]) for k, c in enumerate(r)))


def show(msq, ini, name):
	c = msq.c.get(name)
	if c is None:
		cands = [n for n in msq.c if name.lower() in n.lower()]
		print(f"{name}: not in tune" + (f" (similar: {', '.join(cands[:15])})" if cands else ""))
		return
	if not c.is_array:
		print(f"{name} = {c.scalar_str()}")
		return
	ed = ini.editor_for(name) if ini else None
	title = f'  [{ed["kind"]} "{ed["title"]}"]' if ed else ""
	if not ed and ini:
		uses = [f'{k[0]} axis of {e["kind"]} "{e["title"]}" ({e.get("zBins") or e.get("yBins")})'
				for e in ini.editors() for k in ("xBins", "yBins") if e.get(k) == name]
		if uses:
			title = "  [" + "; ".join(uses) + "]"
	print(f"{name}  {c.rows}x{c.cols} {c.units}{title}")
	if ed and ed["kind"] == "table" and ed.get("xBins") in msq.c and ed.get("yBins") in msq.c:
		xb = msq.c[ed["xBins"]].values
		yb = msq.c[ed["yBins"]].values
		g = c.grid()
		print(f"  rows = {ed['yBins']} ({msq.c[ed['yBins']].units}), cols = {ed['xBins']} ({msq.c[ed['xBins']].units});"
			  f" highest row printed first, as TunerStudio shows it")
		rows = [[f"{ed['yBins']}\\{ed['xBins']}"] + [num(x) for x in xb]]
		for r in reversed(range(len(g))):
			rows.append([num(yb[r]) if r < len(yb) else "?"] + [num(v) for v in g[r]])
		print_table(rows)
	elif ed and ed["kind"] == "curve" and ed.get("xBins") in msq.c:
		xb = msq.c[ed["xBins"]].values
		rows = [[ed["xBins"], name]] + [[num(x), num(y)] for x, y in zip(xb, c.values)]
		print_table(rows)
	elif c.cols > 1 and c.rows > 1:
		print_table([[num(v) for v in r] for r in c.grid()])
	else:
		print("  " + " ".join(num(v) for v in c.values))


def load_ini(msq, args):
	try:
		return fome_ini.Ini(fome_ini.find_ini(args.board or msq.board(), args.ini, quiet=True))
	except SystemExit as e:
		print(f"(no ini: {e}; tables shown without axes)", file=sys.stderr)
		return None


def cmd_info(args):
	m = Msq(args.msq)
	print(f"file:       {args.msq}")
	print(f"signature:  {m.signature}")
	print(f"firmware:   {m.firmware}")
	if m.bib is not None:
		print(f"written:    {m.bib.get('writeDate', '')} by {m.bib.get('author', '')}")
	print(f"board:      {m.board()}")
	h = m.git_hash()
	if h:
		print(f"git hash:   {h}   (try `git log -1 {h}`; `git diff {h} HEAD -- <file>` for what changed since)")
	print(f"constants:  {len(m.c)}")


def cmd_get(args):
	m = Msq(args.msq)
	ini = load_ini(m, args)
	for n in args.names:
		show(m, ini, n)


def cmd_grep(args):
	m = Msq(args.msq)
	rx = re.compile(args.regex, re.I)
	for n, c in m.c.items():
		if rx.search(n):
			print(f"{n} = {c.scalar_str()}" if not c.is_array else f"{n}  [{c.rows}x{c.cols} {c.units}]")


def cmd_find(args):
	m = Msq(args.msq)
	ini = load_ini(m, args)
	if ini is None:
		raise SystemExit("find needs the generated ini")
	fields = []
	hits = fome_ini.match(ini.dialogs(), args.label, key=lambda d: d[3])
	for d in hits:
		if d[4]:
			fields.append((d[3], d[4], d[2] or d[1], d[5]))
	# dialog / menu titles -> every field in that dialog (recursing into panels)
	titles = {}
	for d in ini.dialogs():
		titles.setdefault(d[1], d[2])
	for n, s in ini.sections.get("Menu", []):
		mm = re.match(r'^(?:sub)?[Mm]enu\s*=\s*(\w+)\s*,\s*"([^"]*)"', s)
		if mm and mm.group(1) in titles:
			titles[mm.group(1)] = titles[mm.group(1)] + " | " + mm.group(2).replace("&", "")

	def collect(did, depth=0):
		for d in ini.dialogs():
			if d[1] != did:
				continue
			if d[3].startswith("[panel ") and depth < 4:
				collect(d[3][7:-1], depth + 1)
			elif d[4]:
				fields.append((d[3], d[4], d[2] or d[1], d[5]))

	for did, t in fome_ini.match(list(titles.items()), args.label, key=lambda kv: kv[1]):
		collect(did)
	seen = set()
	for label, field, dtitle, cond in fields:
		if field in seen:
			continue
		seen.add(field)
		c = m.c.get(field)
		val = (c.scalar_str() if not c.is_array else f"[{c.rows}x{c.cols} array - use get]") if c else "(not in tune)"
		print(f'"{label}" ({dtitle}): {field} = {val}' + (f"   when {{{cond}}}" if cond else ""))
	for e in fome_ini.match(ini.editors(), args.label, key=lambda e: e["title"]):
		show(m, ini, e.get("zBins") or e.get("yBins"))
	if not fields:
		print(f"no UI label or dialog like {args.label!r} (try fome_msq.py grep)")


def cmd_diff(args):
	a, b = Msq(args.a), Msq(args.b)
	ini = load_ini(b, args)
	for n in sorted(set(a.c) | set(b.c)):
		ca, cb = a.c.get(n), b.c.get(n)
		if ca is None or cb is None:
			print(f"{n}: only in {'B' if ca is None else 'A'}")
			continue
		if not ca.is_array:
			if ca.value != cb.value:
				print(f"{n}: {ca.scalar_str()} -> {cb.scalar_str()}")
			continue
		if ca.values == cb.values:
			continue
		if len(ca.values) != len(cb.values):
			print(f"{n}: shape {ca.rows}x{ca.cols} -> {cb.rows}x{cb.cols}")
			continue
		changed = [(i, x, y) for i, (x, y) in enumerate(zip(ca.values, cb.values)) if x != y]
		print(f"{n}: {len(changed)}/{len(ca.values)} cells changed")
		if args.full:
			print("  A:")
			show(a, ini, n)
			print("  B:")
			show(b, ini, n)
		else:
			for i, x, y in changed[:8]:
				print(f"  [{i // ca.cols},{i % ca.cols}] {num(x)} -> {num(y)}")
			if len(changed) > 8:
				print(f"  ... (--full prints both tables)")


def main():
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	common = argparse.ArgumentParser(add_help=False)
	common.add_argument("--board", help="override board (default: from the tune's signature)")
	common.add_argument("--ini", help="explicit ini path")
	sub = ap.add_subparsers(dest="cmd", required=True)
	p = sub.add_parser("info", parents=[common])
	p.add_argument("msq")
	p.set_defaults(fn=cmd_info)
	p = sub.add_parser("get", parents=[common])
	p.add_argument("msq")
	p.add_argument("names", nargs="+")
	p.set_defaults(fn=cmd_get)
	p = sub.add_parser("grep", parents=[common])
	p.add_argument("msq")
	p.add_argument("regex")
	p.set_defaults(fn=cmd_grep)
	p = sub.add_parser("find", parents=[common])
	p.add_argument("msq")
	p.add_argument("label")
	p.set_defaults(fn=cmd_find)
	p = sub.add_parser("diff", parents=[common])
	p.add_argument("a")
	p.add_argument("b")
	p.add_argument("--full", action="store_true")
	p.set_defaults(fn=cmd_diff)
	args = ap.parse_args()
	args.fn(args)


if __name__ == "__main__":
	main()
