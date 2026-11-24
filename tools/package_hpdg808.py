"""Assemble the private website handoff for HPDG 808; does not upload anything.

python tools/package_hpdg808.py  ->  Releases/HPDG808_<version>_Site_Package_EN(.zip)
Needs the installer from Installer/Build-HPDG808.ps1 (the version is read from CMakeLists.txt).
Fills the installer's size and SHA-256 into docs/hpdg808/SITE_AGENT.md and release.json.
"""
from pathlib import Path
import hashlib, json, re, shutil, zipfile

root = Path(__file__).resolve().parents[1]
version = re.search(r'set\(HPDG808_VERSION (\d+\.\d+\.\d+)\)', (root / 'CMakeLists.txt').read_text(encoding='utf-8')).group(1)
doc = root / 'docs/hpdg808'
installer = root / f'Releases/HPDG808_Setup_{version}.exe'
dest = root / f'Releases/HPDG808_{version}_Site_Package_EN'
assert installer.exists(), installer
if dest.exists():
    shutil.rmtree(dest)
(dest / 'images').mkdir(parents=True)
(dest / 'site').mkdir()


def sha(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


digest, size = sha(installer), installer.stat().st_size

# SITE_AGENT.md: the installer facts (placeholders on the first run, the previous values afterwards)
agent = (doc / 'SITE_AGENT.md').read_text(encoding='utf-8')
agent = re.sub(r'Check: size (@SIZE@|\d+) bytes, SHA-256\n   `(@SHA@|[0-9a-f]{64})`',
               f'Check: size {size} bytes, SHA-256\n   `{digest}`', agent)
assert str(size) in agent and digest in agent, 'SITE_AGENT.md: installer check line not found'
(doc / 'SITE_AGENT.md').write_text(agent, encoding='utf-8')

notes = ('HPDG 808 ' + version + ': Sound Library with search, filters and favorites; 35 section presets that change '
         'one tab and keep the rest; OSC B harmonics for small speakers; Draw Wave; LOAD REF + AUTO DRAW rebuilds a '
         'reference 808 as editable wave frames; SYNTH preview with PLAY and scrub; 12 new Trap / Drill voicings '
         '(212 presets). Projects and presets from 1.0 sound exactly as before.')
assert len(notes) <= 600, len(notes)
release = {'product': 'hpdg808', 'version': version, 'title': f'HPDG 808 {version}', 'notes': notes,
           'page': 'https://hamloprod.org/en/vst/hpdg808', 'objectKey': f'hpdg808/{installer.name}',
           'sha256': digest, 'size': size}
(doc / 'release.json').write_text(json.dumps(release, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

shutil.copy2(installer, dest / installer.name)
(dest / (installer.name + '.sha256')).write_text(f'{digest}  {installer.name}\n', encoding='ascii')
for name in ['SITE_AGENT.md', 'SITE_PAGE.md', 'RELEASE.txt', 'GUIDE_EN.html', 'release.json']:
    shutil.copy2(doc / name, dest / name)
for image in sorted((doc / 'images').glob('*.png')):
    shutil.copy2(image, dest / 'images' / image.name)
shutil.copy2(doc / 'site/index.html', dest / 'site/index.html')

# English only; customer-facing text without internal names, brands or measurement details
banned = ['Soviet', 'Lomo', 'repository', 'GitHub', 'Astra', 'Snapshot', 'audit', 'Splice', 'YKSOUNDS', 'COLORS',
          '808_TALK', 'Metro', 'DABRO', 'Dilla']
for name in ['SITE_AGENT.md', 'SITE_PAGE.md', 'RELEASE.txt', 'GUIDE_EN.html', 'release.json', 'site/index.html']:
    text = (dest / name).read_text(encoding='utf-8-sig')
    assert not re.search(r'[Ѐ-ӿ]', text), name
    if name != 'SITE_AGENT.md':  # the handoff itself lists what must not be published
        for phrase in banned:
            assert phrase.lower() not in text.lower(), (name, phrase)
for term in [version, '212', 'OSC B', 'AUTO DRAW', 'Sound Library']:
    assert term in (dest / 'RELEASE.txt').read_text(encoding='utf-8'), term
for name in ['hpdg808_library.png', 'hpdg808_autodraw.png', 'hpdg808_synth.png']:
    assert (dest / 'images' / name).exists(), name

files = sorted(f for f in dest.rglob('*') if f.is_file() and f.name != 'SHA256SUMS.txt')
sums = ''.join(f'{sha(f)}  {f.relative_to(dest).as_posix()}\n' for f in files)
(dest / 'SHA256SUMS.txt').write_text(sums, encoding='ascii')
(doc / 'SHA256SUMS.txt').write_text(sums, encoding='ascii')
archive = root / f'Releases/HPDG808_{version}_Site_Package_EN.zip'
with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=1) as z:
    for file in sorted(dest.rglob('*')):
        if file.is_file():
            z.write(file, dest.name + '/' + file.relative_to(dest).as_posix())
print(f'HPDG 808 {version}: installer {size} bytes, SHA256 {digest}')
print(archive)
