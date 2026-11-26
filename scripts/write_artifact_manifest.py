"""Record the exact EXE and private PDB hashes at the artifact file edge."""
import argparse
import hashlib
import json
from pathlib import Path

def sha256(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source,'sha256').hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--artifact',type=Path,required=True)
    parser.add_argument('--pdb',type=Path,required=True)
    parser.add_argument('--identity',type=Path,required=True)
    args=parser.parse_args()
    manifest=json.loads(args.identity.read_text())
    manifest['artifact']={'name':args.artifact.name,'sha256':sha256(args.artifact)}
    manifest['private_symbols']={'name':args.pdb.name,'sha256':sha256(args.pdb)}
    args.artifact.with_suffix('.build.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__': main()
