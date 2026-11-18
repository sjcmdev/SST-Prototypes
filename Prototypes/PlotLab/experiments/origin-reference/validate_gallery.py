"""Compile and run all tests for this experiment; all writes stay here."""
from pathlib import Path
import os
import py_compile
import sys
import unittest

root=Path(__file__).resolve().parent
build=root/'build'
build.mkdir(exist_ok=True)
sys.pycache_prefix=str(build/'pycache')
os.environ['TMP']=os.environ['TEMP']=str(build)
for source in root.glob('*.py'):
    py_compile.compile(str(source),cfile=str(build/(source.stem+'.pyc')),doraise=True)
print('BUILD:',build,flush=True)
sys.path.insert(0,str(root))
result=unittest.TextTestRunner(stream=sys.stdout,verbosity=2).run(unittest.defaultTestLoader.discover(str(root),pattern='test_*.py'))
raise SystemExit(not result.wasSuccessful())
