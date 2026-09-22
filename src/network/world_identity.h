#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace sporemp::network {
using Digest = std::array<uint8_t, 32>;
// Version 1 is an intentionally conservative, fixed closed-file bundle. These
// are identifiers, never paths supplied by a peer. Native terrain correlation
// and a complete semantic dependency graph remain separate acceptance gates.
constexpr size_t world_file_count = 6;
struct WorldFile { const char* field; const char* path; };
inline constexpr std::array<WorldFile, world_file_count> world_files{{
    {"world_satiria_sha256", "Games/Game0/Satiria.spo"},
    {"world_planet_records_sha256", "Games/Game0/planetRecords.pkp"},
    {"world_planet_records_temp_sha256", "Games/Game0/planetRecords.pkt"},
    {"world_planet_scripts_sha256", "Games/Game0/PlanetScripts.pld"},
    {"world_stars_sha256", "Games/Game0/stars.db"},
    {"world_planets_sha256", "Planets.package"}
}};
using WorldIdentity = std::array<Digest, world_file_count>;
bool valid_world_identity(const WorldIdentity&) noexcept;
// Reads actual bytes while all six regular, local files are held against writes
// and deletion. Releases every handle before native startup; does not install,
// repair, import, or load content. Missing/unsafe/oversized files fail closed.
bool read_world_identity(const std::wstring& spore_root, WorldIdentity&, std::string& error);
}
