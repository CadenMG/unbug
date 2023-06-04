#include <vector>
#include <sstream>
#include <optional>

std::vector<std::string> split(const std::string &s, char delimiter) {
    std::vector<std::string> out{};
    std::stringstream ss {s};
    std::string item;

    while (std::getline(ss,item,delimiter)) {
        out.push_back(item);
    }

    return out;
}

bool is_prefix(const std::string& s, const std::string& of) {
    if (s.size() > of.size()) return false;
    return std::equal(s.begin(), s.end(), of.begin());
}

inline u_int8_t bottom_byte(u_int64_t data) {
    return static_cast<uint8_t>(data & 0xff);
}

inline u_int64_t set_bottom_byte(u_int64_t data, u_int64_t bottom_byte) {
    return ((data & ~0xff) | bottom_byte);
}

std::optional<std::uint64_t> parse_hex(std::string& hex) {
    if (!is_prefix("0x", hex) || hex.size() == 2) {
        return std::nullopt;
    }
    return std::stol(hex.substr(2), 0, 16);
}

std::optional<std::intptr_t> parse_addr(std::string& addr) {
    return parse_hex(addr);
}
