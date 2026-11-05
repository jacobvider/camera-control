// SPDX-License-Identifier: MIT
#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>

#include "spy_backend.hpp"
#include "superlotis/protocol.hpp"

using namespace superlotis;
using namespace superlotis::protocol;
namespace fs = std::filesystem;

TEST(ParseRequest, CommandsArgumentsAndOptions) {
    auto r = parse_request("  EXPOSE exptime=5 object=M31 \r");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->command, "expose");
    EXPECT_TRUE(r->positional.empty());
    EXPECT_EQ(r->named.at("exptime"), "5");
    EXPECT_EQ(r->named.at("object"), "M31");

    r = parse_request("burst 3");
    EXPECT_EQ(r->positional, std::vector<std::string>{"3"});
}

TEST(ParseRequest, LegacyEqualsForm) {
    const auto r = parse_request("exptime=2.5");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->command, "exptime");
    EXPECT_EQ(r->positional, std::vector<std::string>{"2.5"});
}

TEST(ParseRequest, BlankAndMalformed) {
    EXPECT_FALSE(parse_request("   "));
    EXPECT_THROW(parse_request("=5"), CommandError);
    EXPECT_THROW(parse_request("expose =5"), CommandError);
    EXPECT_THROW(parse_request("expose a=1 a=2"), CommandError);
    EXPECT_THROW(parse_request(std::string("ping\x01")), CommandError);
}

TEST(Encoding, PercentRoundTrip) {
    const std::string raw = "/data/my dir/a=b,c%d";
    const std::string enc = percent_encode(raw);
    EXPECT_EQ(enc.find_first_of(" =,"), std::string::npos);
    EXPECT_EQ(percent_decode(enc), raw);
    EXPECT_EQ(percent_decode("100%"), "100%");
}

TEST(Response, WireFormat) {
    EXPECT_EQ(Response::success().wire(), "OK\n");
    EXPECT_EQ(Response::success("1.000").wire(), "OK 1.000\n");
    EXPECT_EQ(Response::failure(ErrorCode::Busy, "line1\nline2").wire(), "ERR busy line1 line2\n");
}

namespace {
class HandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir = fs::temp_directory_path() / ("slcam_proto_" + std::to_string(::getpid()));
        fs::remove_all(dir);
        cfg.storage.data_dir = dir;
        state = std::make_shared<test_support::SpyBackend::State>();
        controller =
            std::make_unique<CameraController>(std::make_unique<test_support::SpyBackend>(state), cfg);
    }
    void TearDown() override { fs::remove_all(dir); }
    std::string send(const std::string& line, ServerConfig sc = {}) {
        CommandHandler h(*controller, sc);
        return h.handle_line(line, session).wire();
    }
    fs::path dir;
    Config cfg;
    std::shared_ptr<test_support::SpyBackend::State> state;
    std::unique_ptr<CameraController> controller;
    Session session;
};
}  // namespace

TEST_F(HandlerTest, GettersAndSetters) {
    EXPECT_EQ(send("exptime 2.5"), "OK 2.500\n");
    EXPECT_EQ(send("exptime"), "OK 2.500\n");
    EXPECT_EQ(send("gain=low"), "OK low\n");
    EXPECT_EQ(send("analog_gain"), "OK low\n");
    EXPECT_EQ(send("shutter 2"), "OK closed\n");
    EXPECT_EQ(send("setpoint -70"), "OK -70.00\n");
    EXPECT_EQ(send("temp"), "OK reading=-60.00 setpoint=-70.00 locked=1\n");
}

TEST_F(HandlerTest, ErrorsHaveStableCodes) {
    EXPECT_EQ(send("frobnicate").rfind("ERR unknown_command", 0), 0u);
    EXPECT_EQ(send("exptime abc").rfind("ERR invalid_argument", 0), 0u);
    EXPECT_EQ(send("exptime 1000").rfind("ERR out_of_range", 0), 0u);
    EXPECT_EQ(send("gain ultra").rfind("ERR invalid_argument", 0), 0u);
    EXPECT_EQ(send("expose 5").rfind("ERR bad_request", 0), 0u);
    EXPECT_EQ(send("expose color=red").rfind("ERR invalid_argument", 0), 0u);
    EXPECT_EQ(send("expose object=bad/name").rfind("ERR invalid_argument", 0), 0u);
    EXPECT_EQ(send("bias exptime=5").rfind("ERR invalid_argument", 0), 0u);
    EXPECT_EQ(send("burst").rfind("ERR invalid_argument", 0), 0u);
    EXPECT_EQ(send("burst 0").rfind("ERR out_of_range", 0), 0u);
    EXPECT_EQ(send("shutdown").rfind("ERR not_permitted", 0), 0u);
}

TEST_F(HandlerTest, ExposeReturnsFile) {
    const std::string r = send("expose exptime=3 object=M31");
    ASSERT_EQ(r.rfind("OK file=", 0), 0u) << r;
    EXPECT_NE(r.find("type=object"), std::string::npos);
    EXPECT_DOUBLE_EQ(state->acquires.back().exposure_s, 3.0);
    const std::string b = send("burst 2");
    EXPECT_EQ(b.rfind("OK count=2 files=", 0), 0u) << b;
}

TEST_F(HandlerTest, StatusIsKeyValue) {
    const std::string s = send("status");
    for (const char* key :
         {"state=idle", "exptime=", "shutter=", "gain=", "temp=", "locked=", "frames=0", "last_file=-"})
        EXPECT_NE(s.find(key), std::string::npos) << key << " missing in " << s;
}

TEST_F(HandlerTest, AuthenticationGate) {
    ServerConfig sc;
    sc.auth_token = "tok";
    CommandHandler h(*controller, sc);
    Session s;
    EXPECT_EQ(h.handle_line("ping", s).wire(), "OK pong\n");
    EXPECT_EQ(h.handle_line("status", s).wire().rfind("ERR unauthorized", 0), 0u);
    EXPECT_EQ(h.handle_line("expose", s).wire().rfind("ERR unauthorized", 0), 0u);
    EXPECT_EQ(h.handle_line("auth wrong", s).wire().rfind("ERR unauthorized", 0), 0u);
    EXPECT_EQ(h.handle_line("auth tok", s).wire(), "OK authenticated\n");
    EXPECT_EQ(h.handle_line("status", s).wire().rfind("OK state=", 0), 0u);
    EXPECT_TRUE(state->acquires.empty());
}

TEST_F(HandlerTest, QuitAndShutdownFlags) {
    ServerConfig sc;
    sc.allow_remote_shutdown = true;
    CommandHandler h(*controller, sc);
    const auto q = h.handle_line("quit", session);
    EXPECT_TRUE(q.close_connection);
    EXPECT_FALSE(q.shutdown_server);
    const auto s = h.handle_line("shutdown", session);
    EXPECT_TRUE(s.shutdown_server);
}
