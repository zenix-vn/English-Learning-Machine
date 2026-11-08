#include "text_util.h"

namespace offline {

namespace {
bool IsContinuation(unsigned char c) { return (c & 0xC0) == 0x80; }

// Byte offset of the code point with index `n` (or the end of the string).
size_t ByteOffset(const std::string& s, size_t n) {
    size_t i = 0;
    while (i < s.size() && n > 0) {
        ++i;
        while (i < s.size() && IsContinuation(static_cast<unsigned char>(s[i]))) ++i;
        --n;
    }
    return i;
}

std::string Trim(std::string s) {
    const char* ws = " \t\r\n";
    const size_t a = s.find_first_not_of(ws);
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(ws) - a + 1);
}
}  // namespace

size_t Utf8Length(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) n += !IsContinuation(c);
    return n;
}

std::string ShortMeaning(const std::string& meaning, size_t max_chars) {
    std::string s = Trim(meaning);
    if (Utf8Length(s) <= max_chars) return s;
    const std::string head = s.substr(0, ByteOffset(s, max_chars));
    const size_t floor = ByteOffset(s, max_chars / 2);              // do not cut away more than half of the budget
    for (char sep : {';', ','}) {
        const size_t pos = head.rfind(sep);
        if (pos != std::string::npos && pos >= floor) return Trim(head.substr(0, pos));
    }
    const size_t space = head.rfind(' ');
    return Trim(space != std::string::npos && space >= floor ? head.substr(0, space) : head) + "...";
}

void SplitExample(const std::string& example, std::string& english, std::string& vietnamese) {
    static const std::string kSep = " \xE2\x80\x94 ";                // " — "
    const size_t pos = example.find(kSep);
    if (pos == std::string::npos) {
        english = Trim(example);
        vietnamese.clear();
    } else {
        english = Trim(example.substr(0, pos));
        vietnamese = Trim(example.substr(pos + kSep.size()));
    }
}

std::string FormatInterval(uint32_t minutes) {
    if (minutes < 60) return "gặp lại sau " + std::to_string(minutes < 1 ? 1 : minutes) + " phút";
    if (minutes < 1440) return "gặp lại sau " + std::to_string((minutes + 30) / 60) + " giờ";
    const uint32_t days = (minutes + 720) / 1440;
    if (days < 60) return "gặp lại sau " + std::to_string(days) + " ngày";
    return "gặp lại sau " + std::to_string((days + 15) / 30) + " tháng";
}

int WordFontSize(size_t chars, bool narrow) {
    if (narrow) return chars <= 7 ? 64 : chars <= 11 ? 44 : 28;
    return chars <= 16 ? 64 : chars <= 24 ? 44 : 28;
}

}  // namespace offline
