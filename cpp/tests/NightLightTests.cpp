// Windows Night light's stored values, as recorded on a real PC while each setting was changed by hand
// (docs/NIGHTLIGHT.md), and damaged versions of them that must read as "not found".
#include "Check.h"
#include "platform/NightLight.h"
#include <algorithm>
#include <sstream>

using bb::NightLight::State;

namespace {

// A recorded value: the fields after the header and timestamp, with a header put back in front.
std::vector<uint8_t> Blob(const char* hex)
{
    std::vector<uint8_t> b{0x43, 0x42, 0x01, 0x00, 0x0A, 0x02, 0x01, 0x00, 0x2A, 0x06, 0x01};
    std::istringstream in(hex);
    std::string byte;
    while (in >> byte) b.push_back(static_cast<uint8_t>(std::stoi(byte, nullptr, 16)));
    return b;
}

// The on/off value: off and set by hand; on and set by hand; off by the schedule.
const char* const OffByHand = "2A 2B 0E 13 43 42 01 00 D0 0A 02 C6 14 D5 A2 CA D9 B6 A6 D9 EE 01 00 00 00 00";
const char* const OnByHand = "2A 2B 0E 15 43 42 01 00 10 00 D0 0A 02 C6 14 AC AC B2 AA F3 A6 D9 EE 01 00 00 00 00";
const char* const OffBySchedule = "2A 2B 0E 10 43 42 01 00 C6 14 F5 87 B3 9E FA A6 D9 EE 01 00 00 00 00";

// The settings: no schedule at strengths 78, 0, 100 and 50; sunset to sunrise; set hours 21:00 and 22:30.
const char* const Strength78 = "2A 2B 0E 21 43 42 01 00 CA 14 0E 15 00 CA 1E 0E 07 00 CF 28 FC 24 CA 32 0E 12 2E 31 00 CA 3C 0E 06 2E 25 00 00 00 00 00";
const char* const Strength0 = "2A 2B 0E 21 43 42 01 00 CA 14 0E 15 00 CA 1E 0E 07 00 CF 28 C8 65 CA 32 0E 12 2E 31 00 CA 3C 0E 06 2E 25 00 00 00 00 00";
const char* const Strength100 = "2A 2B 0E 21 43 42 01 00 CA 14 0E 15 00 CA 1E 0E 07 00 CF 28 E0 12 CA 32 0E 12 2E 31 00 CA 3C 0E 06 2E 25 00 00 00 00 00";
const char* const Strength50 = "2A 2B 0E 21 43 42 01 00 CA 14 0E 15 00 CA 1E 0E 07 00 CF 28 94 3C CA 32 0E 12 2E 31 00 CA 3C 0E 06 2E 25 00 00 00 00 00";
const char* const Sunset = "2A 2B 0E 23 43 42 01 00 02 01 CA 14 0E 15 00 CA 1E 0E 07 00 CF 28 94 3C CA 32 0E 12 2E 31 00 CA 3C 0E 06 2E 25 00 00 00 00 00";
const char* const Hours21 = "2A 2B 0E 26 43 42 01 00 02 01 C2 0A 00 CA 14 0E 15 00 CA 1E 0E 07 00 CF 28 94 3C CA 32 0E 12 2E 31 00 CA 3C 0E 06 2E 25 00 00 00 00 00";
const char* const Hours2230 = "2A 2B 0E 28 43 42 01 00 02 01 C2 0A 00 CA 14 0E 16 2E 1E 00 CA 1E 0E 07 00 CF 28 94 3C CA 32 0E 12 2E 31 00 CA 3C 0E 06 2E 25 00 00 00 00 00";

State Read(const char* settings, const char* state) { return bb::NightLight::Parse(Blob(settings), Blob(state)); }

} // namespace

TEST(NightLightOnAndOff)
{
    State off = Read(Strength78, OffByHand), on = Read(Strength78, OnByHand), scheduled = Read(Sunset, OffBySchedule);
    CHECK(off.found && !off.on && off.byHand);
    CHECK(on.found && on.on && on.byHand);
    CHECK(scheduled.found && !scheduled.on && !scheduled.byHand);
}

TEST(NightLightStrength)
{
    CHECK_EQ(Read(Strength78, OffByHand).kelvin, 2366);
    CHECK_EQ(Read(Strength78, OffByHand).Strength(), 78);
    CHECK_EQ(Read(Strength0, OnByHand).kelvin, 6500);
    CHECK_EQ(Read(Strength0, OnByHand).Strength(), 0);
    CHECK_EQ(Read(Strength100, OnByHand).kelvin, 1200);
    CHECK_EQ(Read(Strength100, OnByHand).Strength(), 100);
    CHECK_EQ(Read(Strength50, OnByHand).kelvin, 3850);
    CHECK_EQ(Read(Strength50, OnByHand).Strength(), 50);
    CHECK_EQ(bb::NightLight::BlueFor(3850), 58);
}

TEST(NightLightSchedule)
{
    State none = Read(Strength50, OnByHand), sun = Read(Sunset, OffBySchedule);
    State hours = Read(Hours21, OffBySchedule), later = Read(Hours2230, OffBySchedule);
    CHECK(!none.scheduled);
    CHECK(sun.scheduled && !sun.setHours);
    CHECK_EQ(sun.Start(), 18 * 60 + 49);
    CHECK_EQ(sun.End(), 6 * 60 + 37);
    CHECK(hours.scheduled && hours.setHours);
    CHECK_EQ(hours.Start(), 21 * 60);
    CHECK_EQ(hours.End(), 7 * 60);
    CHECK_EQ(later.Start(), 22 * 60 + 30);
}

TEST(NightLightDamagedReadsAsNotFound)
{
    std::vector<uint8_t> full = Blob(Hours2230), off = Blob(OffBySchedule), none;
    auto at = [&](std::initializer_list<uint8_t> bytes) {
        return static_cast<size_t>(std::search(full.begin(), full.end(), bytes.begin(), bytes.end()) - full.begin());
    };
    auto broken = [&](size_t index, uint8_t value) {
        std::vector<uint8_t> b = full;
        b[index] = value;
        return bb::NightLight::Parse(b, off);
    };
    CHECK(!broken(at({0xCA, 0x14, 0x0E}) + 3, 25).found);   // hour 25
    CHECK(!broken(at({0x2E, 0x1E}) + 1, 75).found);         // minute 75
    CHECK(!broken(at({0xCF, 0x28}), 0xD9).found);           // a field type that doesn't exist
    CHECK(!broken(at({0xCF, 0x28}) + 2, 0x05).found);       // 2 K
    CHECK(!bb::NightLight::Parse(none, none).found);
    CHECK(!bb::NightLight::Parse(std::vector<uint8_t>(40, 0xFF), none).found);
    std::vector<uint8_t> newer = full;   // a field Windows might add one day is passed over
    newer.insert(newer.begin() + at({0xCF, 0x28}), {0xCA, 0x46, 0x0E, 0x01, 0x00});
    State extra = bb::NightLight::Parse(newer, off);
    CHECK(extra.found);
    CHECK_EQ(extra.kelvin, 3850);
}
