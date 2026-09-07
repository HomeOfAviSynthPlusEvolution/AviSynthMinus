"""Assemble verified local CI artifacts; never install, upload, or rebuild cores.

Python's archive APIs preserve POSIX modes and symbolic links on Windows hosts.
The caller must first verify the selected run/jobs/tag and build the dual-architecture installer.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import subprocess
import tarfile
import zipfile


def sha(data):
    return hashlib.sha256(data).hexdigest()


def command(*args):
    return subprocess.check_output(args).decode('utf-8', errors='replace')


def blob(text):
    return text.encode('utf-8')


def json_blob(value):
    return blob(json.dumps(value, indent=2, ensure_ascii=False) + '\n')


def tar_entry(archive, name, data, mode=0o644, link=None):
    item = tarfile.TarInfo(name)
    item.mode = mode
    if link:
        item.type = tarfile.SYMTYPE
        item.linkname = link
    else:
        item.size = len(data)
    archive.addfile(item, None if link else io.BytesIO(data))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--repo', type=Path, default=Path.cwd())
    parser.add_argument('--llvm-dir', type=Path, required=True)
    parser.add_argument('--installer-dir', type=Path)
    args = parser.parse_args()
    root, repo = args.root.resolve(), args.repo.resolve()
    source = json.loads((root / 'download/release-source/release-source.json').read_text())
    tag, commit, run = source['tag'], source['commit'], source['run_id']
    version = tag.removeprefix('minus-v')
    assert re.fullmatch(r'\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?', version)
    assert command('git', '-C', str(repo), 'rev-parse', 'HEAD').strip() == commit
    assert not command('git', '-C', str(repo), 'status', '--porcelain', '--untracked-files=no').strip()
    output = root / 'upload'
    output.mkdir(exist_ok=False)
    readobj = str(args.llvm_dir / 'llvm-readobj.exe')
    readelf = str(args.llvm_dir / 'llvm-readelf.exe')
    objdump = str(args.llvm_dir / 'llvm-objdump.exe')
    run_url = f'https://github.com/HomeOfAviSynthPlusEvolution/AviSynthMinus/actions/runs/{run}'
    provenance = dict(source, repository='HomeOfAviSynthPlusEvolution/AviSynthMinus', run_url=run_url,
                      scope='Core-only; no SDK, standard plugins, runtime redistributables or platform installation tests')
    licenses = {
        'licenses/GPL.txt': (repo / 'distrib/gpl.txt').read_bytes(),
        'licenses/LGPL-used-libraries.txt': (repo / 'distrib/lgpl_for_used_libs.txt').read_bytes(),
        'licenses/filesystem-MIT.txt': (repo / 'filesystem/LICENSE').read_bytes(),
    }
    platforms = [
        ('windows', 'x86', 'avisynth-windows-vs2026-x86', 'pe', 0x14c),
        ('windows', 'x64', 'avisynth-windows-vs2026-x64', 'pe', 0x8664),
        ('windows', 'arm64', 'avisynth-windows-vs2026-arm64', 'pe', 0xaa64),
        ('windows-xp', 'x86', 'avisynth-windows-xp-v141-xp-x86', 'pe', 0x14c),
        ('windows-xp', 'x64', 'avisynth-windows-xp-v141-xp-x64', 'pe', 0x8664),
        ('ubuntu-26.04', 'x64', 'avisynth-ubuntu-26.04-x64', 'elf', 62),
        ('ubuntu-26.04', 'arm64', 'avisynth-ubuntu-26.04-arm64', 'elf', 183),
        ('macOS-15.0+', 'x64', 'avisynth-macos-15-x64', 'macho', 0x1000007),
        ('macOS-15.0+', 'arm64', 'avisynth-macos-15-arm64', 'macho', 0x100000c),
    ]
    inventory = []
    records = root / 'records-compact'
    records.mkdir(exist_ok=False)
    windows_files = {n: (data, 0o644, None) for n, data in licenses.items()}
    other_files = dict(windows_files)
    binary_records = []
    for platform, arch, artifact, fmt, machine in platforms:
        artifact_dir = root / 'download' / artifact
        binaries = {p.relative_to(artifact_dir).as_posix(): p.read_bytes()
                    for p in sorted(artifact_dir.rglob('*')) if p.is_file()}
        assert binaries, artifact
        canonical = next((n for n in binaries if '3.7.5' in n), next(iter(binaries)))
        path = artifact_dir / canonical
        binary = binaries[canonical]
        if fmt == 'pe':
            offset = struct.unpack_from('<I', binary, 0x3c)[0]
            assert binary[offset:offset+4] == b'PE\0\0'
            assert struct.unpack_from('<H', binary, offset+4)[0] == machine
            assert f'AviSynth- {version} ('.encode('utf-16le') in binary
            inspection = command(readobj, '--file-headers', '--coff-imports', str(path))
            dependencies = re.findall(r'^  Name: (.+)$', inspection, re.M)
        elif fmt == 'elf':
            assert binary[:6] == b'\x7fELF\x02\x01'
            assert struct.unpack_from('<H', binary, 18)[0] == machine
            assert f'AviSynth- {version} ('.encode() in binary
            inspection = command(readobj, '--file-headers', '--needed-libs', str(path))
            versions = command(readelf, '--version-info', str(path))
            dependencies = re.findall(r'^  (\S+\.so[^\s]*)$', inspection, re.M)
            symbols = {}
            for prefix in ('GLIBC', 'GLIBCXX', 'CXXABI'):
                found = re.findall(r'Name: ' + prefix + r'_(\d+(?:\.\d+)+)', versions)
                symbols[prefix] = max(found, key=lambda v: tuple(map(int, v.split('.'))))
            inspection += '\n' + versions
        else:
            assert binary[:4] == b'\xcf\xfa\xed\xfe'
            assert struct.unpack_from('<I', binary, 4)[0] == machine
            assert f'AviSynth- {version} ('.encode() in binary
            inspection = command(objdump, '--macho', '--private-headers', str(path))
            minimum = re.search(r'\bminos (\S+)', inspection).group(1)
            assert minimum == '15.0'
            dependencies = re.findall(r'^\s+name (/usr/lib/\S+) \(offset', inspection, re.M)
        binary_records.append(dict(provenance, platform=platform, architecture=arch, artifact=artifact,
                                   files={n: sha(b) for n, b in binaries.items()}, dependencies=dependencies,
                                   inspection=inspection))
        mainstream = platform == 'windows' and arch in ('x86', 'x64')
        destination = windows_files if mainstream else other_files
        folder = arch if mainstream else f'{platform}/{arch}'
        if fmt != 'pe':
            assert len(binaries) == 3 and len({sha(b) for b in binaries.values()}) == 1
        for filename, data in binaries.items():
            alias = Path(canonical).name if fmt != 'pe' and filename != canonical else None
            destination[f'{folder}/{filename}'] = (data, 0o755 if fmt != 'pe' else 0o644, alias)

    for name, files, is_zip in (
        (f'AviSynthMinus_{version}_windows-filesonly', windows_files, True),
        (f'AviSynthMinus_{version}_other-platforms-filesonly', other_files, False),
    ):
        dest = output / (name + ('.zip' if is_zip else '.tar.xz'))
        if is_zip:
            with zipfile.ZipFile(dest, 'x', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
                for filename, (data, mode, link) in files.items():
                    assert link is None
                    archive.writestr(name + '/' + filename, data)
            with zipfile.ZipFile(dest) as archive:
                assert archive.testzip() is None
                for filename, (data, _, _) in files.items():
                    assert archive.read(name + '/' + filename) == data
        else:
            with tarfile.open(dest, 'x:xz', preset=6) as archive:
                for filename, (data, mode, link) in files.items():
                    tar_entry(archive, name + '/' + filename, data, mode, link)
            with tarfile.open(dest) as archive:
                for filename, (data, mode, link) in files.items():
                    member = archive.getmember(name + '/' + filename)
                    assert member.mode == mode
                    assert member.issym() == (link is not None)
                    assert archive.extractfile(member).read() == data
        inventory.append({'file': dest.name, 'sha256': sha(dest.read_bytes())})
        print('Verified', dest.name, flush=True)

    installer_name = f'AviSynthMinus_{version}_windows_x86-x64-mod.exe'
    installer_dir = args.installer_dir or root / 'installer'
    installer = installer_dir / installer_name
    meta_path = installer_dir / (installer_name + '.json')
    meta = json.loads(meta_path.read_text())
    assert meta['Version'] == version and meta['SourceCommit'] == commit and meta['ActionsRunId'] == run
    data = installer.read_bytes()
    assert sha(data) == meta['InstallerSHA256']
    assert meta['PayloadSHA256'] == sha((root / 'download/avisynth-windows-vs2026-x64/bin/AviSynth.dll').read_bytes())
    assert meta['PayloadX86SHA256'] == sha((root / 'download/avisynth-windows-vs2026-x86/bin/AviSynth.dll').read_bytes())
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    assert struct.unpack_from('<H', data, pe + 4)[0] == 0x8664
    (output / installer_name).write_bytes(data)
    inventory.append({'file': installer_name, 'sha256': sha(data), 'installer_provenance': meta})
    # Provenance and diagnostics stay local, never in user-facing archives.
    (records / 'BUILD-MANIFEST.json').write_bytes(json_blob(dict(provenance, packages=inventory, binaries=binary_records)))
    sums = ''.join(f'{sha(p.read_bytes())}  {p.name}\n' for p in sorted(output.iterdir()))
    (output / 'SHA256SUMS.txt').write_text(sums, encoding='ascii')
    print('Upload directory:', output, flush=True)


if __name__ == '__main__':
    main()
