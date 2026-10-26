#include "platform/NightLight.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace bb::NightLight {
namespace {

#define BB_STORE L"Software\\Microsoft\\Windows\\CurrentVersion\\CloudStore\\Store\\DefaultAccount\\Current\\"
constexpr const wchar_t* Keys[2] = {
    BB_STORE L"default$windows.data.bluelightreduction.bluelightreductionstate\\windows.data.bluelightreduction.bluelightreductionstate",
    BB_STORE L"default$windows.data.bluelightreduction.settings\\windows.data.bluelightreduction.settings",
};
#undef BB_STORE

std::vector<uint8_t> ReadData(const wchar_t* key)
{
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, key, L"Data", RRF_RT_REG_BINARY, nullptr, nullptr, &size) != ERROR_SUCCESS || size > 4096) return {};
    std::vector<uint8_t> data(size);
    if (RegGetValueW(HKEY_CURRENT_USER, key, L"Data", RRF_RT_REG_BINARY, nullptr, data.data(), &size) != ERROR_SUCCESS) return {};
    data.resize(size);
    return data;
}

// The values are Bond "compact binary": each field a header byte (type in the low 5 bits, id in the top 3, or in the
// next one or two bytes), then its value; a struct ends with a 0 byte.
class Reader {
public:
    Reader(const std::vector<uint8_t>& data, size_t at) : d(data), at(at) {}
    bool ok = true;

    // The next field of the struct being read; false at its end (or on a problem: see ok).
    bool Field(int& type, int& id)
    {
        int h = Byte();
        type = h & 0x1F;
        id = h >> 5;
        if (id == 6) id = Byte();
        else if (id == 7) id = Byte() | Byte() << 8;
        return ok && type != 0;
    }

    // A whole-number value of the given type (and false for types these values don't use).
    bool Number(int type, int64_t& out)
    {
        switch (type) {
        case 2: case 3: out = Byte(); return ok;                                  // bool, uint8
        case 14: out = static_cast<int8_t>(Byte()); return ok;                     // int8
        case 4: case 5: case 6: out = static_cast<int64_t>(Varint()); return ok;   // uint16..64
        case 15: case 16: case 17: {                                               // int16..64 (zigzag)
            uint64_t v = Varint();
            out = static_cast<int64_t>(v >> 1) ^ -static_cast<int64_t>(v & 1);
            return ok;
        }
        }
        ok = false;
        return false;
    }

    // A time: a struct of hour (id 0) and minute (id 1, left out when 0); -1 if it doesn't look like one.
    int Time()
    {
        int type, id, hour = -1, minute = 0;
        int64_t v;
        while (Field(type, id))
            if (!Number(type, v)) return -1;
            else if (id == 0) hour = static_cast<int>(v);
            else if (id == 1) minute = static_cast<int>(v);
        return ok && hour >= 0 && hour < 24 && minute >= 0 && minute < 60 ? hour * 60 + minute : -1;
    }

    void Skip(int type)
    {
        int64_t v;
        if (type != 10) {
            Number(type, v);
            return;
        }
        int t, id;
        while (Field(t, id)) Skip(t);
    }

private:
    int Byte()
    {
        if (at >= d.size()) {
            ok = false;
            return 0;
        }
        return d[at++];
    }

    uint64_t Varint()
    {
        uint64_t v = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            int b = Byte();
            v |= static_cast<uint64_t>(b & 0x7F) << shift;
            if (!(b & 0x80)) return v;
        }
        ok = false;
        return v;
    }

    const std::vector<uint8_t>& d;
    size_t at;
};

// Where the fields start: after the second "CB" header (the first one opens the whole value).
size_t Fields(const std::vector<uint8_t>& d)
{
    static const uint8_t mark[] = {0x43, 0x42, 0x01, 0x00};
    if (d.size() < 8 || !std::equal(mark, mark + 4, d.begin())) return 0;
    auto inner = std::search(d.begin() + 4, d.end(), mark, mark + 4);
    return inner == d.end() ? 0 : static_cast<size_t>(inner - d.begin()) + 4;
}

} // namespace

int State::Strength() const { return std::clamp(static_cast<int>(std::lround((6500 - kelvin) / 53.0)), 0, 100); }

int BlueFor(int kelvin) { return std::clamp(static_cast<int>(std::lround((6500 - kelvin) / 46.0)), 0, 100); }

State Read() { return Parse(ReadData(Keys[1]), ReadData(Keys[0])); }

State Parse(const std::vector<uint8_t>& settings, const std::vector<uint8_t>& state)
{
    State s;
    size_t at = Fields(settings);
    if (!at) return s;
    Reader r(settings, at);
    int type, id;
    int64_t v;
    s.kelvin = 6500 - 53 * 48;   // (Windows leaves out a value that is its default: strength 48)
    bool sunsetMode = true;
    while (r.Field(type, id)) {
        if (type == 10) {
            if (id != 20 && id != 30 && id != 50 && id != 60) {
                r.Skip(type);   // (something newer: not used)
                continue;
            }
            int time = r.Time();
            if (time < 0) return State{};   // a time that isn't one: the value isn't what it seems
            (id == 20 ? s.from : id == 30 ? s.to : id == 50 ? s.sunset : s.sunrise) = time;
        } else if (!r.Number(type, v)) {
            break;
        } else if (id == 0) s.scheduled = v != 0;
        else if (id == 10) sunsetMode = v != 0;
        else if (id == 40) s.kelvin = static_cast<int>(v);
    }
    if (!r.ok || s.kelvin < 1000 || s.kelvin > 6500) return State{};
    s.setHours = !sunsetMode;
    if (s.setHours && s.from < 0) s.from = 21 * 60;   // (left out: Windows' defaults)
    if (s.setHours && s.to < 0) s.to = 7 * 60;
    if (s.scheduled && (s.Start() < 0 || s.End() < 0)) s.scheduled = false;   // no times to follow

    // On or off now: field 0 is there (as 0) while it is on; field 10 when it was set by hand.
    if (size_t st = Fields(state)) {
        Reader q(state, st);
        bool on = false, hand = false;
        while (q.Field(type, id)) {
            if (type == 10) q.Skip(type);
            else if (!q.Number(type, v)) break;
            else if (id == 0) on = v == 0;
            else if (id == 10) hand = v != 0;
        }
        if (q.ok) {
            s.on = on;
            s.byHand = hand;
        }
    }
    s.found = true;
    return s;
}

} // namespace bb::NightLight
