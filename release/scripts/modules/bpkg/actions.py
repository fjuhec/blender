from pathlib import Path
from . import exceptions
from . import utils
import shutil
import logging

def download(url: str, destination: Path, progress_callback=None) -> Path:
    """
    Downloads file at the given url, and if progress_callback is specified,
    repeatedly calls progress_callback with an argument between 0 and 1, or
    infinity if progress cannot be determined.  Raises DownloadException if an
    error occurs with the download.

    :returns: path to the downloaded file, or None if not modified
    """

    import requests
    log = logging.getLogger('%s.download' % __name__)

    if progress_callback is None:
        # assign to do-nothing function
        progress_callback = lambda x: None

    progress_callback(0)

    log.info('Downloading %s ', url)

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

    # determine destination filename from url, but only after we've determined it works as a real url
    # derive filename from url if given `destination` is an existing directory,
    # otherwise use `destination` directly
    if destination.is_dir():
        # TODO: get filename from Content-Disposition header, if available.
        from urllib.parse import urlsplit, urlunsplit
        parsed_url = urlsplit(url)
        local_filename = Path(parsed_url.path).name or 'download.tmp'
        local_fpath = destination / local_filename
    else:
        local_fpath = destination

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

def install(src_file: Path, dest_dir: Path):
    """Extracts/moves package at `src_file` to `dest_dir`"""

    import zipfile

    log = logging.getLogger('%s.install' % __name__)
    log.error("Starting installation")

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
            backups.append(utils.InplaceBackup(conflict))

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
        backup = None

        if dest_file.exists():
            backup = utils.InplaceBackup(dest_file)

        try:
            shutil.copyfile(str(src_file), str(dest_file))
        except Exception as err:
            backup.restore()
            raise exceptions.InstallException("Failed to copy file to '%s': %s" % (dest_dir, err)) from err

        if backup:
            backup.remove()

    log.debug("Installation succeeded")
