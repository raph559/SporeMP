// Coordinator-only local approval format. No original pointers or native APIs.
namespace {
struct GlobalCheckpointApproval {
    Digest universe{}, selected{}, candidate{}, provenance{}, proof{};
    uint64_t expected = 0, checkpoint = 0, worker = 0, generation = 0;
    LocationId location{};
    uint64_t request = 0, epoch = 0, created = 0, event = 0;
    uint32_t pid = 0, residents = 0, planet = 0;
    std::array<uint32_t, 4> native_ids{};
    std::array<uint32_t, 2> games{}, owners{};

    struct Head {
        uint64_t transfer = 0;
        Digest artifact{}, approval{};
    };

    std::array<Head, 2> heads{};
    std::array<uint32_t, 3> spice{};
};

// An encoded approval has exactly this size; the proof published with it has at most the size below.
constexpr size_t global_checkpoint_approval_bytes = 464, max_checkpoint_proof_bytes = 64 * 1024;

// The approval's fields in their encoded order, after the magic and version words.
void read_global_checkpoint_approval(state::Reader& in, GlobalCheckpointApproval& value) {
    for (auto* digest : {&value.universe, &value.selected, &value.candidate, &value.provenance, &value.proof}) {
        *digest = in.digest();
    }
    for (auto* number :
         {&value.expected, &value.checkpoint, &value.worker, &value.generation, &value.location.galaxy,
          &value.location.star, &value.location.planet, &value.location.area, &value.request, &value.epoch}) {
        *number = in.wide();
    }
    value.pid = in.word();
    value.residents = in.word();
    value.created = in.wide();
    value.event = in.wide();
    for (auto& number : value.native_ids) {
        number = in.word();
    }
    for (auto& number : value.games) {
        number = in.word();
    }
    for (auto& number : value.owners) {
        number = in.word();
    }
    for (auto& head : value.heads) {
        head.transfer = in.wide();
        head.artifact = in.digest();
        head.approval = in.digest();
    }
    value.planet = in.word();
    for (auto& number : value.spice) {
        number = in.word();
    }
}

bool global_checkpoint_approval_valid(const GlobalCheckpointApproval& value) {
    if (value.universe == Digest{} || value.selected == Digest{} || value.candidate == Digest{} ||
        value.provenance == Digest{} || value.proof == Digest{} || !value.expected || value.expected == UINT64_MAX ||
        !value.checkpoint || value.checkpoint > value.expected || !value.worker || !value.generation ||
        !value.location.valid() || !value.request || !value.epoch || !value.pid || !value.created ||
        value.event != value.expected + 1 || value.residents != 6 || value.owners != std::array<uint32_t, 2>{2, 1} ||
        !value.planet || value.planet == UINT32_MAX || value.spice == std::array<uint32_t, 3>{}) {
        return false;
    }
    for (auto number : value.native_ids) {
        if (!number || number == UINT32_MAX) {
            return false;
        }
    }
    for (auto number : value.games) {
        if (!number || number == UINT32_MAX) {
            return false;
        }
    }
    if (value.native_ids[0] == value.native_ids[2] || value.native_ids[1] == value.native_ids[3] ||
        value.games[0] == value.games[1]) {
        return false;
    }
    for (const auto& head : value.heads) {
        if (head.transfer ? (head.artifact == Digest{} || head.approval == Digest{})
                          : (head.artifact != Digest{} || head.approval != Digest{})) {
            return false;
        }
    }
    return true;
}

bool decode_global_checkpoint_approval(const std::vector<uint8_t>& bytes, GlobalCheckpointApproval& result) {
    if (bytes.size() != global_checkpoint_approval_bytes) {
        return false;
    }
    state::Reader in{bytes};
    GlobalCheckpointApproval value;
    if (in.word() != 0x4730314d || in.word() != 1) {
        return false;
    }
    read_global_checkpoint_approval(in, value);
    if (!in.done() || !global_checkpoint_approval_valid(value)) {
        return false;
    }
    result = value;
    return true;
}

// Only call after the complete checkpoint parser has validated lengths, paths,
// duplicate names and hashes. Compare actual nonowner bytes and require every
// selected member to remain present; no native save merge occurs here.
std::map<std::string, std::vector<uint8_t>> checkpoint_members(const std::vector<uint8_t>& bytes) {
    state::Reader in{bytes};
    in.word();
    in.word();
    for (size_t n = 0; n < 4 + world_file_count; ++n) {
        in.digest();
    }
    in.wide();
    in.word();
    for (unsigned n = 0; n < 4; ++n) {
        in.word();
    }
    const auto count = in.word();
    std::map<std::string, std::vector<uint8_t>> members;
    for (uint32_t n = 0; n < count; ++n) {
        const auto name = in.data(240);
        in.digest();
        auto data = in.data(CheckpointArtifact::max_bytes);
        members.emplace(std::string(name.begin(), name.end()), std::move(data));
    }
    return members;
}
} // namespace
