#!/usr/bin/env python3
"""Update the numbered builder draft; publishing remains strictly an owner action.

Keep updating alpha.N while it is a draft. Once published (including as a
prerelease), leave it alone and create alpha.N+1 on the next successful build.
CI never publishes: every created release has draft=True.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

NOTES_DIR = Path(__file__).with_name('notes')


class PublishedDuringUpdate(Exception):
    pass


class GitHub:
    def __init__(self, repo):
        self.repo = repo

    def api(self, path, *, method='GET', payload=None, paginate=False):
        command = ['gh', 'api', '--method', method]
        if paginate:
            command += ['--paginate', '--slurp']
        command.append(f'repos/{self.repo}/{path}')
        result = subprocess.run(
            command,
            input=json.dumps(payload) if payload is not None else None,
            capture_output=True,
            text=True,
            check=True
        )
        return json.loads(result.stdout) if result.stdout.strip() else None

    def releases(self):
        return [release for page in self.api('releases?per_page=100', paginate=True) for release in page]

    def require_draft(self, release_id):
        release = self.api(f'releases/{release_id}')
        if not release['draft']:
            raise PublishedDuringUpdate()
        return release

    def edit(self, release_id, payload):
        self.require_draft(release_id)
        # Never set draft=false: publication strictly belongs to the owner.
        return self.api(f'releases/{release_id}', method='PATCH', payload=payload)

    def create(self, payload):
        return self.api('releases', method='POST', payload={**payload, 'draft': True, 'prerelease': True})

    def upload(self, release_id, files):
        release = self.require_draft(release_id)
        subprocess.run(
            ['gh', 'release', 'upload', release['tag_name'], *map(str, files),
             '--repo', self.repo, '--clobber'],
            check=True
        )

    def delete_asset(self, release_id, asset_id):
        self.require_draft(release_id)
        self.api(f'releases/assets/{asset_id}', method='DELETE')


def select_release(releases, series='v0.1.0-alpha'):
    pattern = re.compile(re.escape(series) + r'\.([1-9][0-9]*)\Z')
    numbered = [
        (int(match[1]), release) for release in releases
        if (match := pattern.fullmatch(release['tag_name']))
    ]
    if numbered:
        number, latest = max(numbered, key=lambda item: item[0])
        if latest['draft']:
            return latest['tag_name'], latest
        number += 1
    else:
        number = 1
    return f'{series}.{number}', None


def package_for_version(source, version):
    suffix = next(
        (suffix for suffix in ('windows-x64.zip', 'linux-x86_64.AppImage')
         if source.name.endswith('-' + suffix)), None
    )
    if suffix is None:
        raise ValueError(f'unknown builder package: {source.name}')
    target = source.with_name(f'DW5E-{version}-{suffix}')
    if target != source:
        source.rename(target)
    checksum = Path(str(target) + '.sha256')
    h = hashlib.sha256()
    with target.open('rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    checksum.write_text(f'{h.hexdigest()}  {target.name}\n')
    return target, checksum


def release_body(version, commit, run_id, subject, platforms):
    metadata = (
        f"Built by GitHub Actions from {commit} (run {run_id}) after clean-room verification and tests.\n\n"
        f"Standalone self-building {platforms} release packages: launcher, compiler, sources and tools.\n"
        "THIS DOWNLOAD CONTAINS NOTHING FROM THE GAME: on the first start it builds the game from your own disc.\n\n"
        "You need: Dynasty Warriors 5: Empires, European PAL (Title ID: 4B4F07D3), as a disc image (.iso).\n\n"
        f"Latest commit: {subject.splitlines()[0] if subject else ''}\n\n"
        "Draft packages are updated by CI until published by the owner."
    )
    notes = NOTES_DIR / f'{version}.md'
    return notes.read_text().rstrip() + '\n\n---\n\n' + metadata if notes.is_file() else metadata


def update_draft(github, source, commit, run_id, subject, series='v0.1.0-alpha', tag_override=None):
    sources = [source] if isinstance(source, Path) else list(source)
    platforms = 'Windows and Linux' if len(sources) > 1 else ('Windows' if 'windows' in sources[0].name else 'Linux')
    for _ in range(3):
        if tag_override:
            version = tag_override
            existing = [r for r in github.releases() if r['tag_name'] == version]
            release = existing[0] if existing else None
        else:
            version, release = select_release(github.releases(), series)

        payload = {
            'tag_name': version,
            'target_commitish': commit,
            'name': f'DW5E {version}',
            'body': release_body(version, commit, run_id, subject, platforms),
            'draft': True,
            'prerelease': True,
        }
        try:
            if release is None:
                release = github.create(payload)
            else:
                release = github.edit(release['id'], payload)

            packages = [package_for_version(s, version) for s in sources]
            sources = [pkg[0] for pkg in packages]
            files = [path for pkg in packages for path in pkg]
            github.upload(release['id'], files)

            keep = {path.name for path in files}
            for asset in github.require_draft(release['id']).get('assets', []):
                if asset['name'].startswith('DW5E-') and asset['name'] not in keep:
                    github.delete_asset(release['id'], asset['id'])
            print(f'Updated draft {version} from {commit}')
            return version
        except PublishedDuringUpdate:
            if tag_override:
                raise RuntimeError(f'Release tag {tag_override} is already published; refusing to overwrite.')
            print('Owner published the selected draft; selecting the next version.')
    raise RuntimeError('Release changed repeatedly during upload; rerun the draft job.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dist', type=Path, default=Path('dist'))
    parser.add_argument('--repo', default=os.environ.get('GH_REPO'))
    parser.add_argument('--commit', default=os.environ.get('GITHUB_SHA'))
    parser.add_argument('--run-id', default=os.environ.get('GITHUB_RUN_ID', 'local'))
    parser.add_argument('--series', default='v0.1.0-alpha')
    parser.add_argument('--tag', default=os.environ.get('RELEASE_TAG'))
    args = parser.parse_args()

    windows = list(args.dist.glob('DW5E-*-windows-x64.zip'))
    linux = list(args.dist.glob('DW5E-*-linux-x86_64.AppImage'))
    if not args.repo or not args.commit:
        parser.error('repo and commit are required')
    if not windows and not linux:
        parser.error('at least one package (Windows zip or Linux AppImage) is required in dist/')

    update_draft(
        GitHub(args.repo),
        windows + linux,
        args.commit,
        args.run_id,
        os.environ.get('COMMIT_MESSAGE', ''),
        series=args.series,
        tag_override=args.tag
    )


if __name__ == '__main__':
    main()
