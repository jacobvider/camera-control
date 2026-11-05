// SPDX-License-Identifier: MIT
#include "superlotis/fits_writer.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace superlotis::fits {

namespace {

constexpr std::size_t kCardLen = 80;
constexpr std::size_t kBlockLen = 2880;

bool valid_keyword(const std::string& k) {
    if (k.empty() || k.size() > 8) return false;
    return std::all_of(k.begin(), k.end(), [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

bool reserved_keyword(const std::string& k) {
    static const std::array<const char*, 11> reserved = {"SIMPLE", "BITPIX",  "NAXIS",  "NAXIS1",
                                                         "NAXIS2", "EXTEND",  "BZERO",  "BSCALE",
                                                         "END",    "HISTORY", "COMMENT"};
    return std::any_of(reserved.begin(), reserved.end(), [&](const char* r) { return k == r; });
}

/// FITS headers are restricted to printable ASCII.
std::string sanitize(const std::string& s) {
    std::string out(s);
    for (char& c : out)
        if (c < 0x20 || c > 0x7E) c = '?';
    return out;
}

std::string right_justify(const std::string& s, std::size_t width) {
    return s.size() >= width ? s : std::string(width - s.size(), ' ') + s;
}

std::string format_double(double v) {
    if (!std::isfinite(v)) throw std::invalid_argument("FITS values must be finite");
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.12G", v);
    std::string s(buf);
    if (s.find_first_of(".E") == std::string::npos) s += ".0";
    return s;
}

std::string format_value(const Value& v) {
    if (const auto* b = std::get_if<bool>(&v)) return right_justify(*b ? "T" : "F", 20);
    if (const auto* i = std::get_if<long long>(&v)) return right_justify(std::to_string(*i), 20);
    if (const auto* d = std::get_if<double>(&v)) return right_justify(format_double(*d), 20);
    // String: single quotes doubled, padded to >= 8 characters inside the quotes.
    // Truncate to fit the card (68 chars between the quotes) without ever
    // splitting an escaped quote pair.
    std::string inner;
    for (char c : sanitize(std::get<std::string>(v))) {
        const std::size_t need = (c == '\'') ? 2 : 1;
        if (inner.size() + need > 68) break;
        inner += c;
        if (c == '\'') inner += '\'';
    }
    if (inner.size() < 8) inner.append(8 - inner.size(), ' ');
    return "'" + inner + "'";
}

std::string pad_card(std::string card) {
    if (card.size() > kCardLen) card.resize(kCardLen);
    card.append(kCardLen - card.size(), ' ');
    return card;
}

void write_all(int fd, const char* data, std::size_t len) {
    while (len > 0) {
        const ssize_t n = ::write(fd, data, len);
        if (n < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error(std::string("write failed: ") + std::strerror(errno));
        }
        data += n;
        len -= static_cast<std::size_t>(n);
    }
}

}  // namespace

void Header::set(const std::string& k, bool v, std::string c) {
    set_value(k, Value{v}, std::move(c));
}
void Header::set(const std::string& k, int v, std::string c) {
    set_value(k, Value{static_cast<long long>(v)}, std::move(c));
}
void Header::set(const std::string& k, long long v, std::string c) {
    set_value(k, Value{v}, std::move(c));
}
void Header::set(const std::string& k, double v, std::string c) {
    set_value(k, Value{v}, std::move(c));
}
void Header::set(const std::string& k, const std::string& v, std::string c) {
    set_value(k, Value{v}, std::move(c));
}
void Header::set(const std::string& k, const char* v, std::string c) {
    set_value(k, Value{std::string(v)}, std::move(c));
}

void Header::set_value(const std::string& keyword, Value value, std::string comment) {
    if (!valid_keyword(keyword)) throw std::invalid_argument("invalid FITS keyword: '" + keyword + "'");
    if (reserved_keyword(keyword)) throw std::invalid_argument("reserved FITS keyword: " + keyword);
    if (const auto* d = std::get_if<double>(&value); d && !std::isfinite(*d))
        throw std::invalid_argument("non-finite value for FITS keyword " + keyword);
    for (auto& c : cards_) {
        if (c.keyword == keyword) {
            c.value = std::move(value);
            c.comment = std::move(comment);
            return;
        }
    }
    cards_.push_back(Card{keyword, std::move(value), std::move(comment)});
}

void Header::add_history(const std::string& text) {
    const std::string clean = sanitize(text);
    for (std::size_t i = 0; i == 0 || i < clean.size(); i += 72)
        commentary_.push_back(pad_card("HISTORY " + clean.substr(i, 72)));
}

void Header::add_comment(const std::string& text) {
    const std::string clean = sanitize(text);
    for (std::size_t i = 0; i == 0 || i < clean.size(); i += 72)
        commentary_.push_back(pad_card("COMMENT " + clean.substr(i, 72)));
}

std::string format_card(const Card& card) {
    std::string out = card.keyword;
    out.append(8 - out.size(), ' ');
    out += "= ";
    out += format_value(card.value);
    if (!card.comment.empty()) out += " / " + sanitize(card.comment);
    return pad_card(out);
}

std::vector<char> encode(const Frame& frame, const Header& header) {
    if (!frame.valid()) throw std::invalid_argument("cannot encode an empty or inconsistent frame");

    std::string hdr;
    auto add = [&](const std::string& k, Value v, const std::string& c) {
        hdr += format_card(Card{k, std::move(v), c});
    };
    add("SIMPLE", true, "conforms to FITS standard");
    add("BITPIX", 16LL, "16-bit integers");
    add("NAXIS", 2LL, "number of data axes");
    add("NAXIS1", static_cast<long long>(frame.width), "columns");
    add("NAXIS2", static_cast<long long>(frame.height), "rows");
    add("EXTEND", true, "file may contain extensions");
    add("BZERO", 32768LL, "offset for unsigned 16-bit data");
    add("BSCALE", 1LL, "data scaling");
    for (const auto& c : header.cards()) hdr += format_card(c);
    for (const auto& c : header.commentary()) hdr += c;
    hdr += pad_card("END");
    hdr.append((kBlockLen - hdr.size() % kBlockLen) % kBlockLen, ' ');

    const std::size_t data_len = frame.pixels.size() * 2;
    std::vector<char> out;
    out.reserve(hdr.size() + data_len + kBlockLen);
    out.insert(out.end(), hdr.begin(), hdr.end());
    for (const std::uint16_t px : frame.pixels) {
        // Stored value = physical - BZERO; for uint16 that is just a sign-bit flip.
        const std::uint16_t stored = static_cast<std::uint16_t>(px ^ 0x8000U);
        out.push_back(static_cast<char>(stored >> 8U));  // FITS is big-endian
        out.push_back(static_cast<char>(stored & 0xFFU));
    }
    out.resize(out.size() + (kBlockLen - data_len % kBlockLen) % kBlockLen, '\0');
    return out;
}

void write(const std::filesystem::path& path, const Frame& frame, const Header& header) {
    const std::vector<char> bytes = encode(frame, header);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());

    const std::filesystem::path tmp = path.string() + ".part";
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd < 0) throw std::runtime_error("cannot create " + tmp.string() + ": " + std::strerror(errno));
    try {
        write_all(fd, bytes.data(), bytes.size());
        if (::fsync(fd) != 0) throw std::runtime_error(std::string("fsync failed: ") + std::strerror(errno));
    } catch (...) {
        ::close(fd);
        ::unlink(tmp.c_str());
        throw;
    }
    ::close(fd);
    // link() fails with EEXIST instead of silently replacing an existing frame.
    if (::link(tmp.c_str(), path.c_str()) != 0) {
        const int err = errno;
        ::unlink(tmp.c_str());
        throw std::runtime_error("cannot publish " + path.string() + ": " + std::strerror(err));
    }
    ::unlink(tmp.c_str());
}

}  // namespace superlotis::fits
