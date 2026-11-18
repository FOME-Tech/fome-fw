#!/usr/bin/env python3
"""Inspect FOME / TunerStudio datalogs without loading them into context.

Reads TunerStudio text logs (.csv/.msl, tab or comma separated, with optional quoted preamble, units
row and MARK lines) and binary MLVLG logs (.mlg, v1 and v2, as written by TunerStudio and by FOME's
SD card logger). Standard library only.

Column arguments are resolved exactly, then case-insensitively, then as a unique case-insensitive
substring. Anywhere a column is accepted you can also pass an expression; refer to columns as
{Name With Spaces} or as bare identifiers when the name is one, e.g.

    fome_log.py hist log.csv "{instantRpm} / {RPM}" --where "{RPM} > 500"

Run with -h, or `<command> -h`, for usage.
"""

import argparse
import math
import re
import struct
import sys

NAN = float("nan")
_WORDS = {"on": 1.0, "off": 0.0, "true": 1.0, "false": 0.0, "yes": 1.0, "no": 0.0, "active": 1.0,
		  "inactive": 0.0, "high": 1.0, "low": 0.0}

# ---------------------------------------------------------------------------------------------
# Loading


class Log:
	def __init__(self, path):
		self.path = path
		self.preamble = []  # informational header lines (firmware signature, capture date, ...)
		self.names = []
		self.units = []
		self.markers = []  # (row index, text)
		self.kind = None
		self._cache = {}
		with open(path, "rb") as f:
			data = f.read()
		if data[:5] == b"MLVLG":
			self._load_mlg(data)
		else:
			self._load_text(data.decode("latin-1"))
		self._synth_time = False
		if self.time_index() is None:
			self._synth_time = True

	# --- text -------------------------------------------------------------------------------
	def _load_text(self, text):
		self.kind = "text"
		lines = text.splitlines()
		i = 0
		while i < len(lines) and (lines[i].startswith('"') or lines[i].startswith("#") or not lines[i].strip()):
			s = lines[i].strip().strip('"')
			if s and s != "#":
				self.preamble.append(s)
			i += 1
		if i >= len(lines):
			raise SystemExit(f"{self.path}: no header row found")
		header = lines[i]
		self.sep = "\t" if "\t" in header else ","
		self.names = [n.strip() for n in header.split(self.sep)]
		i += 1
		self.units = [""] * len(self.names)
		if i < len(lines):
			first = lines[i].split(self.sep)[0].strip()
			if not _is_number(first):
				self.units = [u.strip() for u in lines[i].split(self.sep)]
				self.units += [""] * (len(self.names) - len(self.units))
				i += 1
		self._lines = []
		for line in lines[i:]:
			if not line.strip():
				continue
			if line.startswith("MARK"):
				self.markers.append((len(self._lines), line.strip()))
				continue
			self._lines.append(line)
		self.nrows = len(self._lines)

	def _extract_text(self, idxs):
		out = [[] for _ in idxs]
		sep = self.sep
		for line in self._lines:
			parts = line.split(sep)
			for k, ci in enumerate(idxs):
				try:
					out[k].append(float(parts[ci]))
				except ValueError:
					# TunerStudio writes bit fields as words: On/Off, True/False, ...
					out[k].append(_WORDS.get(parts[ci].strip().lower(), NAN))
				except IndexError:
					out[k].append(NAN)
		return out

	# --- mlg --------------------------------------------------------------------------------
	_SCALAR = {0: "B", 1: "b", 2: "H", 3: "h", 4: "I", 5: "i", 6: "q", 7: "f", 10: "B", 11: "H", 12: "I"}

	def _load_mlg(self, data):
		self.kind = "mlg"
		version = struct.unpack_from(">H", data, 6)[0]
		if version >= 2:
			info_start, data_begin, rec_len, nfields = struct.unpack_from(">IIHH", data, 12)
			off, fsize = 24, 89
		else:
			info_start, data_begin, rec_len, nfields = struct.unpack_from(">HIHH", data, 12)
			off, fsize = 22, 55
		self.mlg_version = version
		fields = []
		pos = 0
		for n in range(nfields):
			b = off + n * fsize
			ftype = data[b]
			name = data[b + 1:b + 35].split(b"\0")[0].decode("latin-1").strip()
			units = data[b + 35:b + 45].split(b"\0")[0].decode("latin-1").strip()
			if ftype in (10, 11, 12):
				scale, transform = 1.0, 0.0
			else:
				scale, transform = struct.unpack_from(">ff", data, b + 46)
			fmt = ">" + self._SCALAR[ftype]
			fields.append((fmt, pos, scale, transform))
			pos += struct.calcsize(fmt)
			self.names.append(name)
			self.units.append(units)
		if pos != rec_len:
			print(f"warning: field sizes sum to {pos}, header says record length {rec_len}", file=sys.stderr)
		if info_start:
			end = data.find(b"\0", info_start)
			info = data[info_start:end if end > 0 else data_begin].decode("latin-1", "replace").strip()
			if info:
				self.preamble.extend(l.strip().strip('"') for l in info.splitlines() if l.strip())
		self._fields = fields
		self._data = data
		self._recs = []  # byte offset of each record's field data
		self._blockts = []  # raw 10us block timestamps
		p = data_begin
		while p < len(data):
			btype = data[p]
			if btype == 0:
				if p + 4 + rec_len + 1 > len(data):
					break
				self._blockts.append(struct.unpack_from(">H", data, p + 2)[0])
				self._recs.append(p + 4)
				p += 4 + rec_len + 1
			elif btype == 1:
				msg = data[p + 4:p + 54].split(b"\0")[0].decode("latin-1", "replace")
				self.markers.append((len(self._recs), msg))
				p += 54
			else:
				print(f"warning: unknown block type {btype} at offset {p}, stopping", file=sys.stderr)
				break
		self.nrows = len(self._recs)

	def _extract_mlg(self, idxs):
		out = []
		data = self._data
		for ci in idxs:
			fmt, pos, scale, transform = self._fields[ci]
			col = []
			for r in self._recs:
				col.append((struct.unpack_from(fmt, data, r + pos)[0] + transform) * scale)
			out.append(col)
		return out

	# --- columns ----------------------------------------------------------------------------
	def time_index(self):
		for i, n in enumerate(self.names):
			if n.lower() == "time":
				return i
		return None

	def column(self, idx):
		return self.columns([idx])[0]

	def columns(self, idxs):
		need = [i for i in idxs if i not in self._cache and i != -1]
		if need:
			cols = self._extract_text(need) if self.kind == "text" else self._extract_mlg(need)
			for i, c in zip(need, cols):
				self._cache[i] = c
		if -1 in idxs and -1 not in self._cache:
			self._cache[-1] = self._synthesize_time()
		return [self._cache[i] for i in idxs]

	def _synthesize_time(self):
		if self.kind == "mlg":
			t, acc, last = [], 0, None
			for ts in self._blockts:
				if last is not None:
					acc += (ts - last) & 0xFFFF
				last = ts
				t.append(acc * 1e-5)
			return t
		return [float(i) for i in range(self.nrows)]

	def time(self):
		ti = self.time_index()
		return self.column(-1 if ti is None else ti)

	def resolve(self, name):
		if name in self.names:
			return self.names.index(name)
		low = name.lower()
		exact = [i for i, n in enumerate(self.names) if n.lower() == low]
		if len(exact) == 1:
			return exact[0]
		sub = [i for i, n in enumerate(self.names) if low in n.lower()]
		if len(sub) == 1:
			return sub[0]
		if not sub:
			raise KeyError(f"no column matches {name!r} (try: fome_log.py cols LOG <regex>)")
		cands = ", ".join(repr(self.names[i]) for i in sub[:15])
		raise KeyError(f"{name!r} is ambiguous: {cands}{' ...' if len(sub) > 15 else ''}")


def _is_number(s):
	try:
		float(s)
		return True
	except ValueError:
		return False


# ---------------------------------------------------------------------------------------------
# Expressions

_SAFE = {k: getattr(math, k) for k in ("sqrt", "log", "log10", "exp", "floor", "ceil", "fabs", "isnan")}
_SAFE.update(abs=abs, min=min, max=max, round=round, int=int, float=float)
_IDENT = re.compile(r"(?<![\w.])[A-Za-z_][A-Za-z0-9_]*")


class Expr:
	"""A column name or an expression over columns. evaluate() returns a list aligned with rows."""

	def __init__(self, log, text):
		self.text = text
		self.log = log
		self.units = ""
		try:
			i = log.resolve(text)
			self.name = log.names[i]
			self.units = log.units[i] if i < len(log.units) else ""
			self.refs = [i]
			self.code = None
			return
		except KeyError as e:
			if not re.search(r"[{}<>=!+\-*/()%&|]", text):
				raise SystemExit(e.args[0])
		self.name = text
		self.refs = []

		def ref(name):
			try:
				idx = log.resolve(name)
			except KeyError as e:
				raise SystemExit(e.args[0])
			if idx not in self.refs:
				self.refs.append(idx)
			return f"_c[{self.refs.index(idx)}]"

		src = re.sub(r"\{([^}]*)\}", lambda m: ref(m.group(1)), text)

		def bare(m):
			w = m.group(0)
			if w in _SAFE or w in ("and", "or", "not", "in", "if", "else", "True", "False", "_c"):
				return w
			return ref(w)

		# Substitute bare identifiers outside of the _c[...] we already produced
		src = re.sub(r"_c\[\d+\]|" + _IDENT.pattern, lambda m: m.group(0) if m.group(0).startswith("_c[") else bare(m), src)
		src = src.replace("&&", " and ").replace("||", " or ")
		try:
			self.code = compile(src, "<expr>", "eval")
		except SyntaxError as e:
			raise SystemExit(f"bad expression {text!r}: {e}")

	def evaluate(self):
		cols = self.log.columns(self.refs)
		if self.code is None:
			return cols[0]
		out = []
		g = {"__builtins__": {}, **_SAFE}
		for row in zip(*cols):
			try:
				v = eval(self.code, g, {"_c": row})
				out.append(float(v))
			except (ZeroDivisionError, ValueError, OverflowError, TypeError):
				out.append(NAN)
		return out


# ---------------------------------------------------------------------------------------------
# Row selection


def select_rows(log, args):
	"""Row indices matching --from/--to/--rows/--where."""
	n = log.nrows
	lo, hi = 0, n
	if getattr(args, "rows", None):
		a, _, b = args.rows.partition(":")
		lo = int(a) if a else 0
		hi = int(b) if b else n
	idx = range(lo, min(hi, n))
	t = log.time()
	if getattr(args, "t_from", None) is not None:
		idx = [i for i in idx if t[i] >= args.t_from]
	if getattr(args, "t_to", None) is not None:
		idx = [i for i in idx if t[i] <= args.t_to]
	if getattr(args, "where", None):
		w = Expr(log, args.where).evaluate()
		idx = [i for i in idx if w[i] and not math.isnan(w[i])]
	return list(idx)


def fmt(v):
	if v is None or (isinstance(v, float) and math.isnan(v)):
		return "-"
	if v == int(v) and abs(v) < 1e9:
		return str(int(v))
	a = abs(v)
	if a >= 100:
		return f"{v:.1f}"
	if a >= 1:
		return f"{v:.3f}".rstrip("0").rstrip(".")
	return f"{v:.4g}"


def print_table(headers, rows, out=sys.stdout):
	widths = [len(h) for h in headers]
	for r in rows:
		for k, c in enumerate(r):
			widths[k] = max(widths[k], len(c))
	print("  ".join(h.rjust(w) for h, w in zip(headers, widths)), file=out)
	for r in rows:
		print("  ".join(c.rjust(w) for c, w in zip(r, widths)), file=out)


def percentile(sorted_vals, p):
	if not sorted_vals:
		return NAN
	k = (len(sorted_vals) - 1) * p
	f = math.floor(k)
	c = min(f + 1, len(sorted_vals) - 1)
	return sorted_vals[f] + (sorted_vals[c] - sorted_vals[f]) * (k - f)


# ---------------------------------------------------------------------------------------------
# Commands


def cmd_info(log, args):
	print(f"file:      {log.path}")
	print(f"format:    {'MLVLG v%d' % log.mlg_version if log.kind == 'mlg' else 'text (sep=%r)' % log.sep}")
	for p in log.preamble:
		print(f"preamble:  {p}")
	m = re.search(r"@([0-9a-f]{7,40})", " ".join(log.preamble))
	if m:
		print(f"git hash:  {m.group(1)}   (firmware build; try `git log -1 {m.group(1)}`)")
	print(f"columns:   {len(log.names)}")
	print(f"rows:      {log.nrows}")
	t = log.time()
	if log.nrows:
		dur = t[-1] - t[0]
		src = "synthesized from MLG block timestamps" if log._synth_time and log.kind == "mlg" else (
			"row index (no Time column)" if log._synth_time else "Time column")
		print(f"time:      {fmt(t[0])} .. {fmt(t[-1])} s ({fmt(dur)} s, from {src})")
		if dur > 0:
			print(f"rate:      ~{fmt(log.nrows / dur)} samples/s average")
		steps = sorted({round(b - a, 6) for a, b in zip(t, t[1:]) if b > a})
		if steps and steps[0] >= 0.5:
			print(f"WARNING:   Time resolution is {fmt(steps[0])} s; several rows share a timestamp. "
				  f"Use --rows A:B (row indices) for fine-grained work.")
	if log.markers:
		print(f"markers:   {len(log.markers)}")
		for r, txt in log.markers[:20]:
			print(f"  row {r} (t={fmt(t[min(r, log.nrows - 1)]) if log.nrows else '-'}): {txt}")


def cmd_cols(log, args):
	pats = [re.compile(p, re.I) for p in args.patterns]
	idxs = [i for i, n in enumerate(log.names) if not pats or any(p.search(n) for p in pats)]
	rows = []
	cols = log.columns(idxs) if idxs else []
	for i, c in zip(idxs, cols):
		vals = [v for v in c if not math.isnan(v)]
		lo = min(vals) if vals else NAN
		hi = max(vals) if vals else NAN
		const = vals and lo == hi
		if args.varying and (not vals or const):
			continue
		if args.constant and not const:
			continue
		rows.append([str(i), log.names[i], log.units[i] if i < len(log.units) else "", fmt(lo), fmt(hi),
					 "const" if const else ""])
	print_table(["#", "name", "units", "min", "max", ""], rows)
	print(f"({len(rows)} columns)")


def cmd_rows(log, args):
	exprs = [Expr(log, c) for c in args.cols]
	ti = log.time_index()
	sel = select_rows(log, args)
	t = log.time()
	vals = [e.evaluate() for e in exprs]
	headers = ["row", "Time"] + [e.name for e in exprs]
	out = []
	prev = None
	for k, i in enumerate(sel):
		if args.every and k % args.every:
			continue
		cur = tuple(fmt(v[i]) for v in vals)
		if args.changes and cur == prev:
			continue
		prev = cur
		out.append([str(i), fmt(t[i])] + list(cur))
	shown = out[:args.limit]
	print_table(headers, shown)
	if len(out) > len(shown):
		print(f"... {len(out) - len(shown)} more rows (raise --limit, or narrow with --from/--to/--rows/--where/--every)")


def cmd_stats(log, args):
	sel = select_rows(log, args)
	rows = []
	for c in args.cols:
		e = Expr(log, c)
		v = e.evaluate()
		vals = sorted(v[i] for i in sel if not math.isnan(v[i]))
		if not vals:
			rows.append([e.name, "0"] + ["-"] * 8)
			continue
		mean = sum(vals) / len(vals)
		nz = sum(1 for x in vals if x != 0) / len(vals)
		rows.append([e.name, str(len(vals)), fmt(vals[0]), fmt(percentile(vals, .05)), fmt(percentile(vals, .5)),
					 fmt(percentile(vals, .95)), fmt(vals[-1]), fmt(mean), f"{nz * 100:.0f}%", e.units])
	print_table(["column", "n", "min", "p5", "median", "p95", "max", "mean", "nonzero", "units"], rows)
	print(f"({len(sel)} of {log.nrows} rows selected)")


def cmd_hist(log, args):
	e = Expr(log, args.col)
	sel = select_rows(log, args)
	v = e.evaluate()
	vals = [v[i] for i in sel if not math.isnan(v[i])]
	if not vals:
		print("no values")
		return
	distinct = sorted(set(vals))
	n = len(vals)
	rows = []
	if len(distinct) <= args.max_distinct and not args.bins:
		for d in distinct:
			c = vals.count(d)
			rows.append([fmt(d), str(c), f"{c * 100 / n:.1f}%", "#" * max(1, round(40 * c / n))])
		print_table([e.name, "count", "share", ""], rows)
	else:
		nb = args.bins or 20
		lo, hi = min(vals), max(vals)
		width = (hi - lo) / nb or 1
		counts = [0] * nb
		for x in vals:
			counts[min(int((x - lo) / width), nb - 1)] += 1
		for b, c in enumerate(counts):
			rows.append([f"{fmt(lo + b * width)} .. {fmt(lo + (b + 1) * width)}", str(c), f"{c * 100 / n:.1f}%",
						 "#" * round(40 * c / n)])
		print_table([e.name, "count", "share", ""], rows)
	print(f"({n} values from {len(sel)} selected rows)")


def cmd_segments(log, args):
	"""Contiguous runs of rows where --where is true."""
	w = Expr(log, args.where).evaluate()
	t = log.time()
	show = [Expr(log, c) for c in args.show]
	sv = [e.evaluate() for e in show]
	lo, hi = 0, log.nrows
	if args.rows:
		a, _, b = args.rows.partition(":")
		lo, hi = int(a) if a else 0, min(int(b) if b else log.nrows, log.nrows)
	segs = []
	start = None
	for i in range(lo, hi):
		on = bool(w[i]) and not math.isnan(w[i])
		if args.t_from is not None and t[i] < args.t_from:
			on = False
		if args.t_to is not None and t[i] > args.t_to:
			on = False
		if on and start is None:
			start = i
		if not on and start is not None:
			segs.append((start, i - 1))
			start = None
	if start is not None:
		segs.append((start, hi - 1))
	segs = [s for s in segs if t[s[1]] - t[s[0]] >= args.min_dur]
	headers = ["rows", "start", "end", "dur"]
	for e in show:
		headers += [f"{e.name} start", "min", "max", "end"]
	rows = []
	for a, b in segs:
		r = [f"{a}:{b + 1}", fmt(t[a]), fmt(t[b]), fmt(t[b] - t[a])]
		for v in sv:
			seg = [x for x in v[a:b + 1] if not math.isnan(x)]
			r += [fmt(v[a]), fmt(min(seg)) if seg else "-", fmt(max(seg)) if seg else "-", fmt(v[b])]
		rows.append(r)
	print_table(headers, rows[:args.limit])
	if len(rows) > args.limit:
		print(f"... {len(rows) - args.limit} more segments (raise --limit or --min-dur)")
	print(f"({len(segs)} segments; rows A:B is half-open, usable as --rows A:B)")


def main():
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	sub = ap.add_subparsers(dest="cmd", required=True)

	def common(p, where=True):
		p.add_argument("log")
		p.add_argument("--from", dest="t_from", type=float, help="start time (s)")
		p.add_argument("--to", dest="t_to", type=float, help="end time (s)")
		p.add_argument("--rows", help="row index range A:B (half-open)")
		if where:
			p.add_argument("--where", help="only rows where this expression is true")

	p = sub.add_parser("info", help="format, firmware signature, duration, rate, markers")
	p.add_argument("log")
	p.set_defaults(fn=cmd_info)

	p = sub.add_parser("cols", help="list columns (filter by regex) with min/max over the whole log")
	p.add_argument("log")
	p.add_argument("patterns", nargs="*", help="case-insensitive regexes; any match")
	p.add_argument("--varying", action="store_true", help="only columns that change")
	p.add_argument("--constant", action="store_true", help="only columns that never change")
	p.set_defaults(fn=cmd_cols)

	p = sub.add_parser("rows", help="print selected rows of some columns/expressions")
	common(p)
	p.add_argument("cols", nargs="+")
	p.add_argument("--every", type=int, help="print every Nth selected row")
	p.add_argument("--changes", action="store_true", help="only rows where a printed value changed")
	p.add_argument("--limit", type=int, default=200)
	p.set_defaults(fn=cmd_rows)

	p = sub.add_parser("stats", help="n/min/percentiles/max/mean per column or expression")
	common(p)
	p.add_argument("cols", nargs="+")
	p.set_defaults(fn=cmd_stats)

	p = sub.add_parser("hist", help="value counts (few distinct values) or binned histogram")
	common(p)
	p.add_argument("col")
	p.add_argument("--bins", type=int, help="force a binned histogram with N bins")
	p.add_argument("--max-distinct", type=int, default=20)
	p.set_defaults(fn=cmd_hist)

	p = sub.add_parser("segments", help="time spans where an expression holds (starts, stalls, phases...)")
	common(p, where=False)
	p.add_argument("--where", required=True)
	p.add_argument("--show", nargs="*", default=[], help="columns to summarize per segment")
	p.add_argument("--min-dur", type=float, default=0, help="drop segments shorter than this (s)")
	p.add_argument("--limit", type=int, default=100)
	p.set_defaults(fn=cmd_segments)

	args = ap.parse_args()
	log = Log(args.log)
	try:
		args.fn(log, args)
	except KeyError as e:
		raise SystemExit(str(e).strip('"'))
	except BrokenPipeError:
		pass


if __name__ == "__main__":
	main()
