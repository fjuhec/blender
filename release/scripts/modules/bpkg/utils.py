from pathlib import Path
import shutil
import logging

def fmt_version(version_number: tuple) -> str:
    """Take version number as a tuple and format it as a string"""
    vstr = str(version_number[0])
    for component in version_number[1:]:
        vstr += "." + str(component)
    return vstr

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
    from .types import Repository
    for repofile in repo_storage_path.glob('*.json'):
        # try
        repo = Repository.from_file(repofile)
        # except
        repositories.append(repo)
    return repositories

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
        print("removing")
        rm(self.backup_path)


def add_repojson_to_url(url: str) -> str:
    """Add `repo.json` to the path component of a url"""
    from urllib.parse import urlsplit, urlunsplit
    parsed_url = urlsplit(url)
    new_path = parsed_url.path + "/repo.json"
    return urlunsplit((parsed_url.scheme, parsed_url.netloc, new_path, parsed_url.query, parsed_url.fragment))
