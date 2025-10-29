#pragma once

template<typename V>
struct StrPair { const char* key; V value; };

static inline constexpr int cstrcmp(const char* a, const char* b) {
    size_t i = 0;
    for (;;) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca != cb) return (int)ca - (int)cb;
        if (ca == 0) return 0;
        ++i;
    }
}

static inline constexpr uint32_t cstr_hash_fnv1a(const char* s) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; ; ++i) {
        unsigned char b = (unsigned char)s[i];
        if (b == 0) break;
        h ^= (uint32_t)b;
        h *= 16777619u;
    }
    return h;
}

template <typename V, size_t Capacity>
class StaticStringHashMap {
    enum : uint8_t { SLOT_EMPTY = 0u, SLOT_OCCUPIED = 1u };

    struct Slot {
        const char* key;
        V           value;
        uint8_t     state;
    };

public:
    typedef V mapped_type;

    inline constexpr StaticStringHashMap() : _count(0) {
        for (size_t i = 0; i < Capacity; ++i) _slots[i].state = SLOT_EMPTY;
    }

    inline constexpr const V* find(const char* key) const {
        const uint32_t h = cstr_hash_fnv1a(key);
        size_t idx = (size_t)(h % (uint32_t)Capacity);
        for (size_t step = 0; step < Capacity; ++step) {
            const Slot& s = _slots[idx];
            if (s.state == SLOT_EMPTY) return (const V*)0;
            if (s.state == SLOT_OCCUPIED && cstrcmp(s.key, key) == 0)
                return &s.value;
            idx = (idx + 1u) % Capacity;
        }
        return (const V*)0;
    }

    inline constexpr int contains(const char* key) const { return find(key) != (const V*)0; }

    template<typename F>
    inline constexpr void for_each(F f) const {
        for (size_t i = 0; i < Capacity; ++i)
            if (_slots[i].state == SLOT_OCCUPIED) f(_slots[i].key, _slots[i].value);
    }

    inline constexpr size_t size() const { return _count; }
    static inline constexpr size_t capacity() { return Capacity; }

private:
    template<typename Map, typename VV, size_t N>
    friend consteval Map make_static_string_hashmap(const StrPair<VV>(&)[N]);

    inline constexpr void builder_insert(const char* key, const V& v) {
        const uint32_t h = cstr_hash_fnv1a(key);
        size_t idx = (size_t)(h % (uint32_t)Capacity);
        for (size_t step = 0; step < Capacity; ++step) {
            Slot& s = _slots[idx];
            if (s.state == SLOT_EMPTY) {
                s.key = key; s.value = v; s.state = SLOT_OCCUPIED;
                ++_count;
                return;
            }
            if (s.state == SLOT_OCCUPIED && cstrcmp(s.key, key) == 0) {
                s.value = v;
                return;
            }
            idx = (idx + 1u) % Capacity;
        }
    }

private:
    Slot    _slots[Capacity];
    size_t  _count;
};

template<typename Map, typename V, size_t N>
consteval Map make_static_string_hashmap(const StrPair<V> (&items)[N]) {
    static_assert(N <= Map::capacity(), "StaticStringHashMap capacity too small for provided items");
    Map m;
    for (size_t i = 0; i < N; ++i) {
        m.builder_insert(items[i].key, items[i].value);
    }
    return m;
}

static constexpr StrPair<SensorType> kSensorPairs[] = {
    { "Invalid",                        SensorType::Invalid },
    { "Clt",                            SensorType::Clt },
    { "Iat",                            SensorType::Iat },
    { "Rpm",                            SensorType::Rpm },
    { "Map",                            SensorType::Map },
    { "Maf",                            SensorType::Maf },

    { "AmbientTemperature",             SensorType::AmbientTemperature },
    { "EcuInternalTemperature",         SensorType::EcuInternalTemperature },

    { "OilPressure",                    SensorType::OilPressure },
    { "OilTemperature",                 SensorType::OilTemperature },

    { "FuelPressureLow",                SensorType::FuelPressureLow },
    { "FuelPressureHigh",               SensorType::FuelPressureHigh },
    { "FuelPressureInjector",           SensorType::FuelPressureInjector },

    { "FuelTemperature",                SensorType::FuelTemperature },

    { "Tps1",                           SensorType::Tps1 },
    { "Tps1Primary",                    SensorType::Tps1Primary },
    { "Tps1Secondary",                  SensorType::Tps1Secondary },

    { "Tps2",                           SensorType::Tps2 },
    { "Tps2Primary",                    SensorType::Tps2Primary },
    { "Tps2Secondary",                  SensorType::Tps2Secondary },

    { "AcceleratorPedal",               SensorType::AcceleratorPedal },
    { "AcceleratorPedalPrimary",        SensorType::AcceleratorPedalPrimary },
    { "AcceleratorPedalSecondary",      SensorType::AcceleratorPedalSecondary },

    { "DriverThrottleIntent",           SensorType::DriverThrottleIntent },

    { "AuxTemp1",                       SensorType::AuxTemp1 },
    { "AuxTemp2",                       SensorType::AuxTemp2 },

    { "Lambda1",                        SensorType::Lambda1 },
    { "Lambda2",                        SensorType::Lambda2 },
    { "Lambda3",                        SensorType::Lambda3 },
    { "Lambda4",                        SensorType::Lambda4 },

    { "WastegatePosition",              SensorType::WastegatePosition },

    { "FuelEthanolPercent",             SensorType::FuelEthanolPercent },

    { "BatteryVoltage",                 SensorType::BatteryVoltage },
    { "MainRelayVoltage",               SensorType::MainRelayVoltage },
    { "Sensor5vVoltage",                SensorType::Sensor5vVoltage },

    { "BarometricPressure",             SensorType::BarometricPressure },

    { "FuelLevel",                      SensorType::FuelLevel },

    { "VehicleSpeed",                   SensorType::VehicleSpeed },
    { "WheelSpeedLF",                   SensorType::WheelSpeedLF },
    { "WheelSpeedRF",                   SensorType::WheelSpeedRF },
    { "WheelSpeedLR",                   SensorType::WheelSpeedLR },
    { "WheelSpeedRR",                   SensorType::WheelSpeedRR },

    { "TurbochargerSpeed",              SensorType::TurbochargerSpeed },

    { "MapFast",                        SensorType::MapFast },
    { "MapSlow",                        SensorType::MapSlow },

    { "InputShaftSpeed",                SensorType::InputShaftSpeed },

    { "Maf2",                           SensorType::Maf2 },

    { "Map2",                           SensorType::Map2 },
    { "MapSlow2",                       SensorType::MapSlow2 },
    { "MapFast2",                       SensorType::MapFast2 },

    { "CompressorDischargePressure",    SensorType::CompressorDischargePressure },
    { "CompressorDischargeTemperature", SensorType::CompressorDischargeTemperature },

    { "ThrottleInletPressure",          SensorType::ThrottleInletPressure },

    { "DetectedGear",                   SensorType::DetectedGear },

    { "AuxAnalog1",                     SensorType::AuxAnalog1 },
    { "AuxAnalog2",                     SensorType::AuxAnalog2 },
    { "AuxAnalog3",                     SensorType::AuxAnalog3 },
    { "AuxAnalog4",                     SensorType::AuxAnalog4 },
    { "AuxAnalog5",                     SensorType::AuxAnalog5 },
    { "AuxAnalog6",                     SensorType::AuxAnalog6 },
    { "AuxAnalog7",                     SensorType::AuxAnalog7 },
    { "AuxAnalog8",                     SensorType::AuxAnalog8 },

    { "LuaGauge1",                      SensorType::LuaGauge1 },
    { "LuaGauge2",                      SensorType::LuaGauge2 },

    { "AuxLinear1",                     SensorType::AuxLinear1 },
    { "AuxLinear2",                     SensorType::AuxLinear2 },
    { "AuxLinear3",                     SensorType::AuxLinear3 },
    { "AuxLinear4",                     SensorType::AuxLinear4 },

    { "AuxSpeed1",                      SensorType::AuxSpeed1 },
    { "AuxSpeed2",                      SensorType::AuxSpeed2 },

    { "PlaceholderLast",                SensorType::PlaceholderLast },
};