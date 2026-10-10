#include "checkpoint_artifact.h"
#include "coordinator.h"
#include "content_registry.h"
#include "state_codec.h"
#include <windows.h>
#include <iostream>
#include <stdexcept>

using namespace sporemp::network;

namespace {
int checks = 0;

void check(bool value, const char* why) {
    ++checks;
    if (!value) {
        throw std::runtime_error(why);
    }
}

Digest digest(uint8_t n) {
    Digest d{};
    d.fill(n);
    return d;
}

std::vector<uint8_t> fixture(const Identity& identity, const char* bad_name = nullptr) {
    state::Writer out;
    out.word(0x39424d53);
    out.word(1);
    for (const auto* d : {&identity.build, &identity.executable, &identity.content}) {
        out.digest(*d);
    }
    const std::vector<uint8_t> native_fixture{4, 9, 2, 7};
    Digest hash{};
    content_digest(native_fixture.data(), native_fixture.size(), hash);
    for (size_t n = 0; n < world_file_count; ++n) {
        out.digest(hash);
    }
    out.digest(digest(8));
    out.wide(4);
    out.word(300000);
    for (auto id : {232u, 9601u, 1u, 9602u}) {
        out.word(id);
    }
    out.word(static_cast<uint32_t>(world_file_count));
    for (size_t n = 0; n < world_file_count; ++n) {
        std::string name = bad_name && n == 0 ? bad_name : std::string("spore/") + world_files[n].path;
        out.data(std::vector<uint8_t>(name.begin(), name.end()));
        out.digest(hash);
        out.data(native_fixture);
    }
    return std::move(out.bytes);
}

void test() {
    CoordinatorConfig config;
    config.port = 0;
    auto& identity = config.session.identity;
    identity.build = digest(1);
    identity.executable = digest(2);
    identity.content = digest(3);
    identity.fixture = digest(4);
    identity.world.fill(digest(4));
    const auto data = fixture(identity);
    CheckpointArtifact artifact;
    std::string error;
    check(validate_checkpoint_artifact(data, artifact, error), "bounded scalar artifact validates");
    for (const auto* path : {"spore/../escape", "spore/a\\b", "spore/CON.txt", "spore/a.", "other/Game0/Satiria.spo",
                             "spore//x", "spore/Planets.package"}) {
        check(!validate_checkpoint_artifact(fixture(identity, path), artifact, error),
              "unsafe/duplicate/missing archive members fail closed");
    }
    auto corrupt = data;
    corrupt.back() ^= 1;
    check(!validate_checkpoint_artifact(corrupt, artifact, error), "member byte corruption refused");
    corrupt = data;
    corrupt.push_back(0);
    check(!validate_checkpoint_artifact(corrupt, artifact, error), "trailing artifact bytes refused");
    for (size_t cut : {0u, 8u, 324u, data.size() - 1}) {
        corrupt.assign(data.begin(), data.begin() + cut);
        check(!validate_checkpoint_artifact(corrupt, artifact, error), "truncated checkpoint refused");
    }
    config.state_directory = std::filesystem::current_path() / L"checkpoint-host" /
                             (std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(config.state_directory);
    Coordinator original;
    check(original.start(config, error), "fresh durable universe starts");
    const auto player = original.peer_config(Role::player, 2);
    const auto universe = original.universe();
    original.stop();
    config.offline = true;
    Coordinator writer;
    check(writer.start(config, error), "exclusive offline checkpoint writer opens existing universe");
    check(!writer.commit_checkpoint(data, 2, error) && writer.durable_revision() == 1,
          "stale seal cannot replace unrelated state");
    check(writer.commit_checkpoint(data, 1, error) && writer.durable_revision() == 2 &&
              writer.checkpoint_revision() == 2,
          "native artifact and world/content metadata atomically acknowledge");
    check(writer.commit_checkpoint(data, 1, error) && writer.durable_revision() == 2,
          "lost acknowledgement retry is idempotent");
    check(writer.checkpoint_artifact() == data, "exact original bytes retained");
    writer.stop();
    Coordinator restored;
    check(restored.start(config, error), "base configuration recovers latest sealed world");
    check(restored.checkpoint_artifact() == data && restored.checkpoint_revision() == 2 &&
              restored.universe() == universe,
          "durable artifact and universe survive restart");
    const auto recovered = restored.peer_config(Role::player, 2);
    check(recovered.credential == player.credential && recovered.certificate_pin == player.certificate_pin &&
              recovered.identity.world != identity.world,
          "credentials remain stable while saved world advances");
    auto fresh_identity = recovered.identity;
    restored.stop();
    config.session.identity = fresh_identity;
    Coordinator latest;
    check(latest.start(config, error), "exact exported checkpoint configuration also restores");
    latest.stop();
    config.session.identity.world[1][0] ^= 1;
    Coordinator mismatch;
    check(!mismatch.start(config, error), "arbitrary requested world cannot authorize restore");
}
} // namespace

int main() {
    try {
        test();
        std::cout << "PASS " << checks << " HOST checkpoint codec and transactional restore checks; SPORE NOT RUN.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
