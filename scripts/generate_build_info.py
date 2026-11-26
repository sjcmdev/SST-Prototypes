"""Generate release identity without invoking git. SST_SOURCE_REVISION is supplied by the owner/CI."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[1]
PRERELEASE = r'(?:0|[1-9]\d*|\d*[A-Za-z-][0-9A-Za-z-]*)'
BUILD = r'[0-9A-Za-z-]+'
SEMVER = rf'(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-{PRERELEASE}(?:\.{PRERELEASE})*)?(?:\+{BUILD}(?:\.{BUILD})*)?'

def windows_version(version):
    parts=[int(p) for p in re.match(r'\d+\.\d+\.\d+',version).group().split('.')]
    if any(p>65535 for p in parts): raise ValueError('Windows VERSIONINFO requires version components <= 65535')
    return ','.join(map(str,[*parts,0]))

def digest(roots):
    result = hashlib.sha256()
    for label, root in roots:
        files = sorted(root.rglob('*')) if root.is_dir() else [root]
        for file in files:
            if not file.is_file() or any(p in {'.git', 'build', '__pycache__', '.venv'} for p in file.relative_to(root.parent).parts):
                continue
            result.update((label + '/' + file.relative_to(root.parent).as_posix()).encode())
            result.update(b'\0')
            with file.open('rb') as source:
                result.update(hashlib.file_digest(source,'sha256').digest())
    return result.hexdigest()

def generate(output):
    version = tomllib.loads((ROOT/'pyproject.toml').read_text())['project']['version']
    if not re.fullmatch(SEMVER, version):
        raise ValueError('pyproject.toml project.version must be SemVer')
    numeric_version=windows_version(version)
    revision = os.environ.get('SST_SOURCE_REVISION', 'unknown')
    if not re.fullmatch(r'[A-Za-z0-9._+-]{1,128}', revision):
        raise ValueError('SST_SOURCE_REVISION must be a source ID without whitespace')
    date = datetime.datetime.fromtimestamp(int(os.environ['SOURCE_DATE_EPOCH']), datetime.timezone.utc) if 'SOURCE_DATE_EPOCH' in os.environ else datetime.datetime.now(datetime.timezone.utc)
    apps = sorted(p for p in (ROOT/'Prototypes').iterdir() if (p/'premake5.lua').is_file())
    source = digest([(p.name, p) for p in [ROOT/'Core', ROOT/'PrototypeTemplate', ROOT/'premake5.lua', ROOT/'scripts/generate_build_info.py', ROOT/'scripts/write_artifact_manifest.py', *apps]]
                    + [('SST-Libraries/'+p.name,p) for p in [ROOT.parent/'SST-Libraries/premake5.lua',ROOT.parent/'SST-Libraries/sst_libraries.lua']]
                    + [(p.name, p) for p in (ROOT.parent/'SST-Libraries').iterdir() if p.is_dir() and p.name.startswith(('SST-', 'UI-', 'PlotLab-'))])
    dependencies = [(p.name, p) for base in [ROOT/'Vendor', ROOT.parent/'SST-Libraries/Vendor'] for p in sorted(base.iterdir()) if p.name != 'Binaries']
    hashes = '\n'.join(name + '=' + digest([(name, path)]) for name, path in dependencies)
    hashes += '\nuv.lock=' + digest([('uv.lock', ROOT/'uv.lock')])
    # Match CapLab's existing source dependency resolution; these roots are read only.
    for name,variable,local,fallback in [
        ('CircuitCore','SST_CIRCUITCORE_ROOT',ROOT.parent/'CircuitCore',ROOT/'../../../CircuitCore'),
        ('CircuitCore-UI','SST_CIRCUIT_UI_ROOT',ROOT.parent/'SST-Experiments/CircuitCore-UI/src',ROOT/'../../../SST-Experiments/CircuitCore-UI/src')]:
        configured=os.environ.get(variable,str(local))
        path=Path(configured) if configured else fallback
        if not path.is_dir(): path=fallback
        if path.is_dir():
            roots=[(name,path/'include'),(name,path/'src')] if name=='CircuitCore' else [(name,path)]
            hashes += '\n'+name+'='+digest(roots)
    python_dll=Path(os.environ.get('SST_PYTHON_DLL_PATH',str(Path(sys.base_prefix)/f'python{sys.version_info.major}{sys.version_info.minor}.dll')))
    if python_dll.is_file(): hashes+='\nCPython-runtime='+digest([('CPython-runtime',python_dll)])
    values = [version, revision, date.strftime('%Y-%m-%dT%H:%M:%SZ'), source, hashes]
    text = '#pragma once\nnamespace SST { inline constexpr BuildInfo GeneratedBuildInfo{\n' + ',\n'.join(json.dumps(v) for v in values) + '\n}; }\n'
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text() != text:
        output.write_text(text, encoding='utf-8')
    identity = dict(version=version,commit=revision,build_utc=values[2],source_sha256=source,dependency_sha256=dict(line.split('=',1) for line in hashes.splitlines()))
    output.with_suffix('.json').write_text(json.dumps(identity,indent=2)+'\n',encoding='utf-8')
    resource=f'''#include <winver.h>
1 VERSIONINFO
 FILEVERSION {numeric_version}
 PRODUCTVERSION {numeric_version}
 FILEFLAGSMASK 0x3fL
 FILEFLAGS {8 if revision=='unknown' else 0}
 FILEOS 0x40004L
 FILETYPE 0x1L
 FILESUBTYPE 0
BEGIN
 BLOCK "StringFileInfo"
 BEGIN
  BLOCK "040904b0"
  BEGIN
   VALUE "FileDescription", "Semiconductor Studio\\0"
   VALUE "FileVersion", "{version}\\0"
   VALUE "ProductName", "Semiconductor Studio\\0"
   VALUE "ProductVersion", "{version}\\0"
   VALUE "PrivateBuild", "{revision}\\0"
   VALUE "Comments", "UTC={values[2]} SourceSHA256={source}\\0"
  END
 END
 BLOCK "VarFileInfo"
 BEGIN
  VALUE "Translation", 0x409, 1200
 END
END
'''
    output.with_suffix('.rc').write_text(resource,encoding='utf-8')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    generate(parser.parse_args().output)
