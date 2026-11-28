#include "Agent.h"
#include "Bridge.h"
#include "services/Fs.h"
#include "services/Search.h"
#include "services/System.h"
#include "services/Watch.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <span>
#include <string_view>

namespace
{

int usage()
{
    std::fputs("usage: kode-remote bridge | serve | daemon [--idle seconds] | --version\n", stderr);
    return EXIT_FAILURE;
}

void addServices(kode::remote::Agent &agent)
{
    agent.addService(std::make_unique<kode::remote::System>());
    agent.addService(std::make_unique<kode::remote::Fs>());
    agent.addService(std::make_unique<kode::remote::Search>());
    agent.addService(std::make_unique<kode::remote::Watch>(agent.hooks()));
}

} // namespace

int main(int argc, char *argv[])
{
    const std::span<char *> arguments{argv, static_cast<std::size_t>(argc)};
    if (arguments.size() < 2) {
        return usage();
    }
    const std::string_view command{arguments[1]};
    if (command == "--version") {
        std::puts(KODE_REMOTE_VERSION);
        return EXIT_SUCCESS;
    }
    if (command == "bridge") {
        return kode::remote::bridge(arguments[0]);
    }
    kode::remote::Agent agent;
    addServices(agent);
    if (command == "serve") {
        return agent.serve();
    }
    if (command != "daemon") {
        return usage();
    }
    constexpr std::chrono::seconds defaultIdle{600};
    std::chrono::seconds idle{defaultIdle};
    if (arguments.size() == 4 && std::string_view{arguments[2]} == "--idle") {
        idle = std::chrono::seconds{std::strtol(arguments[3], nullptr, 10)};
    }
    return agent.daemon(idle);
}
