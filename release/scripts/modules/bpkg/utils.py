from pathlib import Path
from . import exceptions
from .types import (
    Repository,
)
import shutil
import logging


def format_filename(s: str, ext=None) -> str:
    """Take a string and turn it into a reasonable filename"""
    import string
    if ext is None:
        ext = ""
    valid_chars = "-_.() %s%s" % (string.ascii_letters, string.digits)
    filename = ''.join(char for char in s if char in valid_chars)
    filename = filename.replace(' ','_')
    filename.lower()
    filename += ext
    return filename

def sanitize_repository_url(url: str) -> str:
    """Sanitize repository url"""
    from urllib.parse import urlsplit, urlunsplit
    parsed_url = urlsplit(url)
    # new_path = parsed_url.path.rstrip("repo.json")
    new_path = parsed_url.path
    return urlunsplit((parsed_url.scheme, parsed_url.netloc, new_path, parsed_url.query, parsed_url.fragment))

def add_repojson_to_url(url: str) -> str:
    """Add `repo.json` to the path component of a url"""
    from urllib.parse import urlsplit, urlunsplit
    parsed_url = urlsplit(url)
    new_path = str(Path(parsed_url.path) / "repo.json")
    return urlunsplit((parsed_url.scheme, parsed_url.netloc, new_path, parsed_url.query, parsed_url.fragment))

def load_repositories(repo_storage_path: Path) -> list:
    repositories = []
    for repofile in repo_storage_path.glob('*.json'):
        # try
        repo = Repository.from_file(repofile)
        # except
        repositories.append(repo)
    return repositories

def download(url: str, destination: Path, progress_callback=None) -> Path:
    """
    Downloads file at the given url, and if progress_callback is specified,
    repeatedly calls progress_callback with an argument between 0 and 1, or infinity.
    Raises DownloadException if an error occurs with the download.

    :returns: path to the downloaded file, or None if not modified
    """

    import requests
    log = logging.getLogger('%s.download' % __name__)

    if progress_callback is None:
        # assign to do-nothing function
        progress_callback = lambda x: None

    progress_callback(0)

    # derive filename from url if `destination` is an existing directory, otherwise use `destination` directly
    if destination.is_dir():
        # TODO: get filename from Content-Disposition header, if available.
        from urllib.parse import urlsplit, urlunsplit
        parsed_url = urlsplit(url)
        local_filename = Path(parsed_url.path).name or 'download.tmp'
        local_fpath = destination / local_filename
    else:
        local_fpath = destination

    log.info('Downloading %s -> %s', url, local_fpath)

    # try:
    resp = requests.get(url, stream=True, verify=True)
    # except requests.exceptions.RequestException as err:
    #     raise exceptions.DownloadException(err) from err

    try:
        resp.raise_for_status()
    except requests.HTTPError as err:
        raise exceptions.DownloadException(resp.status_code, str(err)) from err

    if resp.status_code == requests.codes.not_modified:
        log.info("Server responded 'Not Modified', not downloading")
        return None

    try:
        # Use float so that we can also use infinity
        content_length = float(resp.headers['content-length'])
    except KeyError:
        log.warning('Server did not send content length, cannot report progress.')
        content_length = float('inf')

    # TODO: check if there's enough disk space.


    downloaded_length = 0
    with local_fpath.open('wb') as outfile:
        for chunk in resp.iter_content(chunk_size=1024 ** 2):
            if not chunk:  # filter out keep-alive new chunks
                continue

            outfile.write(chunk)
            downloaded_length += len(chunk)
            progress_callback(downloaded_length / content_length)

    return local_fpath


def rm(path: Path):
    """Delete whatever is specified by `path`"""
    if path.is_dir():
        shutil.rmtree(str(path))
    else:
        path.unlink()

class InplaceBackup:
    """Utility class for moving a file out of the way by appending a '~'"""

    log = logging.getLogger('%s.inplace-backup' % __name__)

    def __init__(self, path: Path):
        self.path = path
        self.backup()

    def backup(self):
        """Move 'path' to 'path~'"""
        if not self.path.exists():
            raise FileNotFoundError("Can't backup path which doesn't exist")

        self.backup_path = Path(str(self.path) + '~')
        if self.backup_path.exists():
            self.log.warning("Overwriting existing backup '{}'".format(self.backup_path))
            rm(self.backup_path)

        shutil.move(str(self.path), str(self.backup_path))

    def restore(self):
        """Move 'path~' to 'path'"""
        try:
            getattr(self, 'backup_path')
        except AttributeError as err:
            raise RuntimeError("Can't restore file before backing it up") from err

        if not self.backup_path.exists():
            raise FileNotFoundError("Can't restore backup which doesn't exist")

        if self.path.exists():
            self.log.warning("Overwriting '{0}' with backup file".format(self.path))
            rm(self.path)

        shutil.move(str(self.backup_path), str(self.path))

    def remove(self):
        """Remove 'path~'"""
        rm(self.backup_path)


def install(src_file: Path, dest_dir: Path):
    """Extracts/moves package at `src_file` to `dest_dir`"""

    import zipfile

    log = logging.getLogger('%s.install' % __name__)
    log.debug("Starting installation")

    if not src_file.is_file():
        raise exceptions.InstallException("Package isn't a file")

    if not dest_dir.is_dir():
        raise exceptions.InstallException("Destination is not a directory")

    # TODO: check to make sure addon/package isn't already installed elsewhere

    # The following is adapted from `addon_install` in bl_operators/wm.py

    # check to see if the file is in compressed format (.zip)
    if zipfile.is_zipfile(str(src_file)):
        log.debug("Package is zipfile")
        try:
            file_to_extract = zipfile.ZipFile(str(src_file), 'r')
        except Exception as err:
            raise exceptions.InstallException("Failed to read zip file: %s" % err) from err

        def root_files(filelist: list) -> list:
            """Some string parsing to get a list of the root contents of a zip from its namelist"""
            rootlist = []
            for f in filelist:
                # Get all names which have no path separators (root level files)
                # or have a single path separator at the end (root level directories).
                if len(f.rstrip('/').split('/')) == 1:
                    rootlist.append(f)
            return rootlist

        conflicts = [dest_dir / f for f in root_files(file_to_extract.namelist()) if (dest_dir / f).exists()]
        backups = []
        for conflict in conflicts:
            log.debug("Creating backup of conflict %s", conflict)
            backups.append(InplaceBackup(conflict))

        try:
            file_to_extract.extractall(str(dest_dir))
        except Exception as err:
            for backup in backups:
                backup.restore()
            raise exceptions.InstallException("Failed to extract zip file to '%s': %s" % (dest_dir, err)) from err

        for backup in backups:
            backup.remove()

    else:
        log.debug("Package is pyfile")
        dest_file = (dest_dir / src_file.name)

        if dest_file.exists():
            backup = InplaceBackup(dest_file)

        try:
            shutil.copyfile(str(src_file), str(dest_file))
        except Exception as err:
            backup.restore()
            raise exceptions.InstallException("Failed to copy file to '%s': %s" % (dest_dir, err)) from err

    log.debug("Installation succeeded")


# def load_repository(repo_storage_path: Path, repo_name: str) -> Repository:
#     """Loads <repo_name>.json from <repo_storage_path>"""
#     pass
#
# def download_repository(repo_storage_path: Path, repo_name: str):
#     """Loads <repo_name>.json from <repo_storage_path>"""
#     pass
# this is done in Repository


def add_repojson_to_url(url: str) -> str:
    """Add `repo.json` to the path component of a url"""
    from urllib.parse import urlsplit, urlunsplit
    parsed_url = urlsplit(url)
    new_path = parsed_url.path + "/repo.json"
    return urlunsplit((parsed_url.scheme, parsed_url.netloc, new_path, parsed_url.query, parsed_url.fragment))
