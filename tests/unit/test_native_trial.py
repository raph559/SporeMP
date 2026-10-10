"""HOST checks for the native trial runner: synthetic traces and fake processes; no game launch."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

path = Path(__file__).resolve().parents[2] / 'tools/native/native-trial.py'
spec = importlib.util.spec_from_file_location('native_trial', path)
trial = importlib.util.module_from_spec(spec); spec.loader.exec_module(trial)


def row(sequence, event, **values):
    return dict(sequence=sequence, event=event, pid=100, thread_id=7, qpc=sequence * 10, foreign_callbacks=0, **values)


def append(file, *rows, newline=True):
    with file.open('a', encoding='utf-8') as stream:
        for value in rows:
            stream.write(json.dumps(value) + ('\n' if newline else ''))


class TraceTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(); self.addCleanup(self.directory.cleanup)
        self.file = Path(self.directory.name) / 'actors-100.jsonl'; self.file.touch()

    def test_poll_returns_only_rows_added_since_the_previous_poll(self):
        trace = trial.Trace(self.file); self.addCleanup(trace.close)
        append(self.file, row(1, 'trace_start'))
        self.assertEqual([r['event'] for r in trace.poll()], ['trace_start'])
        append(self.file, row(2, 'bound', native_id=5, owner=1, epoch=3))
        self.assertEqual([r['event'] for r in trace.poll()], ['bound'])
        self.assertEqual(trace.poll(), [])

    def test_incomplete_last_line_is_held_until_it_is_finished(self):
        trace = trial.Trace(self.file); self.addCleanup(trace.close)
        append(self.file, row(1, 'trace_start'), newline=False)
        self.assertEqual(trace.poll(), [])
        with self.file.open('a', encoding='utf-8') as stream: stream.write('\n')
        self.assertEqual(len(trace.poll()), 1)

    def test_gap_in_sequence_is_refused(self):
        trace = trial.Trace(self.file); self.addCleanup(trace.close)
        append(self.file, row(1, 'trace_start'), row(3, 'bound', native_id=5, owner=1, epoch=3))
        with self.assertRaisesRegex(trial.TrialAborted, 'Non-contiguous'):
            trace.poll()

    def test_latest_network_and_entity_samples_are_tracked_without_retaining_entity_samples(self):
        trace = trial.Trace(self.file); self.addCleanup(trace.close)
        append(self.file, row(1, 'network_scene_sample', ready=True, scene=4, baseline=9),
               row(2, 'scene_native_replica_sample', remote_entity=11, health=5),
               row(3, 'scene_native_replica_sample', remote_entity=11, health=4),
               row(4, 'native_award_return', owner=1))
        trace.poll()
        self.assertEqual(trace.network['scene'], 4)
        self.assertEqual(trace.entities[11]['health'], 4)
        self.assertEqual([r['event'] for r in trace.retained], ['network_scene_sample', 'native_award_return'])

    def test_stop_and_network_error_are_reported(self):
        trace = trial.Trace(self.file); self.addCleanup(trace.close)
        append(self.file, row(1, 'network_status', state='error', detail='x'))
        trace.poll()
        self.assertEqual((trace.ended, trace.failed), (False, True))
        append(self.file, row(2, 'trace_stop', healthy=True, detach_status=0))
        trace.poll()
        self.assertTrue(trace.ended)


class ConfigurationTests(unittest.TestCase):
    FRESH = 'schema=2\nhost=127.0.0.1\nport=27001\n'

    def test_existing_key_is_replaced_and_new_key_appended(self):
        result = trial.patch_config(self.FRESH, {'schema': 7, 'experimental_foreign_species': 1})
        self.assertEqual(result, 'schema=7\nhost=127.0.0.1\nport=27001\nexperimental_foreign_species=1\n')

    def test_duplicate_existing_key_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'schema'):
            trial.patch_config('schema=2\nschema=2\n', {'schema': 7})

    def test_connection_identity_keys_cannot_be_overridden(self):
        for key in ('host', 'port', 'build_sha256', 'executable_sha256', 'content_sha256', 'fixture_sha256'):
            with self.assertRaisesRegex(ValueError, key):
                trial.patch_config(self.FRESH + 'build_sha256=aa\n', {key: 1})


class ParkingTests(unittest.TestCase):
    def test_windows_are_cascaded_fully_inside_the_target_display(self):
        positions = trial.park_positions((3000, 100, 4920, 1180), [(1296, 759)] * 3)
        self.assertEqual(len(set(positions)), 3)
        for (x, y) in positions:
            self.assertTrue(3000 <= x and x + 1296 <= 4920 and 100 <= y and y + 759 <= 1180, (x, y))

    def test_window_larger_than_the_display_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'does not fit'):
            trial.park_positions((0, 0, 1024, 600), [(1296, 759)])


class Clock:
    """Manual time: sleeping advances it and runs whatever was scheduled meanwhile."""

    def __init__(self):
        self.now = 0.0
        self.scheduled = []

    def __call__(self):
        return self.now

    def at(self, when, action):
        self.scheduled.append((when, action))

    def sleep(self, seconds):
        self.now += seconds
        due = [item for item in self.scheduled if item[0] <= self.now]
        self.scheduled = [item for item in self.scheduled if item[0] > self.now]
        for _, action in due:
            action()


class LiveTests(unittest.TestCase):
    ROLES = ('authority', 'player-1', 'player-2')

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(); self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name) / 'trial'; self.root.mkdir()
        self.clock = Clock(); self.commands = []; self.replies = []
        self.files = {}; roles = {}
        for index, name in enumerate(self.ROLES):
            run = Path(self.directory.name) / ('run-' + name); run.mkdir()
            self.files[name] = run / f'actors-{index}.jsonl'; self.files[name].touch()
            roles[name] = dict(generation='ab' * 16, supervisor_pid=500 + index, run=str(run), worker_id='0' + str(index + 1))
        self.live = trial.Live(self.root, roles, deadline=200.0, control=self.control,
                               clock=self.clock, sleep=self.clock.sleep)
        self.addCleanup(self.live.close)

    def control(self, command):
        self.commands.append(command)
        reply = self.replies.pop(0)
        if isinstance(reply, Exception): raise reply
        return reply

    def ready(self, scene=4, baseline=9, start=1):
        for name in self.ROLES:
            append(self.files[name], row(start, 'network_scene_sample', ready=True, scene=scene, baseline=baseline))

    def log(self):
        return [json.loads(line) for line in (self.root / 'sequence.jsonl').read_text(encoding='utf-8').splitlines()]

    def test_until_returns_a_matching_row_that_arrives_later(self):
        self.clock.at(1.0, lambda: append(self.files['player-2'], row(1, 'goal', owner=1), row(2, 'goal', owner=2)))
        found = self.live.until('player-2', 'goal', where=lambda r: r['owner'] == 2, timeout=5)
        self.assertEqual(found['sequence'], 2)

    def test_until_sees_a_row_that_arrived_before_the_call(self):
        append(self.files['authority'], row(1, 'native_award_return', owner=1))
        self.live.poll()
        self.assertEqual(self.live.until('authority', 'native_award_return', timeout=1)['owner'], 1)

    def test_until_can_require_a_row_newer_than_a_known_sequence(self):
        append(self.files['authority'], row(1, 'save_done'))
        self.clock.at(1.0, lambda: append(self.files['authority'], row(2, 'save_done')))
        self.assertEqual(self.live.until('authority', 'save_done', after=1, timeout=5)['sequence'], 2)

    def test_until_timeout_fails_the_scenario_with_its_label(self):
        with self.assertRaisesRegex(trial.ScenarioFailed, 'player-1 goal'):
            self.live.until('player-1', 'goal', timeout=2)
        self.assertEqual(self.log()[-1]['event'], 'timeout')

    def test_ended_trace_aborts_the_trial(self):
        append(self.files['player-1'], row(1, 'trace_stop', healthy=True, detach_status=0))
        with self.assertRaisesRegex(trial.TrialAborted, 'player-1'):
            self.live.until('authority', 'goal', timeout=5)

    def test_network_error_aborts_the_trial(self):
        append(self.files['player-2'], row(1, 'network_status', state='error'))
        with self.assertRaisesRegex(trial.TrialAborted, 'player-2'):
            self.live.until('authority', 'goal', timeout=5)

    def test_closure_claim_aborts_the_trial(self):
        (self.root / 'close.claim').touch()
        with self.assertRaisesRegex(trial.TrialAborted, 'closing'):
            self.live.until('authority', 'goal', timeout=5)

    def test_approaching_deadline_aborts_before_the_watchdog_closes(self):
        self.clock.now = 190.0
        with self.assertRaisesRegex(trial.TrialAborted, 'deadline'):
            self.live.until('authority', 'goal', timeout=5)

    def test_wait_ready_needs_the_same_scene_and_baseline_to_hold(self):
        self.ready(scene=4, baseline=9)
        self.clock.at(2.0, lambda: self.ready(scene=4, baseline=10, start=2))
        self.live.wait_ready(stable_seconds=3, timeout=30)
        self.assertGreaterEqual(self.clock.now, 5.0)
        self.assertEqual(self.live.scene, 4)

    def test_wait_ready_refuses_clients_on_another_scene(self):
        self.ready()
        append(self.files['player-2'], row(2, 'network_scene_sample', ready=True, scene=5, baseline=9))
        with self.assertRaisesRegex(trial.ScenarioFailed, 'ready'):
            self.live.wait_ready(stable_seconds=1, timeout=4)

    def test_control_sends_one_exact_command_and_returns_the_accepted_reply(self):
        self.replies.append((0, '{"result":"accepted","epoch":4}', ''))
        reply = self.live.control('player-1', 'network_action', 4, 1, 77)
        self.assertEqual(reply['epoch'], 4)
        self.assertEqual(self.commands, [[str(trial.CONTROL), 'ab' * 16, '501', 'network_action', '4', '1', '77']])
        self.assertEqual([r['event'] for r in self.log()], ['control_request', 'control_reply'])

    def test_refused_control_fails_the_scenario_without_a_retry(self):
        self.replies.append((5, '{"result":"refused"}', ''))
        with self.assertRaisesRegex(trial.ScenarioFailed, 'refused'):
            self.live.control('authority', 'save', 4)
        self.assertEqual(len(self.commands), 1)

    def test_refusal_can_be_the_expected_outcome(self):
        self.replies.append((5, '{"result":"refused"}', ''))
        self.assertEqual(self.live.control('authority', 'save', 4, expect='refused')['result'], 'refused')

    def test_control_timeout_aborts_and_is_never_retried(self):
        self.replies.append(TimeoutError('no reply'))
        with self.assertRaisesRegex(trial.TrialAborted, 'not retried'):
            self.live.control('authority', 'save', 4)
        self.assertEqual(len(self.commands), 1)
        self.assertEqual(self.log()[-1]['event'], 'control_outcome_unknown')

    def test_settle_keeps_watching_for_the_given_time(self):
        self.live.settle(4)
        self.assertGreaterEqual(self.clock.now, 4.0)
        self.clock.at(self.clock.now + 1.0, lambda: append(self.files['authority'], row(1, 'trace_stop', healthy=True, detach_status=0)))
        with self.assertRaisesRegex(trial.TrialAborted, 'authority'):
            self.live.settle(4)

    def test_hold_presses_the_key_in_that_players_game_waits_and_releases(self):
        self.ready(); self.live.poll()
        self.replies += [(0, '{"result":"accepted"}', '')] * 2
        self.live.hold('player-2', 'W', 3)
        self.assertEqual([c[3:] for c in self.commands], [['input', '4', '1', '87'], ['input', '4', '2', '87']])
        self.assertEqual([c[2] for c in self.commands], ['502', '502'])
        self.assertGreaterEqual(self.clock.now, 3.0)

    def test_click_points_at_an_entity_in_that_players_game(self):
        self.ready(); self.live.poll()
        self.replies += [(0, '{"result":"accepted"}', '')] * 7
        self.live.click('player-2', 12884911489)
        self.live.click('player-1', 77, button='right')
        self.live.click('player-1', 77, button='none')
        self.live.click('player-1', 77, height=20)
        self.assertEqual([c[3:] for c in self.commands], [['click', '4', '1', '12884911489', '50'], ['click', '4', '2', '12884911489', '50'],
                                                          ['click', '4', '1', '77', '50'], ['click', '4', '3', '77', '50'], ['click', '4', '1', '77', '50'],
                                                          ['click', '4', '1', '77', '20'], ['click', '4', '2', '77', '20']])
        self.assertEqual(self.live.input_classes, {'INPUT_IN_PROCESS'})

    def test_click_at_a_place_points_there_first_then_presses(self):
        self.ready(); self.live.poll()
        self.replies += [(0, '{"result":"accepted"}', '')] * 2
        self.live.click_at('player-1', 1250, 688)
        self.assertEqual([c[3:] for c in self.commands], [['point', '4', '1', '1250', '688'], ['point', '4', '2', '1250', '688']])
        self.assertEqual(self.live.input_classes, {'INPUT_IN_PROCESS'})

    def test_ui_reading_returns_the_windows_of_that_reading_only(self):
        self.ready(); self.live.poll()
        append(self.files['player-1'], row(2, 'native_ui_window', id=9, text='old', area=[0, 0, 1, 1]),
               row(3, 'native_ui_snapshot', mode=1, windows=1))
        self.live.poll()
        self.replies += [(0, '{"result":"accepted"}', '')]
        self.clock.at(0.2, lambda: append(self.files['player-1'], row(4, 'native_ui_window', id=7, text='Stance', area=[450, 95, 830, 333]),
                                          row(5, 'native_ui_snapshot', mode=1, windows=1)))
        windows = self.live.ui('player-1')
        self.assertEqual([c[3:] for c in self.commands], [['ui', '4', '1', '0', '0']])
        self.assertEqual([(w['id'], w['text']) for w in windows], [(7, 'Stance')])

    def test_command_whose_refusal_is_an_answer_returns_the_reply(self):
        self.ready(); self.live.poll()
        self.replies += [(5, '{"result":"unavailable"}', '')]
        self.assertEqual(self.live.click('player-2', 77, expect=None)['result'], 'unavailable')
        self.assertEqual(len(self.commands), 1, 'a creature that cannot be pointed at is not clicked')

    def test_key_is_released_even_when_the_trial_aborts_during_the_hold(self):
        self.ready(); self.live.poll()
        self.replies += [(0, '{"result":"accepted"}', '')] * 2
        self.clock.at(1.0, lambda: append(self.files['authority'], row(2, 'network_status', state='error')))
        with self.assertRaises(trial.TrialAborted):
            self.live.hold('player-1', ' ', 5)
        self.assertEqual([c[3:] for c in self.commands], [['input', '4', '1', '32'], ['input', '4', '2', '32']])

    def test_expectations_are_recorded_and_a_false_one_fails_the_scenario(self):
        self.live.expect(True, 'B keeps 7 DNA', dna=7)
        with self.assertRaisesRegex(trial.ScenarioFailed, 'A reaches 14 DNA'):
            self.live.expect(False, 'A reaches 14 DNA', dna=7)
        self.assertEqual([(c['label'], c['passed']) for c in self.live.checks],
                         [('B keeps 7 DNA', True), ('A reaches 14 DNA', False)])

    def test_not_run_items_are_kept_with_their_prerequisite(self):
        self.live.not_run('Physical held key', 'a person holding the key')
        self.assertEqual(self.live.limits, [dict(label='Physical held key', status='NOT_RUN', prerequisite='a person holding the key')])


def write_png(path, width, height, pixel):
    """A real RGB PNG whose pixels come from pixel(x, y) -> (r, g, b)."""
    import struct, zlib
    rows = b''.join(b'\0' + bytes(c for x in range(width) for c in pixel(x, y)) for y in range(height))
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def picture(x, y):
    return ((x * 37 + y * 101) % 251, (x * x + y * 7) % 241, (x * 13 + y * y) % 239)


def closed_trial(root, roles=('authority', 'player-1', 'player-2')):
    """A complete, healthy closed trial directory with synthetic evidence."""
    root.mkdir(parents=True, exist_ok=True)
    for index, name in enumerate(roles):
        closed = root / ('closed-' + name); closed.mkdir()
        append(closed / f'actors-{index}.jsonl',
               dict(row(1, 'trace_start'), bridge_version='0.0.183', sdk_commit='cbf9', executable_sha256='ee'),
               row(2, 'network_status', state='connected'),
               row(3, 'trace_stop', healthy=True, detach_status=0))
        append(closed / 'native-host.jsonl', dict(event='game_exited', exit_code=0), dict(event='worker_exited', exit_code=0))
        (root / f'personal-after-{name}.json').write_text(json.dumps(dict(personal_sources_unchanged=True, file_count=29)))
        capture = root / ('capture-' + name); capture.mkdir()
        (capture / 'completion.json').write_text(json.dumps(dict(exit_code=0, wall_timeout=False)))
        write_png(capture / 'filmstrip.png', 64, 36, picture)
    (root / 'closure-result.json').write_text(json.dumps(dict(clean=True, failures=[])))
    restore = root / 'fixture-restore'; restore.mkdir()
    (restore / 'restore.json').write_text(json.dumps(dict(state='originals_restored', entries=[])))
    return root


def rewrite(file, change):
    rows = [json.loads(line) for line in file.read_text(encoding='utf-8').splitlines()]
    change(rows)
    file.write_text(''.join(json.dumps(r) + '\n' for r in rows), encoding='utf-8')


class StandardReviewTests(unittest.TestCase):
    ROLES = ('authority', 'player-1', 'player-2')

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(); self.addCleanup(self.directory.cleanup)
        self.root = closed_trial(Path(self.directory.name) / 'trial')

    def failed(self, **options):
        return [c['label'] for c in trial.standard_review(self.root, self.ROLES, **options) if not c['passed']]

    def test_healthy_closed_trial_passes_every_standard_check(self):
        checks = trial.standard_review(self.root, self.ROLES)
        self.assertEqual([c['label'] for c in checks if not c['passed']], [])
        self.assertGreater(len(checks), 15)

    def test_nonzero_game_exit_fails_that_role(self):
        (self.root / 'closed-player-2/native-host.jsonl').write_text(
            json.dumps(dict(event='game_exited', exit_code=35)) + '\n' + json.dumps(dict(event='worker_exited', exit_code=0)) + '\n')
        self.assertEqual(self.failed(), ['player-2: game and worker exit 0'])

    def test_trace_without_a_healthy_stop_fails(self):
        rewrite(self.root / 'closed-authority/actors-0.jsonl', lambda rows: rows.pop())
        self.assertEqual(self.failed(), ['authority: trace starts and stops healthy'])

    def test_sequence_gap_or_foreign_thread_fails_trace_integrity(self):
        rewrite(self.root / 'closed-player-1/actors-1.jsonl', lambda rows: rows[1].update(thread_id=8))
        self.assertEqual(self.failed(), ['player-1: one contiguous single-thread trace'])

    def test_network_error_and_trace_limit_fail(self):
        rewrite(self.root / 'closed-player-1/actors-1.jsonl', lambda rows: rows[1].update(state='error'))
        self.assertEqual(self.failed(), ['player-1: no network error or trace limit'])

    def test_changed_personal_files_fail(self):
        (self.root / 'personal-after-authority.json').write_text(json.dumps(dict(personal_sources_unchanged=False, file_count=29)))
        self.assertEqual(self.failed(), ['authority: personal files unchanged'])

    def test_mixed_bridge_builds_fail(self):
        rewrite(self.root / 'closed-player-2/actors-2.jsonl', lambda rows: rows[0].update(bridge_version='0.0.182'))
        self.assertEqual(self.failed(), ['all peers ran one bridge, SDK and executable'])

    def test_unclean_closure_and_unrestored_fixture_fail(self):
        (self.root / 'closure-result.json').write_text(json.dumps(dict(clean=False, failures=['x'])))
        (self.root / 'fixture-restore/restore.json').write_text(json.dumps(dict(state='restored', entries=[])))
        self.assertEqual(self.failed(), ['closure clean', 'disposable profiles restored to their originals'])

    def parked(self, opened=True, state='attached', redirected=1, foreground=False):
        (self.root / 'window-placement.json').write_text(json.dumps(dict(display=[2560, 0, 4480, 1080], placement='bridge', windows={
            role: dict(first_visible_at=[2560, 0, 3846, 755], opened_on_display=opened, held_foreground=foreground)
            for role in self.ROLES})))
        for index, role in enumerate(self.ROLES):
            rewrite(self.root / f'closed-{role}/actors-{index}.jsonl', lambda rows: (rows.insert(2, dict(
                rows[1], event='native_window_placement', state=state, redirected=redirected)),
                [r.update(sequence=i, qpc=i * 10) for i, r in enumerate(rows, 1)]))

    def test_parked_trial_passes_when_every_window_opened_there_through_the_bridge(self):
        self.parked()
        self.assertEqual(self.failed(), [])
        self.assertIn('every game window opened on the parking display',
                      [c['label'] for c in trial.standard_review(self.root, self.ROLES)])

    def test_trial_whose_windows_were_moved_from_outside_is_not_held_to_the_bridge_checks(self):
        (self.root / 'window-placement.json').write_text(json.dumps(dict(display=[2560, 0, 4480, 1080], windows={
            role: dict(opened_at=[637, 298, 1923, 1053], parked_at=[2560, 0, 3846, 755]) for role in self.ROLES})))
        self.assertEqual(self.failed(), [])

    def test_window_first_shown_elsewhere_fails(self):
        self.parked(opened=False)
        self.assertEqual(self.failed(), ['every game window opened on the parking display'])

    def test_game_that_took_the_foreground_is_recorded_but_does_not_fail_the_trial(self):
        self.parked(foreground=True)
        self.assertEqual(self.failed(), [])
        self.assertEqual(trial.observations(self.root), [dict(
            label='A starting game was the foreground window when it appeared', roles=list(self.ROLES),
            status='ACCEPTED', note='Known and left as is: happens when the user has been idle.')])

    def test_no_observation_when_no_game_took_the_foreground(self):
        self.parked()
        self.assertEqual(trial.observations(self.root), [])

    def test_bridge_that_did_not_redirect_fails(self):
        self.parked(state='binding_mismatch', redirected=0)
        self.assertEqual(self.failed(), ['the bridge redirected the centering request in every game'])

    def test_capture_whose_preview_is_one_flat_colour_fails(self):
        # Trial179: the monitors were off and every captured frame was white, while every trace check passed.
        write_png(self.root / 'capture-player-2/filmstrip.png', 64, 36, lambda x, y: (253, 253, 253))
        self.assertEqual(self.failed(fixture=False), ['player-2: capture shows a picture'])

    def test_capture_without_a_readable_preview_fails(self):
        (self.root / 'capture-authority/filmstrip.png').write_bytes(b'not a picture')
        (self.root / 'capture-player-1/filmstrip.png').unlink()
        self.assertEqual(sorted(self.failed(fixture=False)), ['authority: capture shows a picture', 'player-1: capture shows a picture'])

    def test_fixture_and_capture_checks_are_skipped_when_not_used(self):
        import shutil
        shutil.rmtree(self.root / 'fixture-restore'); shutil.rmtree(self.root / 'capture-authority')
        self.assertEqual(self.failed(fixture=False, captures=False), [])
        self.assertIn('authority: capture finished normally', self.failed(fixture=False))


class VariantTests(unittest.TestCase):
    """One scenario file run for either player: a scenario is a single frozen file."""

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(); self.addCleanup(self.directory.cleanup)
        self.base = Path(self.directory.name); (self.base / 'local').mkdir()
        self.file = self.base / 'local/s.py'
        self.file.write_text("PLAYER = 1\nDEADLINE_SECONDS = 60\nVARIANTS = {'player-2': {'PLAYER': 2, 'DEADLINE_SECONDS': 90}}\n"
                             "def drive(trial):\n    trial.expect(PLAYER == 2, 'drives player 2')\n", encoding='utf-8')

    def test_without_a_variant_the_file_is_used_as_written(self):
        scenario = trial.load_scenario(self.file, repo=self.base)
        self.assertEqual((scenario.variant, scenario.deadline_seconds), (None, 60))

    def test_variant_overrides_the_constants_it_names_before_they_are_read(self):
        scenario = trial.load_scenario(self.file, repo=self.base, variant='player-2')
        self.assertEqual((scenario.variant, scenario.deadline_seconds, scenario.label), ('player-2', 90, 's.py[player-2]'))
        checks = []
        scenario.drive(type('T', (), {'expect': lambda self, ok, label: checks.append(ok)})())
        self.assertEqual(checks, [True])

    def test_unknown_variant_or_unknown_constant_is_refused(self):
        with self.assertRaises(ValueError):
            trial.load_scenario(self.file, repo=self.base, variant='player-3')
        self.file.write_text("VARIANTS = {'x': {'MISSING': 1}}\ndef drive(trial):\n    pass\n", encoding='utf-8')
        with self.assertRaises(ValueError):
            trial.load_scenario(self.file, repo=self.base, variant='x')


class DisplayAwakeTests(unittest.TestCase):
    """Trial197: after the display had idled off, the recorder received one frame in a whole trial."""

    def test_display_is_required_for_the_trial_and_released_afterwards(self):
        states = []
        with trial.display_awake(states.append, state=lambda: 'on', nudge=self.fail) as power:
            self.assertEqual(states, [trial.DISPLAY_REQUIRED])
        self.assertEqual(states, [trial.DISPLAY_REQUIRED, trial.DISPLAY_RELEASED])
        self.assertEqual(power, dict(display_before='on', woken=False, display_after='on'))

    def test_display_that_idled_off_is_turned_on_by_one_pointer_input_that_moves_nothing(self):
        # Trial200: the request alone does not turn an already-off display back on.
        order, readings = [], iter(['off', 'on'])
        with trial.display_awake(lambda value: order.append('request'), state=lambda: next(readings),
                                 nudge=lambda: order.append('nudge'), settle=lambda seconds: order.append('settle')) as power:
            self.assertEqual(order, ['request', 'nudge', 'settle'])
        self.assertEqual(power, dict(display_before='off', woken=True, display_after='on'))

    def test_unreadable_display_state_sends_no_input(self):
        with trial.display_awake(lambda value: None, state=lambda: None, nudge=self.fail) as power:
            pass
        self.assertEqual(power['woken'], False)

    def test_display_is_released_when_the_trial_raises(self):
        states = []
        with self.assertRaises(RuntimeError):
            with trial.display_awake(states.append, state=lambda: 'on', nudge=self.fail):
                raise RuntimeError('trial broke')
        self.assertEqual(states[-1], trial.DISPLAY_RELEASED)

    def test_request_is_the_one_a_video_player_holds(self):
        self.assertEqual((trial.DISPLAY_REQUIRED, trial.DISPLAY_RELEASED), (0x80000003, 0x80000000))


class ClosedTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(); self.addCleanup(self.directory.cleanup)
        self.closed = trial.Closed(closed_trial(Path(self.directory.name) / 'trial'))

    def test_events_and_one_read_the_closed_trace(self):
        self.assertEqual(len(self.closed.events('authority', 'network_status')), 1)
        self.assertEqual(self.closed.one('player-1', 'trace_start')['bridge_version'], '0.0.183')

    def test_failed_expectations_are_collected_instead_of_stopping_the_review(self):
        self.closed.expect(False, 'first')
        self.closed.expect_none('authority', 'network_status')
        self.closed.expect_none('authority', 'native_award_return')
        self.assertEqual([(c['label'], c['passed']) for c in self.closed.checks],
                         [('first', False), ('authority: no network_status', False), ('authority: no native_award_return', True)])

    def test_one_records_a_failure_when_the_event_is_missing_or_repeated(self):
        self.assertIsNone(self.closed.one('authority', 'native_award_return'))
        self.assertEqual(self.closed.checks[-1], dict(label='authority: exactly one native_award_return', passed=False, evidence=dict(count=0)))


class FreezeTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(); self.addCleanup(self.directory.cleanup)
        self.repo = Path(self.directory.name) / 'repo'; (self.repo / 'src').mkdir(parents=True)
        (self.repo / 'src/a.cpp').write_text('int a;'); (self.repo / 'tool.py').write_text('print(1)')
        self.root = Path(self.directory.name) / 'trial'; self.root.mkdir()

    def test_freeze_records_hashes_and_keeps_byte_identical_copies(self):
        entries = trial.freeze_sources(self.repo, self.root, ['tool.py', 'src/a.cpp', 'tool.py'])
        self.assertEqual([e['path'] for e in entries], ['src/a.cpp', 'tool.py'])
        self.assertEqual((self.root / 'source-revisions/src/a.cpp').read_text(), 'int a;')
        self.assertEqual(trial.verify_frozen(self.root), [])

    def test_changed_frozen_copy_is_reported(self):
        trial.freeze_sources(self.repo, self.root, ['src/a.cpp'])
        (self.root / 'source-revisions/src/a.cpp').write_text('int b;')
        self.assertEqual(trial.verify_frozen(self.root), ['src/a.cpp'])

    def test_missing_source_is_refused(self):
        with self.assertRaises(FileNotFoundError):
            trial.freeze_sources(self.repo, self.root, ['src/missing.cpp'])


class ScenarioTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(); self.addCleanup(self.directory.cleanup)
        # Resolved: a scenario's artifact is, and a runner's temporary directory can be given in its short 8.3 form.
        self.repo = Path(self.directory.name).resolve(); (self.repo / 'local/fixture').mkdir(parents=True)
        (self.repo / 'local/fixture/checkpoint.smp').write_bytes(b'x')

    def load(self, body):
        file = self.repo / 'local/scenario.py'; file.write_text(body, encoding='utf-8')
        return trial.load_scenario(file, repo=self.repo)

    def test_minimal_scenario_gets_defaults(self):
        scenario = self.load('def drive(trial):\n    pass\n')
        self.assertEqual((scenario.name, scenario.deadline_seconds, scenario.artifact, scenario.authority, scenario.review),
                         ('scenario', 150, None, {}, None))

    def test_declared_settings_are_read(self):
        scenario = self.load("ARTIFACT='local/fixture/checkpoint.smp'\nAUTHORITY={'schema':7}\nDEADLINE_SECONDS=90\n"
                             'def drive(trial):\n    pass\ndef review(closed):\n    pass\n')
        self.assertEqual(scenario.artifact, self.repo / 'local/fixture/checkpoint.smp')
        self.assertEqual((scenario.authority, scenario.deadline_seconds, callable(scenario.review)), ({'schema': 7}, 90, True))

    def test_scenario_without_drive_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'drive'):
            self.load('X = 1\n')

    def test_deadline_outside_the_checkpoint_budget_is_refused(self):
        for seconds in (10, 301):
            with self.assertRaisesRegex(ValueError, 'DEADLINE_SECONDS'):
                self.load(f'DEADLINE_SECONDS={seconds}\ndef drive(trial):\n    pass\n')

    def test_artifact_outside_local_or_missing_is_refused(self):
        (self.repo / 'outside.smp').write_bytes(b'x')
        for artifact in ('outside.smp', 'local/fixture/absent.smp', 'local/../outside.smp'):
            with self.assertRaisesRegex(ValueError, 'ARTIFACT'):
                self.load(f'ARTIFACT={artifact!r}\ndef drive(trial):\n    pass\n')

    def test_protected_authority_setting_is_refused_at_load(self):
        with self.assertRaisesRegex(ValueError, 'port'):
            self.load("AUTHORITY={'port':1}\ndef drive(trial):\n    pass\n")


class VerdictTests(unittest.TestCase):
    PASSED = [dict(label='a', passed=True)]

    def test_pass_needs_no_error_and_every_check_passing(self):
        verdict = trial.compose_verdict('t1', 's.py', phases=[], checks=self.PASSED, limits=[], error=None)
        self.assertEqual((verdict['outcome'], verdict['visual_review'], verdict['milestone_acceptance']),
                         ('PASS', 'NOT_INSPECTED', 'NONE'))

    def test_failed_check_or_error_gives_fail_with_the_reason(self):
        failed = trial.compose_verdict('t1', 's.py', phases=[], checks=self.PASSED + [dict(label='b', passed=False)], limits=[], error=None)
        self.assertEqual((failed['outcome'], failed['failed_checks']), ('FAIL', ['b']))
        errored = trial.compose_verdict('t1', 's.py', phases=[], checks=self.PASSED, limits=[], error='Timed out')
        self.assertEqual((errored['outcome'], errored['error']), ('FAIL', 'Timed out'))

    def test_no_checks_at_all_is_not_a_pass(self):
        self.assertEqual(trial.compose_verdict('t1', 's.py', phases=[], checks=[], limits=[], error=None)['outcome'], 'FAIL')


def recorded_trial(root, player_two_rows, frequency=10, origins=None):
    """A closed trial whose three traces carry recorded times (qpc at `frequency` ticks per second)."""
    closed_trial(root)

    def trace(extra, origin):
        rows = [dict(row(1, 'trace_start'), bridge_version='0.0.183', sdk_commit='cbf9', executable_sha256='ee', **origin)]
        rows += [dict(event=e, qpc=q, **v) for e, q, v in extra]
        rows.append(dict(event='trace_stop', qpc=200 * frequency, healthy=True, detach_status=0))
        return [dict(pid=100, thread_id=7, foreign_callbacks=0, qpc_frequency=frequency) | r | dict(sequence=i)
                for i, r in enumerate(rows, 1)]
    ready = ('network_scene_sample', 10 * frequency, dict(ready=True, scene=4, baseline=9))
    for index, name in enumerate(('authority', 'player-1', 'player-2')):
        file = root / ('closed-' + name) / f'actors-{index}.jsonl'
        file.write_text('', encoding='utf-8')
        append(file, *trace([ready] + (player_two_rows if name == 'player-2' else []),
                            dict(qpc_origin=origins[name]) if origins else {}))
    return root


class ReplayTests(unittest.TestCase):
    """A retained trial replayed through a scenario in recorded time, with no game."""

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(); self.addCleanup(self.directory.cleanup)
        self.base = Path(self.directory.name); (self.base / 'local').mkdir()

    def scenario(self, drive, review=''):
        file = self.base / 'local/s.py'
        file.write_text(f'def drive(trial):\n    {drive}\n{review}', encoding='utf-8')
        return trial.load_scenario(file, repo=self.base)

    def replay(self, drive, rows, review='', origins=None, frequency=10):
        root = recorded_trial(self.base / 'trial', rows, frequency, origins)
        before = sorted(p.name for p in root.iterdir())
        verdict = trial.replay_trial(self.scenario(drive, review), root)
        self.assertEqual(sorted(p.name for p in root.iterdir()), before, 'retained evidence must not be touched')
        return verdict

    def test_recorded_run_that_meets_the_scenario_passes_as_host_replay(self):
        verdict = self.replay("trial.wait_ready(stable_seconds=3); g = trial.until('player-2', 'goal', timeout=60); "
                              "trial.expect(g['after'] == 1, 'B shows 1/3')", [('goal', 600, dict(after=1))],
                              review="def review(closed):\n    closed.expect_none('authority', 'goal')\n")
        self.assertEqual((verdict['outcome'], verdict['error']), ('PASS', None))
        self.assertEqual(verdict['evidence_class'], 'HOST_REPLAY_OF_RETAINED_NATIVE_TRACES')
        labels = [c['label'] for c in verdict['checks']]
        self.assertIn('B shows 1/3', labels); self.assertIn('authority: no goal', labels); self.assertIn('closure clean', labels)

    def test_waits_run_in_recorded_time_so_a_late_event_times_out(self):
        verdict = self.replay("trial.until('player-2', 'goal', timeout=30)", [('goal', 600, dict(after=1))])
        self.assertEqual(verdict['outcome'], 'FAIL'); self.assertIn('Timed out after 30s', verdict['error'])

    def test_wait_long_enough_in_recorded_time_sees_the_event(self):
        verdict = self.replay("trial.until('player-2', 'goal', timeout=70); trial.expect(True, 'seen')",
                              [('goal', 600, dict(after=1))])
        self.assertEqual(verdict['outcome'], 'PASS', verdict['error'])

    def test_state_is_what_had_been_sampled_at_that_recorded_moment(self):
        rows = [('scene_native_replica_sample', 300, dict(remote_entity=1, native_dna=7)),
                ('goal', 400, dict(after=1)),
                ('scene_native_replica_sample', 900, dict(remote_entity=1, native_dna=14))]
        verdict = self.replay("trial.until('player-2', 'goal', timeout=60); "
                              "trial.expect(trial.traces['player-2'].entities[1]['native_dna'] == 7, 'DNA at the goal moment')", rows)
        self.assertEqual(verdict['outcome'], 'PASS', verdict['error'])

    def test_event_that_was_never_recorded_fails_when_the_recording_ends(self):
        verdict = self.replay("trial.until('player-2', 'goal', timeout=500)", [])
        self.assertEqual(verdict['outcome'], 'FAIL'); self.assertIn('closed', verdict['error'])

    def test_final_settle_that_runs_into_the_end_of_the_recording_is_not_a_failure(self):
        verdict = self.replay("trial.expect(True, 'done'); trial.settle(500)", [])
        self.assertEqual(verdict['outcome'], 'PASS', verdict['error'])

    def test_network_error_during_a_settle_still_fails(self):
        verdict = self.replay("trial.expect(True, 'done'); trial.settle(500)", [('network_status', 300, dict(state='error'))])
        self.assertEqual(verdict['outcome'], 'FAIL')

    def test_commands_are_not_sent_and_are_counted(self):
        verdict = self.replay("trial.control('authority', 'save', 4); trial.expect(True, 'ok')", [])
        self.assertEqual((verdict['outcome'], verdict['commands_not_executed']), ('PASS', 1))

    # Player 2's game started its trace 30 seconds after the other two, on the clock all three share.
    LATE_B = dict(authority=5000, **{'player-1': 5000, 'player-2': 5300})

    def test_traces_are_aligned_by_the_shared_clock_origin_each_one_states(self):
        early = self.replay("trial.until('player-2', 'goal', timeout=70)", [('goal', 600, dict(after=1))], origins=self.LATE_B)
        self.assertEqual(early['outcome'], 'FAIL'); self.assertIn('Timed out after 70s', early['error'])

    def test_aligned_event_is_seen_once_recorded_time_reaches_it(self):
        verdict = self.replay("trial.until('player-2', 'goal', timeout=95); trial.expect(True, 'seen')",
                              [('goal', 600, dict(after=1))], origins=self.LATE_B)
        self.assertEqual(verdict['outcome'], 'PASS', verdict['error'])

    def test_command_issued_right_after_a_condition_is_not_overtaken_by_its_own_receipt(self):
        # The live run saw the creature at rest, and its key arrived 0.03 seconds later. A replay must reach
        # the same line before that receipt, so a mark taken there still precedes the key.
        rows = [('rest', 4031, {}), ('network_input_posted', 4034, dict(key=87, down=True, posted=True)),
                ('network_key', 4035, dict(key=87, down=True))]
        verdict = self.replay("trial.until('player-2', 'rest', timeout=60); mark = trial.traces['player-2'].sequence; "
                              "trial.key('player-2', 'W', True); "
                              "trial.until('player-2', 'network_key', after=mark, timeout=5); trial.expect(True, 'seen')", rows,
                              frequency=100)
        self.assertEqual(verdict['outcome'], 'PASS', verdict['error'])

    HELD_W = [('network_input_posted', 400, dict(key=87, down=True, posted=True)), ('network_key', 402, dict(key=87, down=True)),
              ('network_input_posted', 441, dict(key=87, down=False, posted=True)), ('network_key', 443, dict(key=87, down=False))]

    def test_key_input_is_replayed_at_the_moment_the_game_received_it(self):
        # The live run sent this key 27 seconds after the peers were ready; a short wait after it must still see it.
        verdict = self.replay("trial.wait_ready(stable_seconds=3); mark = trial.traces['player-2'].sequence; trial.hold('player-2', 'W', 4); "
                              "down = trial.until('player-2', 'network_key', where=lambda r: r['down'], after=mark, timeout=5); "
                              "up = trial.until('player-2', 'network_key', where=lambda r: not r['down'], after=mark, timeout=5); "
                              "trial.expect(up['qpc'] - down['qpc'] == 41, 'held for the recorded time')", self.HELD_W)
        self.assertEqual((verdict['outcome'], verdict['error'], verdict['commands_not_executed']), ('PASS', None, 2))

    def test_key_input_the_recording_never_received_fails_when_the_recording_ends(self):
        verdict = self.replay("trial.wait_ready(stable_seconds=3); trial.key('player-2', 'A', True); trial.expect(True, 'sent')", self.HELD_W)
        self.assertEqual(verdict['outcome'], 'FAIL'); self.assertIn('closed', verdict['error'])

    def test_click_is_replayed_at_the_moment_the_game_received_it(self):
        rows = [('native_screen_point', 396, dict(entity=77, inside=True)),
                ('native_screen_point', 400, dict(entity=77, inside=True)), ('selected', 401, {})]
        verdict = self.replay("trial.wait_ready(stable_seconds=3); mark = trial.traces['player-2'].sequence; trial.click('player-2', 77); "
                              "trial.until('player-2', 'selected', after=mark, timeout=2); trial.expect(True, 'seen')", rows)
        self.assertEqual((verdict['outcome'], verdict['error']), ('PASS', None))

    def test_click_on_an_entity_the_recording_never_clicked_fails_when_the_recording_ends(self):
        rows = [('native_screen_point', 400, dict(entity=77, inside=True))]
        other = self.replay("trial.wait_ready(stable_seconds=3); trial.click('player-2', 78); trial.expect(True, 'sent')", rows)
        self.assertEqual(other['outcome'], 'FAIL'); self.assertIn('closed', other['error'])

    def test_replayed_click_answers_what_the_game_answered(self):
        rows = [('native_screen_point', 300, dict(entity=77, inside=False)), ('native_screen_point', 400, dict(entity=77, inside=True))]
        verdict = self.replay("trial.wait_ready(stable_seconds=3); "
                              "first = trial.click('player-2', 77, button='none', expect=None)['result']; "
                              "second = trial.click('player-2', 77, button='none', expect=None)['result']; "
                              "trial.expect((first, second) == ('unavailable', 'accepted'), 'out of view, then in view')", rows)
        self.assertEqual((verdict['outcome'], verdict['error']), ('PASS', None))

    def test_replayed_click_the_game_refused_fails_a_scenario_that_required_it(self):
        rows = [('native_screen_point', 300, dict(entity=77, inside=False))]
        verdict = self.replay("trial.wait_ready(stable_seconds=3); trial.click('player-2', 77, button='none'); trial.expect(True, 'sent')", rows)
        self.assertEqual(verdict['outcome'], 'FAIL'); self.assertIn('unavailable', verdict['error'])

    def test_click_at_a_place_is_replayed_at_the_moment_the_game_received_it(self):
        rows = [('network_point_posted', 396, dict(point=[1250, 688], action=1, posted=True)),
                ('network_point_posted', 400, dict(point=[1250, 688], action=2, posted=True)), ('dismissed', 401, {})]
        verdict = self.replay("trial.wait_ready(stable_seconds=3); mark = trial.traces['player-2'].sequence; trial.click_at('player-2', 1250, 688); "
                              "trial.until('player-2', 'dismissed', after=mark, timeout=2); trial.expect(True, 'seen')", rows)
        self.assertEqual((verdict['outcome'], verdict['error']), ('PASS', None))

    def test_each_recorded_key_input_is_used_once(self):
        verdict = self.replay("trial.wait_ready(stable_seconds=3); trial.key('player-2', 'W', True); trial.key('player-2', 'W', True); "
                              "trial.expect(True, 'sent')", self.HELD_W)
        self.assertEqual(verdict['outcome'], 'FAIL'); self.assertIn('closed', verdict['error'])


class ReplaySuiteTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(); self.addCleanup(self.directory.cleanup)
        self.base = Path(self.directory.name); (self.base / 'local').mkdir()
        recorded_trial(self.base / 'local/good', [('goal', 600, dict(after=1))])
        recorded_trial(self.base / 'local/bad', [])
        (self.base / 'local/s.py').write_text("def drive(trial):\n    trial.until('player-2', 'goal', timeout=70)\n"
                                              "    trial.expect(True, 'goal seen')\n", encoding='utf-8')

    def suite(self, entries):
        manifest = self.base / 'local/suite.json'; manifest.write_text(json.dumps(entries), encoding='utf-8')
        return trial.replay_suite(manifest, repo=self.base)

    def test_suite_passes_when_every_recording_gives_its_expected_outcome(self):
        results = self.suite([dict(scenario='local/s.py', trial='local/good', expect='PASS'),
                              dict(scenario='local/s.py', trial='local/bad', expect='FAIL', because='Timed out')])
        self.assertEqual([(r['outcome'], r['as_expected']) for r in results], [('PASS', True), ('FAIL', True)])

    def test_recordings_can_live_under_another_root_and_need_no_fixture_file(self):
        import shutil
        elsewhere = Path(self.directory.name) / 'main-checkout'; (elsewhere / 'local').mkdir(parents=True)
        shutil.move(str(self.base / 'local/good'), str(elsewhere / 'local/good'))
        (self.base / 'local/s.py').write_text("ARTIFACT = 'local/absent/checkpoint.smp'\n"
                                              + (self.base / 'local/s.py').read_text(encoding='utf-8'), encoding='utf-8')
        manifest = self.base / 'local/suite.json'
        manifest.write_text(json.dumps([dict(scenario='local/s.py', trial='local/good', expect='PASS')]), encoding='utf-8')
        results = trial.replay_suite(manifest, repo=self.base, evidence=elsewhere)
        self.assertEqual([(r['outcome'], r['as_expected']) for r in results], [('PASS', True)])

    def test_recording_that_is_not_available_is_reported_not_guessed(self):
        results = self.suite([dict(scenario='local/s.py', trial='local/nowhere', expect='PASS')])
        self.assertEqual((results[0]['outcome'], results[0]['as_expected']), ('MISSING', False))

    def test_unexpected_outcome_or_wrong_failure_reason_is_flagged(self):
        results = self.suite([dict(scenario='local/s.py', trial='local/bad', expect='PASS'),
                              dict(scenario='local/s.py', trial='local/bad', expect='FAIL', because='reward replayed')])
        self.assertEqual([r['as_expected'] for r in results], [False, False])


class RecordTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(); self.addCleanup(self.directory.cleanup)
        self.root = closed_trial(Path(self.directory.name) / 'trial150')
        verdict = trial.compose_verdict('trial150', 'goalcard.py', phases=[
            dict(phase='drive', utc='x', seconds=40.656), dict(phase='close', utc='x', seconds=5.188)],
            checks=[dict(label='B shows Hunt 1/3', passed=True, evidence={}),
                    dict(label='closure clean', passed=False, evidence={})],
            limits=[dict(label='Physical input', status='NOT_RUN', prerequisite='a person')], error=None)
        (self.root / 'verdict.json').write_text(json.dumps(verdict), encoding='utf-8')
        (self.root / 'source-head.txt').write_text('abc123\n')

    def test_record_states_outcome_identity_timings_failures_and_limits(self):
        text = trial.render_record(self.root)
        for expected in ('# trial150', 'FAIL', 'goalcard.py', '0.0.183', 'cbf9', 'abc123', '| drive | 40.7 |',
                         'closure clean', 'Physical input', 'NOT_RUN', 'a person', 'NOT_INSPECTED', '1 of 2 checks'):
            self.assertIn(expected, text)

    def test_record_carries_no_machine_paths(self):
        text = trial.render_record(self.root)
        self.assertNotIn(str(self.root.parent), text)
        self.assertNotIn(':\\', text)


class SessionDisplayTests(unittest.TestCase):
    """One virtual display held for a whole work session is reused by every trial."""
    MONITOR = dict(device='\\\\.\\DISPLAY10', rect=[2560, 0, 4480, 1080], primary=False)
    INFO = dict(state='held', pid=4242, monitor=MONITOR)

    def usable(self, info=None, alive=True, monitors=None):
        return trial.usable_session(self.INFO if info is None else info, lambda pid: alive and pid == 4242,
                                    [self.MONITOR] if monitors is None else monitors)

    def test_live_holder_with_its_display_attached_is_reused(self):
        self.assertEqual(self.usable(), (2560, 0, 4480, 1080))

    def test_released_or_missing_session_is_not_used(self):
        self.assertIsNone(self.usable(info=dict(self.INFO, state='released')))
        self.assertIsNone(self.usable(info={}))

    def test_session_whose_holder_died_is_not_used(self):
        self.assertIsNone(self.usable(alive=False))

    def test_session_whose_display_is_gone_or_moved_is_not_used(self):
        self.assertIsNone(self.usable(monitors=[]))
        self.assertIsNone(self.usable(monitors=[dict(self.MONITOR, rect=[0, 0, 1920, 1080])]))


class LaunchPlanTests(unittest.TestCase):
    def test_frozen_set_covers_changes_the_scenario_the_tools_and_the_launched_binaries(self):
        paths = trial.frozen_paths(['src/bridge/native_scene.cpp'], ['tests/unit/new_tests.cpp'], 'local/scenarios/goalcard.py')
        for expected in ('src/bridge/native_scene.cpp', 'tests/unit/new_tests.cpp', 'local/scenarios/goalcard.py',
                         'tools/native/native-trial.py', 'tools/native/start-worker.ps1',
                         'build/win32/Release/SporeMP.Bridge.dll', 'build/win32/Release/SporeMP.Coordinator.exe'):
            self.assertIn(expected, paths)
        self.assertEqual(len(paths), len(set(paths)))

    def test_each_role_maps_to_its_prepared_worker_and_bridge_role(self):
        self.assertEqual(trial.ROLE_WORKERS, {'player-1': ('01', 'Replica'), 'player-2': ('02', 'Replica'),
                                              'authority': ('03', 'Authority')})

    def test_each_role_gets_its_own_client_origin_inside_the_parking_display(self):
        display = (2560, 0, 4480, 1080)
        origins = [trial.worker_window_origin(display, role) for role in trial.LAUNCH_ORDER]
        self.assertEqual(len(set(origins)), 3)
        for x, y in origins:
            self.assertTrue(2560 < x and x + 1280 < 4480 and 0 < y and y + 720 < 1080, (x, y))

    def test_filmstrip_samples_sixteen_cells_across_the_whole_capture(self):
        self.assertEqual(trial.filmstrip_filter(80.0), 'fps=16/80.000,scale=320:180,tile=4x4')
        with self.assertRaises(ValueError):
            trial.filmstrip_filter(0)

    def test_trial_names_are_limited_to_what_the_server_script_accepts(self):
        self.assertEqual(trial.trial_name('trial150'), 'trial150')
        for bad in ('Trial150', 'trial 150', '../x', ''):
            with self.assertRaises(ValueError):
                trial.trial_name(bad)


class ProgressTests(unittest.TestCase):
    """trial158: the authority starts last and had not begun updating two seconds after its window appeared."""

    def setUp(self):
        self.clock = Clock()
        self.starts = {'player-1': 0.0, 'player-2': 1.0, 'authority': 6.0}

    def sample(self, role):
        return None if self.clock.now < self.starts[role] - 1 else int(max(0.0, self.clock.now - self.starts[role]) * 10)

    def wait(self, timeout=30):
        return trial.wait_for_progress(list(self.starts), self.sample, clock=self.clock, sleep=self.clock.sleep, timeout=timeout)

    def test_a_game_that_starts_updating_late_still_passes(self):
        samples = self.wait()
        self.assertTrue(all(last > first for first, last in samples.values()), samples)
        self.assertGreaterEqual(self.clock.now, 6.0)

    def test_a_game_that_never_updates_is_named_at_the_timeout(self):
        self.starts['authority'] = 10 ** 6
        with self.assertRaisesRegex(trial.TrialAborted, 'authority'):
            self.wait(timeout=8)

    def test_no_time_is_spent_once_every_game_has_advanced(self):
        self.starts = {'player-1': 0.0, 'player-2': 0.0, 'authority': 0.0}
        self.wait()
        self.assertLess(self.clock.now, 2.0)


class PlaceAsFoundTests(unittest.TestCase):
    """Each game window must leave the primary display the moment it exists, not when all three do."""

    def setUp(self):
        self.clock = Clock(); self.appears = {'player-1': 1.0, 'player-2': 3.0, 'authority': 5.0}; self.placed = []

    def find(self, role):
        return ('window-' + role) if self.clock.now >= self.appears[role] else None

    def place(self, role, window):
        self.placed.append((role, window, round(self.clock.now, 2)))

    def run_placement(self, timeout=30):
        return trial.place_as_found(list(self.appears), self.find, self.place, clock=self.clock,
                                    sleep=self.clock.sleep, timeout=timeout)

    def test_every_window_is_placed_in_the_poll_that_first_sees_it(self):
        found = self.run_placement()
        self.assertEqual(found, {r: 'window-' + r for r in self.appears})
        for role, _, when in self.placed:
            self.assertLessEqual(when - self.appears[role], trial.WINDOW_POLL_SECONDS + 1e-6, role)
        self.assertEqual([p[0] for p in self.placed], ['player-1', 'player-2', 'authority'])

    def test_windows_are_polled_fast_enough_to_be_only_a_flash(self):
        self.assertLessEqual(trial.WINDOW_POLL_SECONDS, 0.05)

    def test_missing_window_is_named_at_the_timeout(self):
        self.appears['authority'] = 10 ** 6
        with self.assertRaisesRegex(trial.TrialAborted, 'authority'):
            self.run_placement(timeout=8)
        self.assertEqual([p[0] for p in self.placed], ['player-1', 'player-2'])


class RunCommandTests(unittest.TestCase):
    """Real child processes: a launcher script leaves a long-lived process holding its output."""

    def test_command_returns_when_it_exits_even_if_a_process_it_started_keeps_its_output_open(self):
        import sys, time
        child = ("import subprocess, sys; subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(8)'], "
                 "stdout=sys.stdout, stderr=sys.stderr); print('server ready')")
        started = time.monotonic()
        code, stdout, _ = trial.run_command([sys.executable, '-c', child], timeout=30)
        self.assertEqual((code, stdout.strip()), (0, 'server ready'))
        self.assertLess(time.monotonic() - started, 5)

    def test_timeout_is_raised_promptly_and_never_waits_for_output_to_close(self):
        import sys, time
        started = time.monotonic()
        with self.assertRaises(TimeoutError):
            trial.run_command([sys.executable, '-c', 'import time; time.sleep(20)'], timeout=1)
        self.assertLess(time.monotonic() - started, 6)

    def test_orphaned_helper_survives_the_runner_and_its_whole_process_tree_being_killed(self):
        """trial157: an interrupted runner took its watchdog down with it and left three games running."""
        import subprocess, sys, time
        directory = tempfile.TemporaryDirectory(); self.addCleanup(directory.cleanup)
        marker = Path(directory.name) / 'helper.pid'
        runner = ("import importlib.util, sys, time\n"
                  f"spec = importlib.util.spec_from_file_location('native_trial', r'{path}')\n"
                  "trial = importlib.util.module_from_spec(spec); spec.loader.exec_module(trial)\n"
                  f"pid = trial.spawn_orphan([sys.executable, '-c', 'import time; time.sleep(60)'], r'{directory.name}\\helper.log')\n"
                  f"open(r'{marker}', 'w').write(str(pid))\n"
                  "time.sleep(60)\n")
        parent = subprocess.Popen([sys.executable, '-c', runner])
        self.addCleanup(parent.kill)
        cutoff = time.monotonic() + 20
        while time.monotonic() < cutoff and not (marker.exists() and marker.read_text().strip()):
            time.sleep(0.1)
        helper = int(marker.read_text())
        self.addCleanup(subprocess.run, ['taskkill', '/F', '/PID', str(helper)], capture_output=True)
        self.assertTrue(trial.process_alive(helper))
        subprocess.run(['taskkill', '/T', '/F', '/PID', str(parent.pid)], capture_output=True, check=True)
        parent.wait(10)
        time.sleep(0.5)
        self.assertTrue(trial.process_alive(helper), 'the helper died with the runner')

    def test_exit_code_and_error_text_are_returned(self):
        import sys
        code, _, stderr = trial.run_command([sys.executable, '-c', "import sys; sys.stderr.write('bad'); sys.exit(3)"])
        self.assertEqual((code, stderr), (3, 'bad'))


class CloseTrialTests(unittest.TestCase):
    ROLES = ('player-1', 'player-2', 'authority')

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(); self.addCleanup(self.directory.cleanup)
        self.base = Path(self.directory.name); self.root = self.base / 'trial'; self.root.mkdir()
        self.private = self.base / 'server/private'; self.private.mkdir(parents=True)
        (self.root / 'plan.json').write_text(json.dumps(dict(server_private=str(self.private))))
        self.clock = Clock(); self.commands = []; self.alive = set(); self.exit_codes = {}
        for index, name in enumerate(self.ROLES):
            run = self.base / ('run-' + name); run.mkdir()
            (run / 'personal-before.json').write_text('{}')
            (self.root / (name + '.json')).write_text(json.dumps(dict(
                generation=f'{index:032x}', supervisor_pid=700 + index, run=str(run), worker_id='0' + str(index + 1))))
            self.alive.add(700 + index)

    def run_command(self, command):
        self.commands.append(command)
        if command[0] == str(trial.CONTROL):
            pid = int(command[2])
            if command[3] == 'status':
                return 0, json.dumps(dict(result='accepted', epoch=3)), ''
            if command[3] == 'shutdown' and self.refusals.get(pid, 0) > 0:
                self.refusals[pid] -= 1
                return 5, '{"result":"unavailable"}', ''
            if command[3] == 'shutdown' and pid != self.stuck:
                self.alive.discard(pid)
                run = next(self.base / ('run-' + n) for i, n in enumerate(self.ROLES) if 700 + i == pid)
                append(run / 'native-host.jsonl', dict(event='game_exited', exit_code=self.exit_codes.get(pid, 0)),
                       dict(event='worker_exited', exit_code=0))
            return 0, '{"result":"accepted"}', ''
        return 0, '{"ok":true}', ''

    stuck = None
    refusals = {}

    def close(self):
        return trial.close_trial(self.root, run=self.run_command, alive=lambda pid: pid in self.alive,
                                 clock=self.clock, sleep=self.clock.sleep)

    def test_every_worker_is_shut_down_at_its_reported_epoch_and_its_evidence_kept(self):
        result = self.close()
        self.assertEqual(result, dict(clean=True, failures=[]) | {'completed_utc': result['completed_utc']})
        shutdowns = [c[2:] for c in self.commands if c[0] == str(trial.CONTROL) and c[3] == 'shutdown']
        self.assertEqual(shutdowns, [['700', 'shutdown', '3'], ['701', 'shutdown', '3'], ['702', 'shutdown', '3']])
        for name in self.ROLES:
            self.assertTrue((self.root / ('closed-' + name) / 'native-host.jsonl').is_file())
        self.assertEqual((self.private / 'stop.request').read_text(), 'stop')
        self.assertTrue(json.loads((self.root / 'closure-result.json').read_text())['clean'])
        tools = [Path(c[2]).name for c in self.commands if c[0] != str(trial.CONTROL)]
        self.assertEqual(tools, ['worker-personal.py', 'sporemp_diag.py', 'sporemp_diag.py'] * 3)

    def test_worker_still_starting_takes_its_shutdown_a_few_seconds_later(self):
        # Trial251: a game that had not finished starting refused its shutdown and was left running.
        self.refusals = {701: 2}
        result = self.close()
        self.assertEqual((result['clean'], result['failures']), (True, []))
        asked = [c[2] for c in self.commands if c[0] == str(trial.CONTROL) and c[3] == 'shutdown']
        self.assertEqual(asked, ['700', '701', '701', '701', '702'])

    def test_worker_that_keeps_refusing_its_shutdown_is_reported_after_a_bounded_wait(self):
        self.refusals = {701: 10 ** 6}
        started = self.clock.now
        result = self.close()
        self.assertIn('Shutdown rejected: player-2', result['failures'])
        self.assertLess(self.clock.now - started, trial.STARTING_SECONDS + trial.SHUTDOWN_SECONDS + 10)

    def test_second_closure_is_refused_by_the_claim(self):
        self.close(); count = len(self.commands)
        self.assertIsNone(self.close())
        self.assertEqual(len(self.commands), count)

    def test_nonzero_game_exit_is_a_recorded_failure(self):
        self.exit_codes[701] = 35
        result = self.close()
        self.assertEqual((result['clean'], result['failures']), (False, ['Non-clean exit: player-2']))

    def test_supervisor_that_never_exits_leaves_evidence_uncopied(self):
        self.stuck = 702
        result = self.close()
        self.assertEqual((result['clean'], result['failures']), (False, ['Supervisors still running: authority']))
        self.assertFalse((self.root / 'closed-authority').exists())
        self.assertGreaterEqual(self.clock.now, 20)

    def test_workers_that_never_started_are_skipped(self):
        (self.root / 'authority.json').unlink(); self.alive.discard(702)
        self.assertTrue(self.close()['clean'])
        self.assertFalse((self.root / 'closed-authority').exists())


class FakeOps:
    """Stands in for every process and Win32 effect; writes the evidence a real closure would."""

    def __init__(self, directory, fail=None, games_after_close=False):
        self.directory, self.fail, self.games_after_close = Path(directory), fail, games_after_close
        self.calls = []
        self.closed = False

    def step(self, name):
        self.calls.append(name)
        if self.fail == name:
            raise RuntimeError(name + ' broke')

    def preflight(self): self.step('preflight')
    def restore_fixture(self, artifact, output): self.step('restore_fixture')
    def freeze(self, root, scenario): self.step('freeze')
    def hold_display(self, root): self.step('hold_display'); return (3000, 0, 4920, 1080)
    def start_server(self, root, name): self.step('start_server'); return self.directory / 'private'
    def configure_authority(self, private, overrides): self.step('configure_authority')
    def start_watchdog(self, root): self.step('start_watchdog')
    def park(self, root, roles, display): self.step('park')
    def start_captures(self, root, roles, seconds): self.step('start_captures')
    def finish_captures(self, root, roles): self.step('finish_captures')
    def release_display(self, root): self.step('release_display')
    def stop_server(self, root, name): self.step('stop_server')
    def undo_fixture(self, journal): self.step('undo_fixture')
    def games_running(self): return self.games_after_close if self.closed else False
    def control(self, command): return 0, '{"result":"accepted"}', ''

    def start_worker(self, root, role, private):
        self.step('start_worker:' + role)
        run = self.directory / ('run-' + role); run.mkdir()
        return dict(generation='ab' * 16, supervisor_pid=1, run=str(run), worker_id='01')

    def close(self, root):
        self.step('close'); self.closed = True
        import shutil
        scratch = self.directory / 'evidence'
        closed_trial(scratch)
        for item in scratch.iterdir():
            if item.name != 'fixture-restore' and not (root / item.name).exists():
                shutil.move(str(item), str(root / item.name))
        shutil.rmtree(scratch)


class RunTrialTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(); self.addCleanup(self.directory.cleanup)
        self.base = Path(self.directory.name); self.root = self.base / 'trial150'
        (self.base / 'local').mkdir(); (self.base / 'local/checkpoint.smp').write_bytes(b'x')
        self.clock = Clock()

    def scenario(self, drive='pass', artifact=True, review=''):
        file = self.base / 'local/goalcard.py'
        file.write_text((("ARTIFACT='local/checkpoint.smp'\n") if artifact else '') + "AUTHORITY={'schema':7}\n"
                        f'def drive(trial):\n    {drive}\n{review}', encoding='utf-8')
        return trial.load_scenario(file, repo=self.base)

    def run_trial(self, ops, **scenario):
        verdict = trial.run_trial(self.scenario(**scenario), self.root, ops, clock=self.clock, sleep=self.clock.sleep)
        if (self.root / 'fixture-restore').is_dir() or 'restore_fixture' in ops.calls:
            pass
        return verdict

    def test_sealing_trial_seals_after_closure_and_before_the_fixture_is_undone(self):
        ops = FakeOps(self.base)
        ops.restore_fixture = lambda artifact, output: (ops.step('restore_fixture'), output.mkdir(),
            (output / 'restore.json').write_text(json.dumps(dict(state='restored', entries=[]))))
        ops.undo_fixture = lambda journal: (ops.step('undo_fixture'),
            journal.write_text(json.dumps(dict(state='originals_restored', entries=[]))))
        ops.sealing, ops.seal = True, lambda root: ops.step('seal')
        verdict = self.run_trial(ops, drive="trial.expect(True, 'saved')")
        self.assertEqual(ops.calls[-4:], ['finish_captures', 'release_display', 'seal', 'undo_fixture'])
        self.assertIn('seal', [p['phase'] for p in verdict['phases']])

    def test_failed_scenario_is_not_sealed(self):
        ops = FakeOps(self.base)
        ops.sealing, ops.seal = True, lambda root: ops.step('seal')
        verdict = self.run_trial(ops, drive="trial.expect(False, 'the save did not complete')")
        self.assertEqual(verdict['outcome'], 'FAIL')
        self.assertNotIn('seal', ops.calls)

    def test_complete_trial_runs_every_phase_in_order_and_passes(self):
        ops = FakeOps(self.base)
        # A real restore leaves a journal that undo flips; emulate both ends.
        ops.restore_fixture = lambda artifact, output: (ops.step('restore_fixture'), output.mkdir(),
            (output / 'restore.json').write_text(json.dumps(dict(state='restored', entries=[]))))
        ops.undo_fixture = lambda journal: (ops.step('undo_fixture'),
            journal.write_text(json.dumps(dict(state='originals_restored', entries=[]))))
        verdict = self.run_trial(ops, drive="trial.expect(True, 'B shows Hunt 1/3')",
                                 review="def review(closed):\n    closed.expect_none('authority', 'native_award_return')\n")
        self.assertEqual(ops.calls, ['preflight', 'restore_fixture', 'freeze', 'hold_display', 'start_server',
            'configure_authority', 'start_watchdog', 'start_worker:player-1', 'start_worker:player-2',
            'start_worker:authority', 'park', 'start_captures', 'close', 'stop_server', 'finish_captures',
            'release_display', 'undo_fixture'])
        self.assertEqual((verdict['outcome'], verdict['failed_checks'], verdict['error']), ('PASS', [], None))
        labels = [c['label'] for c in verdict['checks']]
        self.assertIn('B shows Hunt 1/3', labels); self.assertIn('authority: no native_award_return', labels)
        self.assertIn('closure clean', labels)
        self.assertEqual(json.loads((self.root / 'verdict.json').read_text(encoding='utf-8'))['outcome'], 'PASS')
        self.assertEqual([p['phase'] for p in verdict['phases']][:3], ['preflight', 'fixture_restore', 'freeze'])
        self.assertTrue(all(p['seconds'] >= 0 for p in verdict['phases']))
        self.assertEqual(json.loads((self.root / 'authority.json').read_text())['supervisor_pid'], 1)
        self.assertIn('# trial150', (self.root / 'RECORD.md').read_text(encoding='utf-8'))
        self.assertIn('deadline_utc', json.loads((self.root / 'plan.json').read_text()))
        self.assertEqual(json.loads((self.root / 'plan.json').read_text())['server_private'], str(self.base / 'private'))

    def test_failed_expectation_still_closes_restores_and_reports_fail(self):
        ops = FakeOps(self.base)
        verdict = self.run_trial(ops, drive="trial.expect(False, 'A reaches 14 DNA')")
        self.assertEqual((verdict['outcome'], verdict['error']), ('FAIL', 'A reaches 14 DNA'))
        for step in ('close', 'stop_server', 'release_display', 'undo_fixture'):
            self.assertIn(step, ops.calls)

    def test_scenario_bug_is_reported_and_the_trial_still_closes(self):
        ops = FakeOps(self.base)
        verdict = self.run_trial(ops, drive="{}['missing']")
        self.assertEqual(verdict['outcome'], 'FAIL'); self.assertIn('KeyError', verdict['error'])
        self.assertIn('close', ops.calls); self.assertIn('undo_fixture', ops.calls)

    def test_worker_launch_failure_closes_the_ones_already_started(self):
        ops = FakeOps(self.base, fail='start_worker:player-2')
        verdict = self.run_trial(ops)
        self.assertEqual(verdict['outcome'], 'FAIL'); self.assertIn('start_worker:player-2 broke', verdict['error'])
        self.assertNotIn('start_worker:authority', ops.calls); self.assertNotIn('park', ops.calls)
        self.assertLess(ops.calls.index('close'), ops.calls.index('undo_fixture'))

    def test_failed_restore_launches_nothing_and_does_not_undo(self):
        ops = FakeOps(self.base, fail='restore_fixture')
        verdict = self.run_trial(ops)
        self.assertEqual(ops.calls, ['preflight', 'restore_fixture'])
        self.assertEqual(verdict['outcome'], 'FAIL')

    def test_missing_virtual_display_stops_before_any_process_starts(self):
        ops = FakeOps(self.base, fail='hold_display')
        self.run_trial(ops)
        self.assertEqual(ops.calls, ['preflight', 'restore_fixture', 'freeze', 'hold_display', 'release_display', 'undo_fixture'])

    def test_fixture_is_left_alone_while_a_game_is_still_running(self):
        ops = FakeOps(self.base, games_after_close=True)
        verdict = self.run_trial(ops, drive="trial.expect(True, 'ok')")
        self.assertNotIn('undo_fixture', ops.calls)
        self.assertEqual(verdict['outcome'], 'FAIL'); self.assertIn('MANUAL_RECOVERY_REQUIRED', verdict['error'])

    def test_verdict_names_the_input_classes_the_scenario_used(self):
        used = self.run_trial(FakeOps(self.base), drive="trial.control('player-1', 'input', 4, 1, 87); "
                              "trial.control('player-2', 'network_action', 4, 2, 9); trial.expect(True, 'ok')")
        self.assertEqual(used['input_class'], 'DIAGNOSTIC_NETWORK_INTENT+INPUT_IN_PROCESS')

    def test_trial_that_sends_nothing_has_no_input_class(self):
        self.root = self.base / 'trial151'
        quiet = self.run_trial(FakeOps(self.base), drive="trial.control('authority', 'save', 4); trial.expect(True, 'ok')")
        self.assertEqual(quiet['input_class'], 'NONE')

    def test_scenario_without_a_fixture_skips_restore_and_undo(self):
        ops = FakeOps(self.base)
        verdict = self.run_trial(ops, drive="trial.expect(True, 'ok')", artifact=False)
        self.assertNotIn('restore_fixture', ops.calls); self.assertNotIn('undo_fixture', ops.calls)
        self.assertEqual(verdict['outcome'], 'PASS')

    def test_existing_trial_directory_is_refused(self):
        self.root.mkdir()
        with self.assertRaises(FileExistsError):
            self.run_trial(FakeOps(self.base))


if __name__ == '__main__':
    unittest.main()
