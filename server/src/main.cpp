// SPDX-License-Identifier: MIT
// slcamd: SuperLOTIS camera control daemon.
#include <pthread.h>
#include <signal.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "superlotis/config.hpp"
#include "superlotis/controller.hpp"
#include "superlotis/log.hpp"
#include "superlotis/protocol.hpp"
#include "superlotis/simulated_backend.hpp"
#include "superlotis/tcp_server.hpp"
#include "superlotis/version.hpp"
#ifdef SUPERLOTIS_WITH_PICAM
#include "superlotis/picam_backend.hpp"
#endif

using namespace superlotis;

namespace {

constexpr const char* kUsage = R"(Usage: slcamd [options]

SuperLOTIS camera control daemon.

Options:
  -c, --config FILE       Configuration file (INI). Defaults are used if omitted.
  -b, --backend NAME      Camera backend: simulated | picam
  -a, --bind ADDRESS      Address to listen on (default 127.0.0.1)
  -p, --port PORT         TCP port (default 6972; 0 = pick a free port)
  -d, --data-dir DIR      Directory for FITS files
      --demo              Use the PICam SDK demo camera (picam backend only)
  -s, --set KEY=VALUE     Override any setting, e.g. --set simulator.time_scale=0
  -l, --log-level LEVEL   debug | info | warn | error
      --check-config      Validate the configuration and exit
  -V, --version           Print version and exit
  -h, --help              Show this help

Exit status: 0 on clean shutdown, 1 on runtime failure, 2 on usage/config errors.
)";

struct Options {
    std::string config_path;
    std::vector<std::pair<std::string, std::string>> overrides;
    bool check_only = false;
};

int usage_error(const std::string& msg) {
    std::cerr << "slcamd: " << msg << "\nTry 'slcamd --help'.\n";
    return 2;
}

std::unique_ptr<CameraBackend> make_backend(const Config& cfg) {
    if (cfg.camera.backend == "simulated") {
        log::warn("Using the SIMULATED camera backend");
        return std::make_unique<SimulatedBackend>(cfg.simulator);
    }
    if (cfg.camera.backend == "picam") {
#ifdef SUPERLOTIS_WITH_PICAM
        return std::make_unique<PicamBackend>(cfg.picam);
#else
        throw ConfigError("this build has no PICam support; rebuild with -DSUPERLOTIS_WITH_PICAM=ON");
#endif
    }
    throw ConfigError("unknown backend '" + cfg.camera.backend + "'");
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    const std::vector<std::string> args(argv + 1, argv + argc);
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto value = [&](const char* name) -> std::string {
            if (i + 1 >= args.size()) throw std::invalid_argument(std::string(name) + " requires a value");
            return args[++i];
        };
        try {
            if (a == "-h" || a == "--help") {
                std::cout << kUsage;
                return 0;
            }
            if (a == "-V" || a == "--version") {
                std::cout << "slcamd " << SUPERLOTIS_VERSION << " (protocol " << SUPERLOTIS_PROTOCOL_VERSION
#ifdef SUPERLOTIS_WITH_PICAM
                          << ", picam"
#endif
                          << ")\n";
                return 0;
            }
            if (a == "-c" || a == "--config")
                opt.config_path = value("--config");
            else if (a == "-b" || a == "--backend")
                opt.overrides.emplace_back("camera.backend", value("--backend"));
            else if (a == "-a" || a == "--bind")
                opt.overrides.emplace_back("server.bind_address", value("--bind"));
            else if (a == "-p" || a == "--port")
                opt.overrides.emplace_back("server.port", value("--port"));
            else if (a == "-d" || a == "--data-dir")
                opt.overrides.emplace_back("storage.data_dir", value("--data-dir"));
            else if (a == "-l" || a == "--log-level")
                opt.overrides.emplace_back("log.level", value("--log-level"));
            else if (a == "--demo")
                opt.overrides.emplace_back("picam.demo", "true");
            else if (a == "--check-config")
                opt.check_only = true;
            else if (a == "-s" || a == "--set") {
                const std::string kv = value("--set");
                const auto eq = kv.find('=');
                if (eq == std::string::npos) return usage_error("--set expects KEY=VALUE");
                opt.overrides.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
            } else {
                return usage_error("unknown option '" + a + "'");
            }
        } catch (const std::invalid_argument& e) {
            return usage_error(e.what());
        }
    }

    Config cfg;
    try {
        cfg = opt.config_path.empty() ? Config{} : Config::load(opt.config_path);
        for (const auto& [k, v] : opt.overrides) cfg.apply(k, v, "command line");
        cfg.validate();
    } catch (const ConfigError& e) {
        std::cerr << "slcamd: configuration error: " << e.what() << "\n";
        return 2;
    }
    if (opt.check_only) {
        std::cout << "configuration OK\n";
        return 0;
    }
    log::set_level(*log::parse_level(cfg.log_level));
    log::info("slcamd ", SUPERLOTIS_VERSION, " starting");

    // Block termination signals in every thread; a dedicated thread waits for them.
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &signals, nullptr);
    ::signal(SIGPIPE, SIG_IGN);

    try {
        CameraController controller(make_backend(cfg), cfg);
        protocol::CommandHandler handler(controller, cfg.server);
        TcpServer server(handler, cfg.server);
        server.start();

        std::atomic<bool> finished{false};
        std::thread signal_thread([&] {
            const timespec tick{0, 200'000'000};
            while (!finished.load()) {
                const int sig = sigtimedwait(&signals, nullptr, &tick);
                if (sig > 0) {
                    log::info("Received ", sig == SIGINT ? "SIGINT" : "SIGTERM", ", shutting down");
                    server.stop();
                    break;
                }
            }
        });
        server.run();
        finished.store(true);
        signal_thread.join();
    } catch (const ConfigError& e) {
        log::error("configuration error: ", e.what());
        return 2;
    } catch (const std::exception& e) {
        log::error("fatal: ", e.what());
        return 1;
    }
    log::info("slcamd stopped cleanly");
    return 0;
}
