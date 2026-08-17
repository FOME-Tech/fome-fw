#include "pch.h"

#include "defaults.h"

// A 32x32 cell budget's worth of storage, so tests can freely reshape within it. Real tables are
// allocated by the config generator at their maxsize.
static uint8_t storage[1024];

// Fills the in-use region such that each cell's value is its own (row * 10 + col), making it obvious
// which cells an interpolation actually read.
static void fillIdentifiable(size_t rows, size_t cols) {
	for (size_t row = 0; row < rows; row++) {
		for (size_t col = 0; col < cols; col++) {
			storage[row * cols + col] = 10 * row + col;
		}
	}
}

TEST(ResizableTable, ExactCorners) {
	const uint8_t rowBins[] = {0, 10, 20, 30};
	const uint8_t colBins[] = {0, 100, 200};

	fillIdentifiable(4, 3);

	// Land exactly on each bin and check we get that cell back untouched
	for (size_t row = 0; row < 4; row++) {
		for (size_t col = 0; col < 3; col++) {
			EXPECT_FLOAT_EQ(
					10 * row + col, interpolate3dDynamic(storage, rowBins, 4, rowBins[row], colBins, 3, colBins[col]))
					<< "row " << row << " col " << col;
		}
	}
}

TEST(ResizableTable, Interpolates) {
	const uint8_t rowBins[] = {0, 10, 20, 30};
	const uint8_t colBins[] = {0, 100, 200};

	fillIdentifiable(4, 3);

	// Halfway between rows 1 and 2 at column 0: halfway between 10 and 20
	EXPECT_FLOAT_EQ(15, interpolate3dDynamic(storage, rowBins, 4, 15, colBins, 3, 0));

	// Halfway between columns 0 and 1 at row 0: halfway between 0 and 1
	EXPECT_FLOAT_EQ(0.5f, interpolate3dDynamic(storage, rowBins, 4, 0, colBins, 3, 50));

	// Centre of the bottom left square: mean of cells (0,0) (0,1) (1,0) (1,1) = 0, 1, 10, 11
	EXPECT_FLOAT_EQ(5.5f, interpolate3dDynamic(storage, rowBins, 4, 5, colBins, 3, 50));
}

TEST(ResizableTable, Clamps) {
	const uint8_t rowBins[] = {0, 10, 20, 30};
	const uint8_t colBins[] = {0, 100, 200};

	fillIdentifiable(4, 3);

	// Off scale low on both axes clamps to the first cell
	EXPECT_FLOAT_EQ(0, interpolate3dDynamic(storage, rowBins, 4, -50, colBins, 3, -50));

	// Off scale high on both axes clamps to the last cell of the last row
	EXPECT_FLOAT_EQ(32, interpolate3dDynamic(storage, rowBins, 4, 1000, colBins, 3, 1000));

	// NaN is treated as off scale low rather than propagating
	EXPECT_FLOAT_EQ(0, interpolate3dDynamic(storage, rowBins, 4, NAN, colBins, 3, NAN));
}

// The row stride is the *current* column count, so the same values array reads back differently
// after a reshape. This is the property that makes a resizable table work at all.
TEST(ResizableTable, StrideFollowsColumnCount) {
	const uint8_t rowBins[] = {0, 10, 20, 30, 40, 50};
	const uint16_t colBins[] = {0, 100, 200, 300, 400, 500};

	// As a 6-column table, cell (1, 0) is at flat index 6
	fillIdentifiable(4, 6);
	EXPECT_FLOAT_EQ(10, interpolate3dDynamic(storage, rowBins, 4, 10, colBins, 6, 0));

	// Reshaped to 3 columns, cell (1, 0) is at flat index 3
	fillIdentifiable(6, 3);
	EXPECT_FLOAT_EQ(10, interpolate3dDynamic(storage, rowBins, 6, 10, colBins, 3, 0));
}

// A tune that has never been initialized reads back as zeroes. That's not a valid table, but it must
// not read outside the allocation.
TEST(ResizableTable, DegenerateSizes) {
	const uint8_t rowBins[] = {0, 10, 20, 30};
	const uint8_t colBins[] = {0, 100, 200};

	fillIdentifiable(4, 3);

	// Single-bin and zero-bin axes pin to the bottom of that axis instead of running off the end
	EXPECT_NO_FATAL_FAILURE(interpolate3dDynamic(storage, rowBins, 1, 15, colBins, 3, 50));
	EXPECT_NO_FATAL_FAILURE(interpolate3dDynamic(storage, rowBins, 0, 15, colBins, 0, 50));
}

TEST(ResizableTable, SetTable) {
	memset(storage, 0xAB, sizeof(storage));

	setTableDynamic(storage, 3, 4, 7);

	// The 12 cells in use are set...
	for (size_t i = 0; i < 12; i++) {
		EXPECT_EQ(7, storage[i]) << "cell " << i;
	}

	// ...and nothing past them is touched
	EXPECT_EQ(0xAB, storage[12]);
}

TEST(ResizableTable, CopyTableResizes) {
	static const uint8_t source[2][3] = {
			{1, 2, 3},
			{4, 5, 6},
	};

	uint8_t rows = 99;
	uint8_t cols = 99;

	copyTableDynamic(storage, rows, cols, source);

	EXPECT_EQ(2, rows);
	EXPECT_EQ(3, cols);

	for (size_t i = 0; i < 6; i++) {
		EXPECT_EQ(i + 1, storage[i]) << "cell " << i;
	}
}

TEST(ResizableTable, CopyBinsResizes) {
	static const uint16_t source[] = {100, 200, 300, 400, 500};

	uint16_t bins[32];
	uint8_t count = 99;

	copyBinsDynamic(bins, count, source);

	EXPECT_EQ(5, count);

	for (size_t i = 0; i < 5; i++) {
		EXPECT_EQ(source[i], bins[i]) << "bin " << i;
	}
}

TEST(ResizableTable, SetLinearCurveStopsAtCount) {
	uint16_t bins[32];
	memset(bins, 0xFF, sizeof(bins));

	setLinearCurveDynamic(bins, 5, 100, 500, 1);

	EXPECT_EQ(100, bins[0]);
	EXPECT_EQ(300, bins[2]);
	EXPECT_EQ(500, bins[4]);

	// The unused tail is left alone rather than continuing the ramp past the last live bin
	EXPECT_EQ(0xFFFF, bins[5]);
}

// The whole point of the feature: a table sized from the tune reads correctly at a non-default shape.
TEST(ResizableTable, VeTableAtNonDefaultShape) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	// 32 rows x 18 columns is 576 cells - the VE table's full budget in a lopsided shape
	config->veTableRows = 32;
	config->veTableCols = 18;

	setLinearCurveDynamic(config->veLoadBins, config->veTableRows, 10, 320, 1);
	setLinearCurveDynamic(config->veRpmBins, config->veTableCols, 800, 8000, 1);
	setTableDynamic(config->veTable, config->veTableRows, config->veTableCols, 55);

	// Poke a single cell and read it back exactly, which only works if the stride is 18
	config->veTable[5 * 18 + 3] = 88;

	EXPECT_FLOAT_EQ(
			88,
			interpolate3dDynamic(
					config->veTable,
					config->veLoadBins,
					config->veTableRows,
					config->veLoadBins[5],
					config->veRpmBins,
					config->veTableCols,
					config->veRpmBins[3]));

	// Everything else still reads the flat 55
	EXPECT_FLOAT_EQ(
			55,
			interpolate3dDynamic(
					config->veTable,
					config->veLoadBins,
					config->veTableRows,
					config->veLoadBins[20],
					config->veRpmBins,
					config->veTableCols,
					config->veRpmBins[10]));
}

// The ignition table is signed and scaled, so it also covers negative cells round-tripping through
// the scaled_channel storage at a reshaped stride.
TEST(ResizableTable, IgnitionTableAtNonDefaultShape) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	// 18 rows x 32 columns - the same 576 cell budget as VE, transposed
	config->ignitionTableRows = 18;
	config->ignitionTableCols = 32;

	setLinearCurveDynamic(config->ignitionLoadBins, config->ignitionTableRows, 20, 200, 1);
	setLinearCurveDynamic(config->ignitionRpmBins, config->ignitionTableCols, 800, 8000, 1);
	setTableDynamic(config->ignitionTable, config->ignitionTableRows, config->ignitionTableCols, 25);

	// Retard at low load/low rpm, which only lands where intended if the stride is 32
	config->ignitionTable[2 * 32 + 7] = -12.5f;

	EXPECT_FLOAT_EQ(
			-12.5f,
			interpolate3dDynamic(
					config->ignitionTable,
					config->ignitionLoadBins,
					config->ignitionTableRows,
					config->ignitionLoadBins[2],
					config->ignitionRpmBins,
					config->ignitionTableCols,
					config->ignitionRpmBins[7]));

	EXPECT_FLOAT_EQ(
			25,
			interpolate3dDynamic(
					config->ignitionTable,
					config->ignitionLoadBins,
					config->ignitionTableRows,
					config->ignitionLoadBins[10],
					config->ignitionRpmBins,
					config->ignitionTableCols,
					config->ignitionRpmBins[20]));
}

// setDefaultIgnition() builds the timing map by walking the shape in use. Poison the whole
// allocation first, so this checks that every cell of the default shape actually got written - a
// stride or bound mistake would leave some of them poisoned.
TEST(ResizableTable, DefaultIgnitionTableIsFullyPopulated) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	// buildTimingMap() only runs for MAP-based load, otherwise it warns and leaves the table alone
	engineConfiguration->fuelAlgorithm = LM_SPEED_DENSITY;

	// Well outside the -20..90 degree range the table can hold, and exactly representable at the
	// table's 0.1 degree resolution, so it round-trips unchanged if nothing overwrites it
	static constexpr float poison = -99.9f;

	for (size_t i = 0; i < efi::size(config->ignitionTable); i++) {
		config->ignitionTable[i] = poison;
	}

	setDefaultIgnition();

	size_t rows = config->ignitionTableRows;
	size_t cols = config->ignitionTableCols;

	ASSERT_GE(rows, 2u);
	ASSERT_GE(cols, 2u);

	for (size_t row = 0; row < rows; row++) {
		for (size_t col = 0; col < cols; col++) {
			EXPECT_NE(poison, (float)config->ignitionTable[row * cols + col]) << "row " << row << " col " << col;
		}
	}
}
