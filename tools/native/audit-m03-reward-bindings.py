"""Pin researched lifecycle/handler addresses; these are NOT installed bindings."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    game = args.executable
    if args.output.exists():
        raise ValueError('Use a fresh output path')
    if hashlib.sha256(game.read_bytes()).hexdigest() != 'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37':
        raise ValueError('Unknown executable SHA-256')
    args.output.parent.mkdir(parents=True, exist_ok=True)

    data = game.read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    assert sha == 'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37'
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    count = struct.unpack_from('<H', data, pe + 6)[0]
    optional_size = struct.unpack_from('<H', data, pe + 20)[0]
    base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
    assert base == 0x400000
    sections = [struct.unpack_from('<IIII', data, pe + 24 + optional_size + i * 40 + 8)
                for i in range(count)]
    def at(va, length):
        for _, rva, raw_size, raw in sections:
            if rva <= va - base and va - base + length <= rva + raw_size:
                return data[raw + va - base - rva:raw + va - base - rva + length]
        raise ValueError(hex(va))
    def word(va):
        return struct.unpack('<I', at(va, 4))[0]

    handlers = []
    # D47910 initializes these five handler objects and appends them in this order.
    for table in [0x147ab38, 0x147ab18, 0x147ad48, 0x147aa90, 0x147aaf8]:
        handlers.append({'vtable_va': hex(table), 'execute_action_slot': '0x18',
                         'execute_action_va': hex(word(table + 0x18)),
                         'table_bytes': at(table, 32).hex()})
    functions = {
        'player_factory': 0xb1e960, 'noun_factory': 0xb20bf0, 'ensure_player': 0xb20f40,
        'player_constructor': 0xc7b930, 'player_cast': 0xc755d0, 'base_cast': 0xb183a0,
        'player_set_owner': 0xc7c870, 'player_remove_owner': 0xc7c3c0,
        'player_register_listeners': 0xc75380, 'player_destructor': 0xc7ae80,
        'native_award_amount': 0xc042a0, 'award_brain_factor': 0xc035a0,
        'add_evolution_points': 0xd2e8a0, 'player_trait_progress': 0xc75840,
        'display_add': 0xd2e2e0, 'display_enqueue': 0xd2d560,
        'action_dispatch': 0xd39360, 'strategy_initialize': 0xd47910,
        'strategy_on_enter': 0xd3a7f0, 'strategy_update': 0xd45740,
        'brain_progression': 0xd3fca0,
    }
    report = {'evidence_class': 'STATIC_RESEARCH_NOT_CALLABLE_ABI_OR_NATIVE_ACCEPTANCE',
              'executable_sha256': sha, 'preferred_image_base': hex(base),
              'sdk_commit': 'cbf9206b9a823f0911cd9be0217104a49d72380b',
              'new_installed_bindings': 0, 'handlers_initialized_statically': handlers,
              'functions': {key: {'va': hex(va), 'first_16_bytes': at(va, 16).hex()}
                            for key, va in functions.items()},
              'native_player_noun_id': '0x02c21781',
              'native_player_cast_id': '0x02c216ed', 'sdk_player_type': '0x03c609f8',
              'native_player_size': '0x12d8', 'native_player_vtable': '0x1472cc8',
              'notes': ['Five handler definitions found by static construction, not a recovered live vector.',
                        'Decompiler prototypes must be checked against machine instructions before new bindings.',
                        'Shared brain level affects the award amount; goal, trait and delayed progression are separate dependencies.']}
    with args.output.open('x', encoding='utf-8') as f:
        json.dump(report, f, indent=2)
    print(json.dumps({'handlers': handlers, 'research_functions': len(functions)}))


if __name__ == "__main__":
    main()
