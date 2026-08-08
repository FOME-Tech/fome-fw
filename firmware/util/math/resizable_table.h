/**
 * @file	resizable_table.h
 *
 * Helpers for tables whose row and column counts live in the tune rather than being fixed at
 * compile time. TunerStudio lets the user reshape these tables in place - see "Resizable Tables"
 * in the EFI Analytics ini spec.
 *
 * Such a table is declared in fome_config.txt with a min/max/default axis spec, e.g.
 *
 *     begin_table maxsize 576
 *         table_rows min 8 max 32 default 16 uint16_t veLoadBins;;...
 *         table_cols min 8 max 32 default 16 uint16_t veRpmBins;;...
 *         table_values uint16_t autoscale veTable;;...
 *     end_table
 *
 * The generator allocates the values as a flat array of maxsize cells and the bins at their maximum
 * length, and emits two uint8_t fields (veTableRows, veTableCols) holding the counts in use. The
 * values are packed row-major with a stride of the *current* column count, not the allocated one,
 * which is what TunerStudio writes - so the cell at (row, col) is values[row * colCount + col] and
 * everything past rowCount * colCount is unused.
 *
 * The ...Dynamic names exist to keep these clearly distinct from the fixed-size helpers in
 * table_helper.h and libfirmware's interpolation.h: a pointer-and-count call silently binding to a
 * fixed-size overload would be a nasty way to read the wrong cells.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <rusefi/interpolation.h>

// for setRpmBin(), and transitively efi_interpolation.h / efilib.h
#include "table_helper.h"

namespace priv {
/**
 * Runtime-length counterpart to libfirmware's compile-time getBin.
 *
 * @param size number of bins currently in use, which may be fewer than the array is allocated for.
 */
template <typename TBin>
BinResult getBinDynamic(float value, const TBin* bins, size_t size) {
	// Enforce numeric only (int, float, uintx_t, etc)
	static_assert(std::is_arithmetic_v<TBin>, "Table bins must be an arithmetic type");

	// Interpolation needs two bins to sit between. Anything shorter means the tune is corrupt or
	// uninitialized, so pin to the bottom of the axis instead of walking off the end of it.
	if (size < 2) {
		return {0, 0.0f};
	}

	// Handle NaN
	if (cisnan(value)) {
		return {0, 0.0f};
	}

	// Handle off-scale low
	if (value <= bins[0]) {
		return {0, 0.0f};
	}

	// Handle off-scale high
	if (value >= bins[size - 1]) {
		return {size - 2, 1.0f};
	}

	size_t idx = 0;

	// Find the last index less than the searched value
	for (idx = 0; idx < size - 1; idx++) {
		if (bins[idx + 1] > value) {
			break;
		}
	}

	float low = bins[idx];
	float high = bins[idx + 1];

	// Compute how far along the bin we are
	return {idx, (value - low) / (high - low)};
}
} // namespace priv

/**
 * Bilinear interpolation over a resizable table.
 *
 * @param table   flat values array, row-major with a stride of colCount
 */
template <typename VType, typename RType, typename CType>
float interpolate3dDynamic(
		const VType* table,
		const RType* rowBins,
		size_t rowCount,
		float rowValue,
		const CType* colBins,
		size_t colCount,
		float colValue) {
	auto row = priv::getBinDynamic(rowValue, rowBins, rowCount);
	auto col = priv::getBinDynamic(colValue, colBins, colCount);

	auto cell = [&](size_t r, size_t c) { return static_cast<float>(table[r * colCount + c]); };

	// Orient the table such that (0, 0) is the bottom left corner,
	// then the following variable names will make sense
	float lowerLeft = cell(row.Idx, col.Idx);
	float upperLeft = cell(row.Idx + 1, col.Idx);
	float lowerRight = cell(row.Idx, col.Idx + 1);
	float upperRight = cell(row.Idx + 1, col.Idx + 1);

	// Interpolate each side by itself
	float left = priv::linterp(lowerLeft, upperLeft, row.Frac);
	float right = priv::linterp(lowerRight, upperRight, row.Frac);

	// Then interpolate between those
	return priv::linterp(left, right, col.Frac);
}

/**
 * Sets every cell currently in use to the same value. Cells outside rowCount x colCount are left
 * alone - they aren't part of the table until the user grows it, at which point TunerStudio
 * interpolates the new cells from their neighbours rather than reading whatever was there.
 */
template <typename TElement, typename VElement>
void setTableDynamic(TElement* dest, size_t rowCount, size_t colCount, const VElement value) {
	for (size_t row = 0; row < rowCount; row++) {
		for (size_t col = 0; col < colCount; col++) {
			dest[row * colCount + col] = value;
		}
	}
}

/**
 * Resizes the table to match a fixed-size source table and copies it in.
 *
 * Taking the counts by reference means the shape can't drift out of sync with the data - this is
 * how a board or engine config installs a hand-tuned default table.
 */
template <typename TDest, typename TSource, size_t NRows, size_t NCols>
void copyTableDynamic(
		TDest* dest,
		uint8_t& rowCount,
		uint8_t& colCount,
		const TSource (&source)[NRows][NCols],
		float multiply = 1.0f) {
	rowCount = NRows;
	colCount = NCols;

	for (size_t row = 0; row < NRows; row++) {
		for (size_t col = 0; col < NCols; col++) {
			dest[row * NCols + col] = source[row][col] * multiply;
		}
	}
}

/**
 * Copies bins into one axis of a resizable table, resizing that axis to match the source.
 *
 * Pair this with copyTableDynamic() when a board or engine config installs a default table: the
 * axis length and the table shape have to agree, so both are derived from the source arrays rather
 * than written by hand.
 */
template <typename TDest, typename TSource, size_t N>
void copyBinsDynamic(TDest* dest, uint8_t& count, const TSource (&source)[N]) {
	count = N;

	for (size_t i = 0; i < N; i++) {
		dest[i] = source[i];
	}
}

/**
 * Spreads a linear ramp across the bins currently in use. See setLinearCurve() for the fixed-size
 * equivalent - this one stops at count instead of filling the whole allocation, so the unused tail
 * doesn't end up ahead of the last live bin.
 */
template <typename TValue>
void setLinearCurveDynamic(TValue* array, size_t count, float from, float to, float precision = 0.01f) {
	if (count < 2) {
		return;
	}

	for (size_t i = 0; i < count; i++) {
		float value = interpolateClamped(0, from, count - 1, to, i);

		// rounded values look nicer, also we want to avoid precision mismatch with Tuner Studio
		array[i] = efiRound(value, precision);
	}
}

/** Fills the bins currently in use with the default RPM range. See setRpmTableBin(). */
template <typename TValue>
void setRpmTableBinDynamic(TValue* array, size_t count) {
	if (count < 3) {
		return;
	}

	setRpmBin(array, count, 800, 7000);
}

/** Checks only the bins currently in use - the unused tail is meaningless and often all zeroes. */
template <typename TValue>
void ensureArrayIsAscendingDynamic(const char* msg, const TValue* values, size_t count) {
	for (size_t i = 0; i + 1 < count; i++) {
		float cur = values[i];
		float next = values[i + 1];
		if (next <= cur) {
			firmwareError(
					ObdCode::CUSTOM_ERR_AXIS_ORDER,
					"Invalid table axis (must be ascending!): %s %f %f at %d",
					msg,
					cur,
					next,
					i);
		}
	}
}
