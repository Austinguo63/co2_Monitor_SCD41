#pragma once

#include <Arduino.h>
#include <FS.h>

#include "app_types.h"

#pragma pack(push, 1)
struct RawRecord {
    uint32_t epoch;
    uint16_t co2;
    int16_t tempCenti;
    uint16_t humidityCenti;
};

struct BucketRecord {
    uint32_t startEpoch;
    uint16_t avgPpm;
    uint16_t minPpm;
    uint16_t maxPpm;
    uint16_t count;
};
#pragma pack(pop)

class HistoryStore {
  public:
    using RawVisitor = bool (*)(const RawRecord& record, void* userData);
    using BucketVisitor = bool (*)(const BucketRecord& record, void* userData);

    bool begin();
    void addSensorSample(uint32_t epoch, uint16_t co2);
    void addPublishedRawPoint(uint32_t epoch, uint16_t co2, float temperatureC,
                              float humidity);

    void forEachBucket(HistoryRange range, BucketVisitor visitor, void* userData,
                       uint32_t cutoffEpoch = 0) const;
    void forEachRaw(RawVisitor visitor, void* userData,
                    uint32_t cutoffEpoch = 0) const;

    HistoryStats compute24HourStats(uint32_t nowEpoch) const;
    uint32_t bucketSecondsForRange(HistoryRange range) const;

  private:
    struct RingSpec {
        const char* path;
        uint32_t magic;
        uint16_t recordSize;
        uint32_t capacity;
    };

    struct RingHeader {
        uint32_t magic;
        uint16_t version;
        uint16_t recordSize;
        uint32_t capacity;
        uint32_t head;
        uint32_t count;
    };

    static constexpr uint16_t kRingVersion = 1;
    static constexpr uint32_t kRaw24hWindowSec = 24UL * 60UL * 60UL;
    static constexpr uint32_t k24hWindowSec = 24UL * 60UL * 60UL;
    static constexpr uint32_t k7dWindowSec = 7UL * 24UL * 60UL * 60UL;
    static constexpr uint32_t k30dWindowSec = 30UL * 24UL * 60UL * 60UL;
    static constexpr uint32_t k6moWindowSec = 183UL * 24UL * 60UL * 60UL;

    static const RingSpec kRawSpec;
    static const RingSpec k24hSpec;
    static const RingSpec k7dSpec;
    static const RingSpec k30dSpec;
    static const RingSpec k6moSpec;

    bool ensureRing(const RingSpec& spec);
    bool readHeader(File& file, RingHeader& header) const;
    bool writeHeader(File& file, const RingHeader& header) const;
    bool appendRecord(const RingSpec& spec, const void* record);
    bool readLatestRecord(const RingSpec& spec, void* record, bool& found) const;
    bool overwriteLatestRecord(const RingSpec& spec, const void* record);
    void updateBucket(const RingSpec& spec, uint32_t bucketStart, uint16_t co2);

    template <typename RecordType, typename VisitorType>
    void forEachTyped(const RingSpec& spec, VisitorType visitor, void* userData,
                      uint32_t cutoffEpoch) const;

    const RingSpec& specForRange(HistoryRange range) const;
    uint32_t windowForRange(HistoryRange range) const;
};
