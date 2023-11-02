/*
 * @file logicdata_csv_reader.h
 *
 * @date Jun 26, 2021
 * @author Andrey Belomutskiy, (c) 2012-2021
 */

const int NORMAL_ORDER[2] = {0, 1};

const int REVERSE_ORDER[2] = {1, 0};



class CsvReader {
public:
	CsvReader(size_t triggerCount, size_t vvtCount) : CsvReader(triggerCount, vvtCount, 0.0) {}
	CsvReader(size_t triggerCount, size_t vvtCount, double timestampOffset)
		: m_triggerCount(triggerCount)
		, m_vvtCount(vvtCount)
		, m_timestampOffset(timestampOffset)
	{
	}
	~CsvReader();

	bool twoBanksSingleCamMode = true;

	void open(const char *fileName, const int* triggerColumnIndeces = NORMAL_ORDER, const int *vvtColumnIndeces = NORMAL_ORDER);
	bool haveMore();
	void processLine(EngineTestHelper *eth);
	void readLine(EngineTestHelper *eth);
	double readTimestampAndValues(double *v);

	int lineIndex() const {
		return m_lineIndex;
	}

private:
	const size_t m_triggerCount;
	const size_t m_vvtCount;
	const double m_timestampOffset;

	FILE *fp = nullptr;
	char buffer[255];

	bool currentState[2] = {0, 0};
	bool currentVvtState[CAM_INPUTS_COUNT] = {0, 0};

	int m_lineIndex = -1;

	const int* m_triggerColumnIndeces;
	const int* m_vvtColumnIndeces;
};

