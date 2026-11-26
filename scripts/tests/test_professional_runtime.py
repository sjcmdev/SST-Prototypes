"""Fresh-process acceptance for F11/F12/F18/F22; point SST_PRO_RUNTIME_BIN at a built MSVC tree."""
import hashlib
import ctypes
import importlib.util
import json
import os
import re
from pathlib import Path
import subprocess
import time

import pytest

ROOT=Path(__file__).resolve().parents[2]
BIN=Path(os.environ.get('SST_PRO_RUNTIME_BIN',ROOT/'build/bin/Release-windows-x86_64'))

def exe(app):
    path=BIN/app/(app+'.exe')
    if not path.is_file(): pytest.skip('Build the MSVC application before running artifact acceptance')
    return path

def environment(root):
    env=os.environ.copy();env['SST_DIAGNOSTICS_ROOT']=str(root);return env

@pytest.mark.parametrize('app',['JFM','CapLab','PlotLab'])
@pytest.mark.parametrize('args',[
    ['--open'],['--open','--version'],['--screenshot'],['--screenshot','x.jpg'],
    ['--screenshot-after-frames'],['--screenshot-after-frames','30'],['--exit-after-screenshot'],
    ['--screenshot','x.png','--screenshot-after-frames','4294967296'],
    ['--log-level=typo'],['--log-file='],['--unknown'],['--version','--help'],['--export-diagnostics'],
])
def test_usage_errors_exit_two_without_bootstrap(app,args,tmp_path):
    root=tmp_path/'diagnostics'
    result=subprocess.run([exe(app),*args],cwd=tmp_path,env=environment(root),capture_output=True,timeout=15)
    assert result.returncode==2,(result.returncode,result.stderr)
    assert b'--help' in result.stderr
    assert not root.exists(), 'Invalid arguments must not initialize logs, crash hooks, configuration or a window'
    assert list(tmp_path.iterdir())==[]

@pytest.mark.parametrize('app',['JFM','CapLab','PlotLab'])
def test_help_and_version_share_compiled_identity_and_manifest(app,tmp_path):
    root=tmp_path/'diagnostics'
    for arg in ['--help','--version']:
        result=subprocess.run([exe(app),arg],cwd=tmp_path,env=environment(root),capture_output=True,timeout=15)
        assert result.returncode==0,result.stderr
        assert result.stdout
        assert not root.exists()
    version=result.stdout.decode()
    manifest=json.loads(exe(app).with_suffix('.build.json').read_text())
    assert manifest['version'] in version
    assert manifest['source_sha256'] in version
    assert manifest['commit'] in version
    for key,path in [('artifact',exe(app)),('private_symbols',exe(app).with_suffix('.pdb'))]:
        assert manifest[key]['sha256']==hashlib.sha256(path.read_bytes()).hexdigest()
    assert 'MSVC' in version and 'Release' in version
    dll=ctypes.WinDLL('version',use_last_error=True)
    dll.GetFileVersionInfoSizeW.argtypes=[ctypes.c_wchar_p,ctypes.POINTER(ctypes.c_uint32)]
    dll.GetFileVersionInfoW.argtypes=[ctypes.c_wchar_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p]
    dll.VerQueryValueW.argtypes=[ctypes.c_void_p,ctypes.c_wchar_p,ctypes.POINTER(ctypes.c_void_p),ctypes.POINTER(ctypes.c_uint)]
    size=dll.GetFileVersionInfoSizeW(str(exe(app)),None);assert size
    buffer=ctypes.create_string_buffer(size);assert dll.GetFileVersionInfoW(str(exe(app)),0,size,buffer)
    value=ctypes.c_void_p();length=ctypes.c_uint()
    assert dll.VerQueryValueW(buffer,r'\StringFileInfo\040904b0\ProductVersion',ctypes.byref(value),ctypes.byref(length))
    assert ctypes.wstring_at(value,length.value).rstrip('\0')==manifest['version']

def test_plotlab_rejects_missing_script_and_checks_remaining_args(tmp_path):
    for args in [['--worksheet-script'],['--worksheet-script','--version'],['--worksheet-script','x.py','--unknown']]:
        result=subprocess.run([exe('PlotLab'),*args],cwd=tmp_path,env=environment(tmp_path/'diag'),capture_output=True,timeout=15)
        assert result.returncode==2
    assert not (tmp_path/'diag').exists()

@pytest.mark.parametrize('app',['JFM','CapLab','PlotLab'])
@pytest.mark.parametrize('mode',['--crash-probe','--crash-probe=terminate','--crash-probe=signal'])
def test_native_crash_has_local_minidump_and_matching_symbols(app,mode,tmp_path):
    root=tmp_path/'diagnostics-zażółć-图'
    result=subprocess.run([exe(app),mode],cwd=tmp_path,env=environment(root),capture_output=True,timeout=25)
    assert result.returncode!=0
    dumps=list(root.glob('crashes/session-*/crash.dmp'))
    assert len(dumps)==1,(result.returncode,result.stderr,list(root.rglob('*')))
    data=dumps[0].read_bytes();assert data[:4]==b'MDMP'
    assert dumps[0].with_name('pending').exists()
    binary=exe(app).read_bytes();pos=binary.index(b'RSDS');codeview=binary[pos:pos+24]
    assert codeview in data,'Dump must identify this exact executable build'
    assert codeview[4:20] in exe(app).with_suffix('.pdb').read_bytes(),'Private PDB must match the executable CodeView GUID'
    report=tmp_path/'report.txt'
    result=subprocess.run([exe(app),'--export-diagnostics',report],cwd=tmp_path,env=environment(root),capture_output=True,timeout=15)
    assert result.returncode==0,result.stderr
    text=report.read_text();assert 'Interrupted previous sessions: 1' in text
    assert str(tmp_path) not in text
    assert list(tmp_path.glob('*.dmp'))==[], 'Default export must never copy the dump'

def test_kill_restart_and_active_session_retention(tmp_path):
    root=tmp_path/'diagnostics';env=environment(root)
    foreign_crash=root/'crashes/session-foreign';foreign_crash.mkdir(parents=True);(foreign_crash/'pending').write_text('keep')
    process=subprocess.Popen([exe('CapLab'),'--crash-probe=wait'],cwd=tmp_path,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    try:
        deadline=time.monotonic()+10
        while not list(root.glob('crashes/session-*/pending')) and time.monotonic()<deadline:
            assert process.poll() is None
            time.sleep(.05)
        markers=list(root.glob('crashes/session-*/pending'));assert len(markers)==1
        report=tmp_path/'live.txt'
        result=subprocess.run([exe('CapLab'),'--export-diagnostics',report],cwd=tmp_path,env=env,capture_output=True,timeout=15)
        assert result.returncode==0
        assert 'Interrupted previous sessions: 0' in report.read_text(), 'A live process must not appear crashed'
        process.kill();process.wait(timeout=5)
        report=tmp_path/'restart.txt'
        result=subprocess.run([exe('CapLab'),'--export-diagnostics',report],cwd=tmp_path,env=env,capture_output=True,timeout=15)
        assert result.returncode==0
        assert 'Interrupted previous sessions: 1' in report.read_text()
        assert markers[0].exists()
        # Seed stale logger sessions; the next session prunes only diagnostics and leaves the crash evidence.
        sessions=root/'logs/sessions'
        foreign=sessions/'session-foreign';foreign.mkdir(parents=True);(foreign/'precious.txt').write_text('keep')
        for i in range(25):
            directory=sessions/f'session-old-{i:02d}';directory.mkdir(parents=True);(directory/'active').write_bytes(b'SST-LOG-v1\n')
        result=subprocess.run([exe('CapLab'),'--export-diagnostics',tmp_path/'retention.txt'],cwd=tmp_path,env=env,capture_output=True,timeout=15)
        assert result.returncode==0,result.stderr
        assert len([p for p in sessions.glob('session-*') if p!=foreign])<=20
        assert (foreign/'precious.txt').read_text()=='keep'
        assert markers[0].exists()
    finally:
        if process.poll() is None: process.kill();process.wait(timeout=5)

def test_python_surface_is_headless_and_uses_same_identity(tmp_path):
    script=tmp_path/'probe.py';report=tmp_path/'python-zażółć-图.txt'
    script.write_text('import json, sst_application\n'
        'info=sst_application.version_info()\n'
        'assert len(info["source_sha256"])==64\n'
        f'result=sst_application.export_diagnostics({str(report)!r})\n'
        'assert result["ok"], result\n'
        'bad=sst_application.export_diagnostics("bad\\x00name")\n'
        'assert not bad["ok"] and bad["error"]["code"]=="diagnostics.invalid_request", bad\n'
        f'open({str(tmp_path/"version.json")!r},"w").write(json.dumps(info))\n',encoding='utf-8')
    result=subprocess.run([exe('PlotLab'),'--worksheet-script',script],cwd=ROOT,env=environment(tmp_path/'diag'),capture_output=True,timeout=30)
    assert result.returncode==0,result.stderr
    info=json.loads((tmp_path/'version.json').read_text())
    assert info['source_sha256'] in report.read_text()
    assert info['source_sha256']==json.loads(exe('PlotLab').with_suffix('.build.json').read_text())['source_sha256']

def test_real_headless_python_abort_is_captured(tmp_path):
    script=tmp_path/'abort.py';script.write_text('import os\nos.abort()\n',encoding='utf-8')
    root=tmp_path/'diagnostics'
    result=subprocess.run([exe('PlotLab'),'--worksheet-script',script],cwd=ROOT,env=environment(root),capture_output=True,timeout=25)
    assert result.returncode!=0
    dumps=list(root.glob('crashes/session-*/crash.dmp'));assert len(dumps)==1
    assert dumps[0].read_bytes()[:4]==b'MDMP'
    assert dumps[0].with_name('pending').exists()

def test_build_digest_tracks_input_bytes_and_names(tmp_path):
    spec=importlib.util.spec_from_file_location('build_info',ROOT/'scripts/generate_build_info.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    source=tmp_path/'source';source.mkdir();file=source/'x.cpp';file.write_bytes(b'one')
    a=module.digest([('source',source)]);assert a==module.digest([('source',source)])
    file.write_bytes(b'two');b=module.digest([('source',source)]);assert a!=b
    file.rename(source/'y.cpp');assert b!=module.digest([('source',source)])
    for version in ['0.1.0','1.2.3-rc.1+build.17','1.0.0-0']:
        assert re.fullmatch(module.SEMVER,version)
    for version in ['01.2.3','1.02.3','1.2','1.2.3-01','1.2.3-a..b','1.2.3+']:
        assert not re.fullmatch(module.SEMVER,version)
    assert module.windows_version('1.2.3-rc.1')=='1,2,3,0'
    with pytest.raises(ValueError,match='65535'):module.windows_version('65536.0.0')

def test_build_generator_rejects_untrusted_revision(tmp_path,monkeypatch):
    spec=importlib.util.spec_from_file_location('build_info',ROOT/'scripts/generate_build_info.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    monkeypatch.setenv('SST_SOURCE_REVISION','path or secret\n')
    with pytest.raises(ValueError,match='SST_SOURCE_REVISION'):
        module.generate(tmp_path/'BuildInfo.generated.hpp')
