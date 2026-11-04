"""Native Windows bootstrap layout, Unicode argv and exit-status regression."""
import argparse,json,os,pathlib,shutil,subprocess,tempfile,unittest
P=pathlib.Path
class Bootstrap(unittest.TestCase):
    def test_native_bootstrap(self):
        with tempfile.TemporaryDirectory(dir=CONFIG.out) as directory:
            root=P(directory)/'release ø 測試';inner=root/'dw5e-data/launcher';inner.mkdir(parents=True)
            shutil.copy2(CONFIG.bootstrap,root/'DW5E.exe');shutil.copy2(CONFIG.child,inner/'DW5E.exe')
            search=[CONFIG.child.parent,*[P(p) for p in os.environ.get('PATH','').split(os.pathsep) if p]]
            if CONFIG.runtime_bin:search.insert(0,CONFIG.runtime_bin)
            for name in ('libc++.dll','libunwind.dll','libwinpthread-1.dll'):
                source=next((p/name for p in search if (p/name).is_file()),None)
                if source:shutil.copy2(source,inner/name)
            cwd=P(directory)/'unrelated';cwd.mkdir()
            report=P(directory)/'reports';report.mkdir();env=os.environ.copy();env['DW5E_FIXTURE_REPORT']=str(report);env['DW5E_FIXTURE_EXIT']='37'
            arguments=['bootstrap-probe','path with spaces ø 測試',r'literal"quote',r'ends\\']
            result=subprocess.run([str(root/'DW5E.exe'),*arguments],cwd=cwd,env=env,capture_output=True,timeout=30)
            self.assertEqual(result.returncode,37,result.stderr)
            actual=json.loads((report/'bootstrap.json').read_text(encoding='utf-8'))
            self.assertEqual(actual['args'][1:],arguments)
            self.assertEqual(P(actual['cwd']),cwd)
            self.assertFalse(list(root.glob('*.dll')),'outer bootstrap needs only Windows system libraries')
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--bootstrap',type=P,required=True);parser.add_argument('--child',type=P,required=True);parser.add_argument('--out',type=P,required=True);parser.add_argument('--runtime-bin',type=P);CONFIG=parser.parse_args();CONFIG.out.mkdir(parents=True,exist_ok=True)
    CONFIG.bootstrap=CONFIG.bootstrap.resolve();CONFIG.child=CONFIG.child.resolve();CONFIG.out=CONFIG.out.resolve()
    unittest.main(argv=[__file__],verbosity=2)
