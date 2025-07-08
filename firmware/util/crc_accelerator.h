#pragma once

class Crc final {
public:
	// Constructor passing an argument that gives the estimated number
	// of bytes that will be checksummed. MAY be used to select an optimal
	// implementation given that information.
	Crc(size_t estimatedWorkSize = 10000);

	~Crc();

	void addData(const void* buf, size_t size);
	uint32_t getCrc() const;

private:
	const bool m_acquiredExclusive;
	uint32_t m_crc = 0;
};

inline uint32_t singleCrc(const void* buf, size_t size) {
	Crc crc(size);
	crc.addData(buf, size);
	return crc.getCrc();
}
