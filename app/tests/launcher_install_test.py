"""Disc-free tests of the actual launcher, plus its production main/pipeline with synthetic GUI selection."""
import argparse, contextlib, ctypes, hashlib, json, os, pathlib, shutil, subprocess, sys, tempfile, unittest, uuid
P = pathlib.Path
CONFIG = None

def put(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(value, encoding='utf-8')

def hashes(root):
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in root.rglob('*') if p.is_file()}

def own_directory_handles(folder):
    """Inspect only this test process's file handles when a move fails."""
    if os.name != 'nt':
        return []
    from ctypes import wintypes as w
    class Entry(ctypes.Structure):
        _fields_ = [('object', ctypes.c_void_p), ('pid', ctypes.c_size_t),
                    ('handle', ctypes.c_size_t), ('access', w.ULONG),
                    ('trace', w.USHORT), ('kind', w.USHORT),
                    ('attributes', w.ULONG), ('reserved', w.ULONG)]
    nt = ctypes.WinDLL('ntdll')
    needed = w.ULONG()
    size = 1024 * 1024
    while size <= 64 * 1024 * 1024:
        buffer = ctypes.create_string_buffer(size)
        status = nt.NtQuerySystemInformation(64, buffer, size, ctypes.byref(needed))
        if status == 0:
            break
        size = max(size * 2, needed.value + 65536)
    else:
        return [{'query_failed': True}]
    count = ctypes.c_size_t.from_buffer(buffer).value
    entries = (Entry * count).from_buffer(buffer, ctypes.sizeof(ctypes.c_size_t) * 2)
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.GetFileType.argtypes = [ctypes.c_void_p]
    kernel.GetFinalPathNameByHandleW.argtypes = [ctypes.c_void_p, w.LPWSTR, w.DWORD, w.DWORD]
    result = []
    for entry in entries:
        if entry.pid != os.getpid() or kernel.GetFileType(entry.handle) != 1:
            continue
        text = ctypes.create_unicode_buffer(32768)
        if kernel.GetFinalPathNameByHandleW(entry.handle, text, len(text), 0):
            value = text.value
            if str(folder).casefold() in value.casefold():
                result.append({'handle': entry.handle, 'path': value, 'access': entry.access})
    return result

def windows_lock_owners(folder):
    """Read-only Restart Manager diagnostics; never closes or restarts an application."""
    if os.name != 'nt':
        return {}
    from ctypes import wintypes as w
    class UniqueProcess(ctypes.Structure):
        _fields_ = [('pid', w.DWORD), ('started', w.FILETIME)]
    class ProcessInfo(ctypes.Structure):
        _fields_ = [('process', UniqueProcess), ('name', w.WCHAR * 256),
                    ('service', w.WCHAR * 64), ('kind', w.UINT), ('status', w.DWORD),
                    ('session', w.DWORD), ('restartable', w.BOOL)]
    rm = ctypes.WinDLL('rstrtmgr')
    session = w.DWORD()
    key = ctypes.create_unicode_buffer(33)
    result = {'start': rm.RmStartSession(ctypes.byref(session), 0, key)}
    if result['start']:
        return result
    try:
        resources = [p for p in (folder/'DW5E.exe', folder/'build/dw5e.exe',
                     folder/'libc++.dll', folder/'libunwind.dll') if p.is_file()]
        names = (w.LPCWSTR * len(resources))(*map(str, resources))
        result['register'] = rm.RmRegisterResources(session, len(resources), names, 0, None, 0, None)
        needed, count, reasons = w.UINT(), w.UINT(), w.DWORD()
        result['query'] = rm.RmGetList(session, ctypes.byref(needed), ctypes.byref(count), None, ctypes.byref(reasons))
        if needed.value:
            buffer = (ProcessInfo * needed.value)()
            count.value = needed.value
            result['query'] = rm.RmGetList(session, ctypes.byref(needed), ctypes.byref(count), buffer, ctypes.byref(reasons))
            result['owners'] = [{'pid': buffer[i].process.pid, 'name': buffer[i].name} for i in range(count.value)]
        return result
    finally:
        rm.RmEndSession(session)

class LauncherInstallTests(unittest.TestCase):

    def setUp(self):
        self.root = P(tempfile.mkdtemp(prefix='case-', dir=CONFIG.out)).resolve()
        self.root.chmod(493)
        self.package = self.root / 'package space ø 測試'
        self.package.mkdir()
        self.cwd = self.root / 'unrelated-cwd'
        self.cwd.mkdir()
        self.home = self.root / 'fake-user-data'
        self.home.mkdir()
        self.reports = self.root / 'reports'
        self.reports.mkdir()
        self.env = os.environ.copy()
        for key in ('DW5E_DATA_DIR', 'APPIMAGE', 'DW5E_FIXTURE_GUI_FOLDER'):
            self.env.pop(key, None)
        for key in ('HOME', 'USERPROFILE', 'LOCALAPPDATA', 'XDG_DATA_HOME'):
            self.env[key] = str(self.home)
        self.env['DW5E_FIXTURE_REPORT'] = str(self.reports)
        self.suffix = '.exe' if os.name == 'nt' else ''
        self.launcher = self.package / ('DW5E' + self.suffix)
        self.driver = self.package / ('selection-driver' + self.suffix)
        shutil.copy2(CONFIG.launcher, self.launcher)
        shutil.copy2(CONFIG.selection_driver, self.driver)
        put(self.package / 'app/CMakeLists.txt', '# synthetic package sources\n')
        put(self.package / 'dw5e_manifest.toml', '# synthetic packaged manifest, must not be overwritten\n')
        put(self.package / 'BUILD_ID', 'fixture-v1\n')
        helper = CONFIG.game
        names = ['tools/rexglue' + self.suffix, 'tools/cmake/bin/cmake' + self.suffix, 'tools/ninja' + self.suffix]
        compiler = 'tools/llvm-mingw/bin/x86_64-w64-mingw32-clang++.exe' if os.name == 'nt' else 'tools/clang/bin/clang++'
        for name in names + [compiler]:
            dst = self.package / name
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(helper, dst)
        self.runtime_dirs = [CONFIG.launcher.parent, CONFIG.game.parent, *(P(d) for d in os.environ.get('PATH', '').split(os.pathsep) if d)]
        if CONFIG.runtime_bin:
            self.runtime_dirs.insert(0, CONFIG.runtime_bin)
        if os.name == 'nt':
            for name in ('libc++.dll', 'libunwind.dll', 'libwinpthread-1.dll'):
                source = next((d / name for d in self.runtime_dirs if (d / name).is_file()), None)
                if name != 'libwinpthread-1.dll':
                    self.assertIsNotNone(source, 'native compiler runtime ' + name)
                if source:
                    shutil.copy2(source, self.package / name)
                    shutil.copy2(source, self.package / 'tools/llvm-mingw/bin' / name)
            self.env['PATH'] = os.pathsep.join(map(str, self.runtime_dirs)) + os.pathsep + self.env.get('PATH', '')
        self.original_user = hashes(self.home)
        self.source_manifest = (self.package / 'dw5e_manifest.toml').read_bytes()
        self.serial = 0

    def tearDown(self):
        self.assertEqual(hashes(self.home), self.original_user, 'no writes to platform user-data roots')

    def installed(self, root):
        # A complete installed fixture includes setup logs. Linux fast launch appends to this file.
        put(root / 'logs/game.log', '')
        put(root / 'game/installed.txt', 'authored synthetic marker\n')
        put(root / 'game/files/default.xex', 'authored synthetic non-retail bytes\n')
        put(root / 'save/sentinel', 'save bytes that must survive\n')
        put(root / 'settings.toml', 'settings bytes that must survive\n')
        (root / 'build').mkdir(exist_ok=True)
        shutil.copy2(CONFIG.game, root / 'build' / ('dw5e' + self.suffix))
        put(root / 'build/built-from.txt', 'fixture-v1\n')

    def sentinels(self, root):
        return {str(p.relative_to(root)): p.read_bytes() for p in (root / 'save/sentinel', root / 'settings.toml', root / 'game/files/default.xex')}

    def launch(self, *args, expected=0, game=False, driver=False, drop_privileges=False):
        """Wait for real child completion before relocating its executable or working folder."""
        self.serial += 1
        report = self.reports / 'game.json'
        report.unlink(missing_ok=True)
        env = self.env.copy()
        handle = None
        ack = None
        process_handle = None
        if os.name == 'nt' and game:
            kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            kernel.CreateEventW.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_wchar_p]
            kernel.CreateEventW.restype = ctypes.c_void_p
            kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
            kernel.CloseHandle.argtypes = [ctypes.c_void_p]
            event = 'Local\\DW5E-install-test-' + uuid.uuid4().hex
            handle = kernel.CreateEventW(None, True, False, event)
            self.assertTrue(handle)
            env['DW5E_FIXTURE_EVENT'] = event
            ack = kernel.CreateEventW(None, True, False, event + '-ack')
            self.assertTrue(ack)
            env['DW5E_FIXTURE_ACK_EVENT'] = event + '-ack'
            kernel.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
            kernel.OpenProcess.restype = ctypes.c_void_p
            kernel.SetEvent.argtypes = [ctypes.c_void_p]

        def drop():
            os.setgroups([])
            os.setgid(65534)
            os.setuid(65534)
        try:
            launch_options = {'preexec_fn': drop} if drop_privileges and os.name != 'nt' and os.geteuid() == 0 else {}
            # File-backed diagnostics let us wait for the launcher without waiting for pipe EOF
            # from its detached child, which is deliberately waiting for our acknowledgement.
            log = self.root / f'launch-{self.serial}.log'
            with log.open('wb') as diagnostics:
                process = subprocess.run(
                    [str(self.driver if driver else self.launcher), *map(str, args), '--audio_mute=true'],
                    cwd=self.cwd, env=env, stdout=diagnostics, stderr=subprocess.STDOUT,
                    timeout=30, **launch_options)
            process.stdout = log.read_text(encoding='utf-8', errors='replace')
            process.stderr = ''
            self.assertEqual(process.returncode, expected, process.stdout)
            if game:
                if handle:
                    self.assertEqual(kernel.WaitForSingleObject(handle, 10000), 0, 'native detached child completed')
                self.assertTrue(report.is_file(), 'actual native child wrote report')
                result = json.loads(report.read_text(encoding='utf-8'))
                if handle:
                    process_handle = kernel.OpenProcess(0x00101000, False, result['pid'])
                    self.assertTrue(process_handle, 'retain the actual child before allowing its exit')
                    kernel.SetEvent(ack)
                    self.assertEqual(kernel.WaitForSingleObject(process_handle, 10000), 0, 'actual child process exit completes before relocation')
                    code = ctypes.c_uint32()
                    kernel.GetExitCodeProcess.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32)]
                    self.assertTrue(kernel.GetExitCodeProcess(process_handle, ctypes.byref(code)))
                    self.assertEqual(code.value, 0, 'synthetic game completed successfully, without hitting its safety timeout')
                return result
            self.assertFalse(report.exists(), 'no game started for failure/build-only route')
            return process
        finally:
            if ack:
                kernel.SetEvent(ack)
                kernel.CloseHandle(ack)
            if process_handle:
                kernel.CloseHandle(process_handle)
            if handle:
                kernel.CloseHandle(handle)

    def check_game(self, report, root):
        self.assertEqual(P(report['cwd']), root)
        for key, path in (('game_data_root', root / 'game/files'), ('user_data_root', root), ('cache_root', root / 'cache')):
            self.assertIn('--' + key + '=' + path.as_posix(), report['args'])
        self.assertIn('--audio_mute=true', report['args'])

    def test_default_fast_path_and_whole_install_move_then_rebuild(self):
        self.installed(self.package)
        original = self.sentinels(self.package)
        original_package = hashes(self.package)
        self.check_game(self.launch(game=True), self.package)
        self.assertEqual(hashes(self.package), original_package,
                         'fast launch leaves the complete package/player inventory unchanged before relocation')
        moved = self.root / 'moved package ø 測試'
        try:
            self.package.rename(moved)
        except OSError:
            (self.root/'rename-locks.json').write_text(json.dumps({'file_owners':windows_lock_owners(self.package),'test_handles':own_directory_handles(self.package)},indent=2),encoding='utf-8')
            raise
        self.package = moved
        self.launcher = moved / self.launcher.name
        self.driver = moved / self.driver.name
        self.assertFalse((self.root / 'package space ø 測試').exists())
        self.check_game(self.launch(game=True), moved)
        self.assertEqual(self.sentinels(moved), original)
        put(moved / 'build/CMakeCache.txt', 'CMAKE_HOME_DIRECTORY:INTERNAL=' + str(self.root / 'old/app') + '\n')
        put(moved / 'build/CMakeFiles/obsolete', 'old path')
        put(moved / 'build/_deps/keep', 'dependency sentinel')
        self.launch('--no-window', '--rebuild')
        self.assertFalse((moved / 'build/CMakeFiles/obsolete').exists())
        self.assertEqual((moved / 'build/_deps/keep').read_text(encoding='utf-8'), 'dependency sentinel')
        source = moved / ('app' if os.name == 'nt' else 'builder/app')
        self.assertIn(str(source).replace('\\', '/'), (moved / 'build/CMakeCache.txt').read_text(encoding='utf-8').replace('\\', '/'))
        self.assertEqual(self.sentinels(moved), original)
        self.assertEqual((moved / 'dw5e_manifest.toml').read_bytes(), self.source_manifest)
        self.check_game(self.launch(game=True), moved)

    def test_same_invocation_gui_choice_and_next_run_fast_path(self):
        selected = self.root / 'custom ø 測試'
        self.installed(selected)
        original = self.sentinels(selected)
        self.env['DW5E_FIXTURE_GUI_FOLDER'] = str(selected)
        self.check_game(self.launch(driver=True, game=True), selected)
        self.env.pop('DW5E_FIXTURE_GUI_FOLDER')
        self.assertEqual((selected / 'build/built-from.txt').read_text(encoding='utf-8').strip(), 'fixture-v1')
        self.check_game(self.launch(game=True), selected)
        self.assertEqual(self.sentinels(selected), original)
        self.assertFalse((self.package / 'build').exists())

    def test_overrides_and_external_choice_survive_launcher_only_move(self):
        selected = self.root / 'remembered'
        self.installed(selected)
        put(self.package / 'dw5e-install.json', json.dumps({'version': 1, 'data_dir': str(selected)}))
        saved = (self.package / 'dw5e-install.json').read_bytes()
        envroot = self.root / 'environment ø 測試'
        cli = self.root / 'command ø 測試'
        self.installed(envroot)
        self.installed(cli)
        self.env['DW5E_DATA_DIR'] = str(envroot)
        self.check_game(self.launch(game=True), envroot)
        self.check_game(self.launch('--data-dir', cli, game=True), cli)
        self.assertEqual((self.package / 'dw5e-install.json').read_bytes(), saved)
        self.env.pop('DW5E_DATA_DIR')
        moved = self.root / 'launcher moved'
        try:
            self.package.rename(moved)
        except OSError:
            (self.root/'rename-locks.json').write_text(json.dumps({'file_owners':windows_lock_owners(self.package),'test_handles':own_directory_handles(self.package)},indent=2),encoding='utf-8')
            raise
        self.package = moved
        self.launcher = moved / self.launcher.name
        self.driver = moved / self.driver.name
        self.check_game(self.launch(game=True), selected)

    def test_in_place_update_and_copy_first_migration(self):
        old = self.root / 'old-location'
        self.installed(old)
        old_hashes = hashes(old)
        shutil.copytree(old, self.package, dirs_exist_ok=True)
        put(self.package / 'BUILD_ID', 'fixture-v2\n')
        self.launch('--no-window')
        self.assertEqual((self.package / 'build/built-from.txt').read_text(encoding='utf-8').strip(), 'fixture-v2')
        self.assertEqual(self.sentinels(self.package), self.sentinels(old))
        self.assertEqual(hashes(old), old_hashes)
        self.assertEqual((self.package / 'dw5e_manifest.toml').read_bytes(), self.source_manifest)
        self.check_game(self.launch(game=True), self.package)

    def test_malformed_missing_and_file_destinations_never_fall_back(self):
        config = self.package / 'dw5e-install.json'
        for text in ('{broken', json.dumps({'version': 1, 'data_dir': 'missing-volume/folder'})):
            put(config, text)
            result = self.launch('--no-window', expected=1)
            self.assertTrue('destination' in (result.stdout + result.stderr).lower() or 'choice' in (result.stdout + result.stderr).lower())
            self.assertEqual(config.read_text(encoding='utf-8'), text)
        invalid = self.root / 'is-a-file'
        put(invalid, 'preserve')
        result = self.launch('--no-window', '--data-dir', invalid, expected=1)
        self.assertIn(invalid.name, result.stdout + result.stderr)
        self.assertFalse((self.package / 'game').exists())
        self.assertFalse((self.package / 'missing-volume').exists())
        self.assertEqual(invalid.read_text(encoding='utf-8'), 'preserve')

    @contextlib.contextmanager
    def read_only(self, folder):
        if os.name == 'nt':
            import csv, io
            user = subprocess.run(['whoami', '/user', '/fo', 'csv', '/nh'], capture_output=True, text=True, check=True)
            sid = next(csv.reader(io.StringIO(user.stdout)))[1]
            result = subprocess.run(['icacls', str(folder), '/deny', '*' + sid + ':(OI)(CI)(WD,AD,WEA,WA)'], capture_output=True)
            self.assertEqual(result.returncode, 0, 'set test-only write-deny ACL')
            try:
                yield
            finally:
                result = subprocess.run(['icacls', str(folder), '/remove:d', '*' + sid], capture_output=True)
                self.assertEqual(result.returncode, 0, 'restore test-only ACL')
        else:
            mode = folder.stat().st_mode
            folder.chmod(365)
            try:
                yield
            finally:
                folder.chmod(mode)

    def test_actual_write_denial_fails_before_work_and_sidecar_failure_preserves_choice(self):
        self.installed(self.package)
        original = self.sentinels(self.package)
        put(self.package / 'dw5e-install.json', json.dumps({'version': 1, 'data_dir': '.'}))
        saved = (self.package / 'dw5e-install.json').read_bytes()
        selected = self.root / 'writable-custom'
        selected.mkdir()
        selected.chmod(511)
        with self.read_only(self.package):

            def drop():
                os.setgroups([])
                os.setgid(65534)
                os.setuid(65534)
            probe = subprocess.run([sys.executable, '-c', 'import pathlib,sys; pathlib.Path(sys.argv[1]).write_text("forbidden")', str(self.package / 'access-proof')], capture_output=True, **{'preexec_fn': drop} if os.name != 'nt' and os.geteuid() == 0 else {})
            self.assertNotEqual(probe.returncode, 0, 'test process really cannot write selected folder')
            self.launch('--no-window', expected=1, drop_privileges=True)
            self.env['DW5E_FIXTURE_GUI_FOLDER'] = str(selected)
            self.launch('--rebuild', driver=True, expected=1, drop_privileges=True)
        self.assertEqual((self.package / 'dw5e-install.json').read_bytes(), saved)
        self.assertEqual(self.sentinels(self.package), original)
        self.assertFalse((selected / 'generated').exists())
        self.assertFalse((self.reports / 'configure.json').exists())

    @unittest.skipIf(os.name == 'nt', 'Linux AppImage discovery and mount lifetime need a Linux native host')
    def test_original_appimage_and_mount_lifetime(self):
        outer = self.root / 'download'
        outer.mkdir()
        image = outer / 'DW5E.AppImage'
        put(image, 'synthetic original AppImage identity')
        self.env['APPIMAGE'] = str(image)
        self.installed(outer)
        self.check_game(self.launch(game=True), outer)
        self.assertTrue((self.reports / 'game.json').exists(), 'launcher waited for child completion')
        self.assertFalse((self.package / 'build').exists())
if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    sys.stderr.reconfigure(encoding='utf-8', errors='replace')
    parser = argparse.ArgumentParser()
    parser.add_argument('--launcher', required=True, type=P)
    parser.add_argument('--game', required=True, type=P)
    parser.add_argument('--selection-driver', required=True, type=P)
    parser.add_argument('--out', required=True, type=P)
    parser.add_argument('--runtime-bin', type=P)
    CONFIG = parser.parse_args()
    CONFIG.launcher = CONFIG.launcher.resolve()
    CONFIG.game = CONFIG.game.resolve()
    CONFIG.selection_driver = CONFIG.selection_driver.resolve()
    CONFIG.out.mkdir(parents=True, exist_ok=True)
    unittest.main(argv=[sys.argv[0]], verbosity=2)
