#!/usr/bin/env bash

# Wraps the firmware link command (passed as arguments) to print a short memory
# usage report: one line per linker memory region that has anything in it, with
# a breakdown of which output sections use that space.
#
# ld's own --print-memory-usage lists every region, including the many empty
# placeholder regions ChibiOS linker scripts must define, and mixes B/KB/MB units.
# It also treats every region independently, but ours overlap:
#
# - Some RAM regions are sub-ranges of another (on F7, ram0 = SRAM1 + SRAM2 and
#   ram2 = SRAM2). If the sub-region is in use, the parent can't grow into it,
#   so the parent's capacity is reported without it.
# - On F7, flash1 is the ITCM-bus address of flash0: code is linked to run from
#   there but stored in flash0, so it's already counted. Any flash region the
#   same size as flash0 is such a view of it, and isn't reported.

mapFile=""
elfFile=""
prev=""
for arg in "$@"; do
	if [[ $arg =~ -Map=([^,]+) ]]; then
		mapFile=${BASH_REMATCH[1]}
	fi
	if [[ $prev == "-o" ]]; then
		elfFile=$arg
	fi
	prev=$arg
done

# objdump from the same toolchain as the compiler driver doing the link
objdump="${1%gcc}objdump"

# Capture ld's output before reading the map and ELF, so we never read the
# files left over from a previous link.
ldOutput=$("$@" -Wl,--print-memory-usage)
status=$?

if [ $status -ne 0 ]; then
	[ -n "$ldOutput" ] && printf '%s\n' "$ldOutput"
	exit $status
fi

# "region <name> <origin> <length>", from the map file's "Memory Configuration" table
regionTable() {
	[ -f "$mapFile" ] || return 0

	sed -n '/^Memory Configuration/,/^Linker script/p' "$mapFile" |
		while read -r name origin length _; do
			if [[ $origin == 0x* && $name != "*default*" ]]; then
				echo "region $name $((origin)) $((length))"
			fi
		done
}

# "section <name> <size> <vma> <lma> <loaded>", for each section that occupies memory on the target
sectionTable() {
	[ -f "$elfFile" ] && [ -x "$objdump" ] || return 0

	"$objdump" -h -w "$elfFile" |
		while read -r idx name size vma lma _ _ flags; do
			[[ $idx =~ ^[0-9]+$ && $flags == *ALLOC* ]] || continue

			loaded=0
			[[ $flags == *LOAD* ]] && loaded=1

			echo "section $name $((16#$size)) $((16#$vma)) $((16#$lma)) $loaded"
		done
}

printf '%s\n' "$ldOutput" | awk '
function bytes(n, unit) {
	if (unit == "KB") return n * 1024
	if (unit == "MB") return n * 1024 * 1024
	if (unit == "GB") return n * 1024 * 1024 * 1024
	return n
}

function contains(outer, inner) {
	return (outer in origin) && (inner in origin) &&
		length_[inner] < length_[outer] &&
		origin[inner] >= origin[outer] &&
		origin[inner] + length_[inner] <= origin[outer] + length_[outer]
}

# The used region that holds [addr, addr + size). Regions overlap, but ld
# guarantees sections do not, so the smallest used region containing it is the
# one it was placed in.
function regionAt(addr, size,    i, name, best) {
	best = ""
	for (i = 1; i <= count; i++) {
		name = names[i]
		if ((name in origin) && addr >= origin[name] && addr + size <= origin[name] + length_[name]) {
			if (best == "" || length_[name] < length_[best])
				best = name
		}
	}
	return best
}

function addToRegion(region, section, size) {
	if (region == "") return
	if (!((region, section) in sectionBytes)) {
		sectionCount[region]++
		sectionName[region, sectionCount[region]] = section
		sectionBytes[region, section] = 0
	}
	sectionBytes[region, section] += size
	sectionTotal[region] += size
}

function assignSections(    i) {
	for (i = 1; i <= nSections; i++) {
		# Where it lives at runtime...
		addToRegion(regionAt(secVma[i], secSize[i]), secName[i], secSize[i])

		# ...and, if it is copied there at startup, where it is stored (eg .data in flash)
		if (secLoaded[i] && secLma[i] != secVma[i])
			addToRegion(regionAt(secLma[i], secSize[i]), secName[i], secSize[i])
	}
}

function kb(b) {
	return b / 1024
}

function printBreakdown(region, used,    n, i, j, tmp, other, padding) {
	n = sectionCount[region]
	if (n == 0) return

	for (i = 1; i <= n; i++)
		order[i] = sectionName[region, i]

	# Largest first (insertion sort - only a handful of sections per region)
	for (i = 2; i <= n; i++) {
		tmp = order[i]
		for (j = i - 1; j >= 1 && sectionBytes[region, order[j]] < sectionBytes[region, tmp]; j--)
			order[j + 1] = order[j]
		order[j + 1] = tmp
	}

	other = 0
	for (i = 1; i <= n; i++) {
		if (sectionBytes[region, order[i]] >= 1024)
			printf "      %-14s %8.1f KB\n", order[i], kb(sectionBytes[region, order[i]])
		else
			other += sectionBytes[region, order[i]]
	}

	if (other > 0)
		printf "      %-14s %8.1f KB\n", "(other)", kb(other)

	# ld counts alignment gaps between sections as used
	padding = used - sectionTotal[region]
	if (padding >= 52)
		printf "      %-14s %8.1f KB\n", "(padding)", kb(padding)
}

function report(    i, j, name, size, excluded, sep, pct) {
	if (count == 0) return

	assignSections()

	print "Memory usage:"

	for (i = 1; i <= count; i++) {
		name = names[i]

		if (name != "flash0" && name ~ /^flash/ && ("flash0" in length_) && length_[name] == length_["flash0"])
			continue

		size = sizeBytes[name]
		excluded = ""

		for (j = 1; j <= count; j++) {
			if (contains(name, names[j])) {
				size -= length_[names[j]]
				sep = (excluded == "") ? "  (excl. " : ", "
				excluded = excluded sep names[j]
			}
		}

		if (excluded != "") excluded = excluded ")"

		pct = (size > 0) ? 100 * usedBytes[name] / size : 0
		printf "  %-8s %8.1f / %7.1f KB (%5.1f%%)%s\n", name, kb(usedBytes[name]), kb(size), pct, excluded

		printBreakdown(name, usedBytes[name])
	}
}

# First input: the region and section tables
FILENAME != "-" && $1 == "region" {
	origin[$2] = $3
	length_[$2] = $4
	next
}

FILENAME != "-" && $1 == "section" {
	nSections++
	secName[nSections] = $2
	secSize[nSections] = $3
	secVma[nSections] = $4
	secLma[nSections] = $5
	secLoaded[nSections] = $6
	next
}

FILENAME != "-" { next }

/^Memory region +Used Size +Region Size/ { inReport = 1; next }

inReport && $1 ~ /:$/ && NF >= 5 {
	name = substr($1, 1, length($1) - 1)
	used = bytes($2, $3)
	if (used > 0) {
		count++
		names[count] = name
		usedBytes[name] = used
		sizeBytes[name] = bytes($4, $5)
	}
	next
}

inReport { inReport = 0; report() }

{ print }

END { if (inReport) report() }
' <(regionTable; sectionTable) -
