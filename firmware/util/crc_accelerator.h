#pragma once

class Crc final {
public:
	Crc();
	~Crc();

	void addData(const void* buf, size_t size);
	uint32_t getCrc() const;

private:
	const bool m_acquiredExclusive;
	uint32_t m_crc = 0;
};

inline uint32_t singleCrc(const void* buf, size_t size) {
	Crc crc;
	crc.addData(buf, size);
	return crc.getCrc();
}
