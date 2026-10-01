"""Prepare a separate test package; never installs into the client's directory."""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('client', type=Path)
    parser.add_argument('--archive', type=Path, help='Explicit diagnostic DBC source; default is installed patch-M.MPQ')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    tools = root / 'tools/EnvironmentDatabaseBuilder'
    research = root / 'build/client-research'
    dll = root / 'build/Release/d3d9.dll'
    if not dll.is_file():
        raise ValueError('Build Release Win32 first')
    subprocess.run([sys.executable, str(tools/'inspect_client.py'), str(args.client/'Ascension.exe'), '--output', str(research)], check=True)
    inspection = json.loads((research/'client-inspection.json').read_text())
    if 'candidateRvas' not in inspection:
        raise ValueError('No researched diagnostic offsets for this executable; generic safe DLL remains available in build/Release')
    archive = args.archive or args.client/'Data/patch-M.MPQ'
    subprocess.run([sys.executable, str(tools/'build_database.py'), str(args.client), '--diagnostic-archive', str(archive), '--output', str(research/'areas.txt')], check=True)
    destination = root / 'build/Ascension-environment-diagnostic'
    (destination/'data').mkdir(parents=True,exist_ok=True)
    shutil.copy2(dll,destination/'d3d9.dll')
    for name in ('areas.txt','areas.metadata.json','areas.demo.ini'):
        shutil.copy2(research/name,destination/'data'/name)
    settings = (root/'EnvironmentProfiles.ini').read_text(encoding='utf-8')
    settings = settings.replace('ExeSHA256=\n',f'ExeSHA256={inspection["sha256"]}\n')
    for key,rva in zip(('MapRva','ZoneRva','AreaRva'),('map','zone','area')):
        settings = settings.replace(f'{key}=0\n',f'{key}={inspection["candidateRvas"][rva]:#x}\n')
    settings += '\n; Diagnostic mappings from this archive; artistic values are provisional.\n' + (research/'areas.demo.ini').read_text()
    (destination/'EnvironmentProfiles.ini').write_text(settings,encoding='utf-8')
    shutil.copy2(root/'docs/ENVIRONMENT-SYSTEM-RU.md',destination/'REPORT-RU.md')
    manifest = dict(executableSHA256=inspection['sha256'],dllSHA256=hashlib.sha256(dll.read_bytes()).hexdigest(),
                    verifiedInWorld=False,databaseProvenance='diagnostic-only',installedIntoClient=False)
    (destination/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    (destination/'README.txt').write_text(
        'DIAGNOSTIC BUILD ONLY\n'
        'Prepared separately; the installed client has not been modified.\n'
        'Location RVAs are exact-hash offline candidates, not in-world verified.\n'
        'The database is from one explicitly selected custom archive, not verified loader priority.\n'
        'Automatic environment changes are locked by both verification gates.\n'
        'For a future manual test: close the game, back up its d3d9.dll and any existing EnvironmentProfiles.ini/data/areas.txt, then copy the package DLL, EnvironmentProfiles.ini and data folder next to Ascension.exe.\n'
        'Keep the existing GraphicsEffects.ini and ModernWoWRenderer.ini.\n'
        'F7 shows location, validation and base settings; All settings are visible in three columns without scrolling or pages. F12 reloads both configurations.\n'
        'Do not mark verification complete until the checks in REPORT-RU.md have actually passed.\n',encoding='utf-8')
    zip_path = shutil.make_archive(str(destination),'zip',destination)
    print(f'Prepared {destination}\nArchive: {zip_path}\nClient installation unchanged.')


if __name__ == '__main__':
    main()
