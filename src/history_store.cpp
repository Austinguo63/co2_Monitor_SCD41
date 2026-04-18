#include "history_store.h"

#include <LittleFS.h>
#include <type_traits>

namespace {
constexpr uint32_t kHistoryMagicBase = 0x434f3248;  // "CO2H"
}  // namespace

const HistoryStore::RingSpec HistoryStore::kRawSpec = {
    "/raw24h.bin", kHistoryMagicBase + 1, sizeof(RawRecord), 17280};
const HistoryStore::RingSpec HistoryStore::k24hSpec = {
    "/hist_24h.bin", kHistoryMagicBase + 2, sizeof(BucketRecord), 288};
const HistoryStore::RingSpec HistoryStore::k7dSpec = {
    "/hist_7d.bin", kHistoryMagicBase + 3, sizeof(BucketRecord), 336};
const HistoryStore::RingSpec HistoryStore::k30dSpec = {
    "/hist_30d.bin", kHistoryMagicBase + 4, sizeof(BucketRecord), 360};
const HistoryStore::RingSpec HistoryStore::k6moSpec = {
    "/hist_6mo.bin", kHistoryMagicBase + 5, sizeof(BucketRecord), 372};

bool HistoryStore::begin() {
    if (!LittleFS.begin(true, "/littlefs", 10, kFilesystemPartitionLabel)) {
        return false;
    }
    return ensureRing(kRawSpec) && ensureRing(k24hSpec) && ensureRing(k7dSpec) &&
           ensureRing(k30dSpec) && ensureRing(k6moSpec);
}

void HistoryStore::addSensorSample(uint32_t epoch, uint16_t co2) {
    if (epoch == 0) {
        return;
    }
    updateBucket(k24hSpec, epoch - (epoch % 300UL), co2);
    updateBucket(k7dSpec, epoch - (epoch % 1800UL), co2);
    updateBucket(k30dSpec, epoch - (epoch % 7200UL), co2);
    updateBucket(k6moSpec, epoch - (epoch % 43200UL), co2);
}

void HistoryStore::addPublishedRawPoint(uint32_t epoch, uint16_t co2,
                                        float temperatureC, float humidity) {
    if (epoch == 0) {
        return;
    }
    RawRecord record = {};
    record.epoch = epoch;
    record.co2 = co2;
    record.tempCenti = static_cast<int16_t>(temperatureC * 100.0f);
    record.humidityCenti = static_cast<uint16_t>(humidity * 100.0f);
    appendRecord(kRawSpec, &record);
}

HistoryStats HistoryStore::compute24HourStats(uint32_t nowEpoch) const {
    HistoryStats stats;
    if (nowEpoch == 0) {
        return stats;
    }

    struct AggregateState {
        uint32_t cutoff;
        uint32_t weightedTotal = 0;
        uint32_t totalCount = 0;
        uint16_t minPpm = 0;
        uint16_t maxPpm = 0;
        bool seen = false;
    } state;
    state.cutoff = nowEpoch > k24hWindowSec ? nowEpoch - k24hWindowSec : 0;

    forEachBucket(
        HistoryRange::Range24h,
        [](const BucketRecord& record, void* userData) -> bool {
            auto* state = static_cast<AggregateState*>(userData);
            if (record.startEpoch < state->cutoff || record.count == 0) {
                return true;
            }

            state->weightedTotal +=
                static_cast<uint32_t>(record.avgPpm) * record.count;
            state->totalCount += record.count;
            if (!state->seen) {
                state->minPpm = record.minPpm;
                state->maxPpm = record.maxPpm;
                state->seen = true;
            } else {
                if (record.minPpm < state->minPpm) {
                    state->minPpm = record.minPpm;
                }
                if (record.maxPpm > state->maxPpm) {
                    state->maxPpm = record.maxPpm;
                }
            }
            return true;
        },
        &state, state.cutoff);

    if (!state.seen || state.totalCount == 0) {
        return stats;
    }

    stats.valid = true;
    stats.avgPpm = static_cast<uint16_t>(state.weightedTotal / state.totalCount);
    stats.minPpm = state.minPpm;
    stats.maxPpm = state.maxPpm;
    return stats;
}

uint32_t HistoryStore::bucketSecondsForRange(HistoryRange range) const {
    switch (range) {
        case HistoryRange::Range24h:
            return 300;
        case HistoryRange::Range7d:
            return 1800;
        case HistoryRange::Range30d:
            return 7200;
        case HistoryRange::Range6mo:
            return 43200;
    }
    return 300;
}

bool HistoryStore::ensureRing(const RingSpec& spec) {
    File file = LittleFS.open(spec.path, FILE_READ);
    if (file) {
        RingHeader header = {};
        bool valid = readHeader(file, header) && header.magic == spec.magic &&
                     header.version == kRingVersion &&
                     header.recordSize == spec.recordSize &&
                     header.capacity == spec.capacity;
        file.close();
        if (valid) {
            return true;
        }
        LittleFS.remove(spec.path);
    }

    file = LittleFS.open(spec.path, "w");
    if (!file) {
        return false;
    }

    RingHeader header = {};
    header.magic = spec.magic;
    header.version = kRingVersion;
    header.recordSize = spec.recordSize;
    header.capacity = spec.capacity;
    header.head = 0;
    header.count = 0;
    bool ok = writeHeader(file, header);
    file.close();
    return ok;
}

bool HistoryStore::readHeader(File& file, RingHeader& header) const {
    if (!file.seek(0, SeekSet)) {
        return false;
    }
    return file.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) ==
           sizeof(header);
}

bool HistoryStore::writeHeader(File& file, const RingHeader& header) const {
    if (!file.seek(0, SeekSet)) {
        return false;
    }
    return file.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) ==
           sizeof(header);
}

bool HistoryStore::appendRecord(const RingSpec& spec, const void* record) {
    File file = LittleFS.open(spec.path, FILE_READ);
    if (!file) {
        return false;
    }

    RingHeader header = {};
    if (!readHeader(file, header)) {
        file.close();
        return false;
    }
    file.close();

    File writeFile = LittleFS.open(spec.path, "r+");
    if (!writeFile) {
        return false;
    }

    const uint32_t offset =
        sizeof(RingHeader) + (header.head * static_cast<uint32_t>(spec.recordSize));
    if (!writeFile.seek(offset, SeekSet)) {
        writeFile.close();
        return false;
    }
    if (writeFile.write(reinterpret_cast<const uint8_t*>(record), spec.recordSize) !=
        spec.recordSize) {
        writeFile.close();
        return false;
    }

    header.head = (header.head + 1) % spec.capacity;
    if (header.count < spec.capacity) {
        ++header.count;
    }
    bool ok = writeHeader(writeFile, header);
    writeFile.close();
    return ok;
}

bool HistoryStore::readLatestRecord(const RingSpec& spec, void* record,
                                    bool& found) const {
    found = false;
    File file = LittleFS.open(spec.path, FILE_READ);
    if (!file) {
        return false;
    }
    RingHeader header = {};
    if (!readHeader(file, header)) {
        file.close();
        return false;
    }
    if (header.count == 0) {
        file.close();
        return true;
    }

    const uint32_t index = (header.head + spec.capacity - 1) % spec.capacity;
    const uint32_t offset =
        sizeof(RingHeader) + index * static_cast<uint32_t>(spec.recordSize);
    if (!file.seek(offset, SeekSet)) {
        file.close();
        return false;
    }
    found = file.read(reinterpret_cast<uint8_t*>(record), spec.recordSize) ==
            spec.recordSize;
    file.close();
    return found;
}

bool HistoryStore::overwriteLatestRecord(const RingSpec& spec, const void* record) {
    File readFile = LittleFS.open(spec.path, FILE_READ);
    if (!readFile) {
        return false;
    }
    RingHeader header = {};
    if (!readHeader(readFile, header)) {
        readFile.close();
        return false;
    }
    readFile.close();

    if (header.count == 0) {
        return false;
    }

    File file = LittleFS.open(spec.path, "r+");
    if (!file) {
        return false;
    }
    const uint32_t index = (header.head + spec.capacity - 1) % spec.capacity;
    const uint32_t offset =
        sizeof(RingHeader) + index * static_cast<uint32_t>(spec.recordSize);
    if (!file.seek(offset, SeekSet)) {
        file.close();
        return false;
    }
    bool ok = file.write(reinterpret_cast<const uint8_t*>(record), spec.recordSize) ==
              spec.recordSize;
    file.close();
    return ok;
}

void HistoryStore::updateBucket(const RingSpec& spec, uint32_t bucketStart,
                                uint16_t co2) {
    BucketRecord record = {};
    bool found = false;
    if (!readLatestRecord(spec, &record, found)) {
        return;
    }

    if (found && record.startEpoch == bucketStart) {
        uint32_t total = static_cast<uint32_t>(record.avgPpm) * record.count + co2;
        record.count++;
        record.avgPpm = static_cast<uint16_t>(total / record.count);
        if (co2 < record.minPpm) {
            record.minPpm = co2;
        }
        if (co2 > record.maxPpm) {
            record.maxPpm = co2;
        }
        overwriteLatestRecord(spec, &record);
        return;
    }

    BucketRecord fresh = {};
    fresh.startEpoch = bucketStart;
    fresh.avgPpm = co2;
    fresh.minPpm = co2;
    fresh.maxPpm = co2;
    fresh.count = 1;
    appendRecord(spec, &fresh);
}

template <typename RecordType, typename VisitorType>
void HistoryStore::forEachTyped(const RingSpec& spec, VisitorType visitor,
                                void* userData, uint32_t cutoffEpoch) const {
    File file = LittleFS.open(spec.path, FILE_READ);
    if (!file) {
        return;
    }
    RingHeader header = {};
    if (!readHeader(file, header) || header.count == 0) {
        file.close();
        return;
    }

    const uint32_t startIndex =
        (header.count < spec.capacity) ? 0 : header.head % spec.capacity;
    RecordType record = {};
    for (uint32_t i = 0; i < header.count; ++i) {
        const uint32_t physicalIndex = (startIndex + i) % spec.capacity;
        const uint32_t offset =
            sizeof(RingHeader) + physicalIndex * static_cast<uint32_t>(spec.recordSize);
        if (!file.seek(offset, SeekSet)) {
            break;
        }
        if (file.read(reinterpret_cast<uint8_t*>(&record), sizeof(record)) !=
            sizeof(record)) {
            break;
        }

        if (!visitor(record, userData, cutoffEpoch)) {
            break;
        }
    }
    file.close();
}

void HistoryStore::forEachBucket(HistoryRange range, BucketVisitor visitor,
                                 void* userData, uint32_t cutoffEpoch) const {
    auto forwarder = [visitor](const BucketRecord& record, void* data,
                               uint32_t cutoff) -> bool {
        if (cutoff != 0 && record.startEpoch < cutoff) {
            return true;
        }
        return visitor(record, data);
    };
    forEachTyped<BucketRecord>(specForRange(range), forwarder, userData,
                               cutoffEpoch);
}

void HistoryStore::forEachRaw(RawVisitor visitor, void* userData,
                              uint32_t cutoffEpoch) const {
    auto forwarder = [visitor](const RawRecord& record, void* data,
                               uint32_t cutoff) -> bool {
        if (cutoff != 0 && record.epoch < cutoff) {
            return true;
        }
        return visitor(record, data);
    };
    forEachTyped<RawRecord>(kRawSpec, forwarder, userData, cutoffEpoch);
}

const HistoryStore::RingSpec& HistoryStore::specForRange(HistoryRange range) const {
    switch (range) {
        case HistoryRange::Range24h:
            return k24hSpec;
        case HistoryRange::Range7d:
            return k7dSpec;
        case HistoryRange::Range30d:
            return k30dSpec;
        case HistoryRange::Range6mo:
            return k6moSpec;
    }
    return k24hSpec;
}

uint32_t HistoryStore::windowForRange(HistoryRange range) const {
    switch (range) {
        case HistoryRange::Range24h:
            return k24hWindowSec;
        case HistoryRange::Range7d:
            return k7dWindowSec;
        case HistoryRange::Range30d:
            return k30dWindowSec;
        case HistoryRange::Range6mo:
            return k6moWindowSec;
    }
    return k24hWindowSec;
}
