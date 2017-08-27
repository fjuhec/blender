"""
All the stuff that needs to run in a subprocess.
"""

from pathlib import Path
from . import (
    messages,
    exceptions, 
    utils,
)
from .types import (
    Package,
    Repository,
)
import logging

def download_and_install_package(pipe_to_blender, package: Package, install_path: Path):
    """Downloads and installs the given package."""

    log = logging.getLogger(__name__ + '.download_and_install')

    from . import cache
    cache_dir = cache.cache_directory('downloads')

    try:
        package.install(install_path, cache_dir)
    except exceptions.DownloadException as err:
        pipe_to_blender.send(messages.DownloadError(err))
        log.exception(err)
        return
    except exceptions.InstallException as err:
        pipe_to_blender.send(messages.InstallError(err))
        log.exception(err)
        return

    pipe_to_blender.send(messages.Success())


def uninstall_package(pipe_to_blender, package: Package, install_path: Path):
    """Deletes the given package's files from the install directory"""
    #TODO: move package to cache and present an "undo" button to user, to give nicer UX on misclicks

    log = logging.getLogger(__name__ + ".uninstall_package")

    for pkgfile in [install_path / Path(p) for p in package.files]:
        if not pkgfile.exists():
            pipe_to_blender.send(messages.UninstallError("Could not find file owned by package: '%s'. Refusing to uninstall." % pkgfile))
            return None

    try:
        for pkgfile in [install_path / Path(p) for p in package.files]:
            utils.rm(pkgfile)
    except Exception as err:
        pipe_to_blender.send(messages.UninstallError(err))
        log.exception(err)
        return

    pipe_to_blender.send(messages.Success())


def refresh_repositories(pipe_to_blender, repo_storage_path: Path, repository_urls: str, progress_callback=None):
    """Downloads and stores the given repository"""

    log = logging.getLogger(__name__ + '.refresh_repository')

    if progress_callback is None:
        progress_callback = lambda x: None
    progress_callback(0.0)

    repos = utils.load_repositories(repo_storage_path)

    def prog(progress: float):
        progress_callback(progress/len(repos))

    known_repo_urls = [repo.url for repo in repos]
    for repo_url in repository_urls:
        if repo_url not in known_repo_urls:
            repos.append(Repository(repo_url))

    for repo in repos:
        log.debug("repo name: %s, url: %s", repo.name, repo.url)
    for repo in repos:
        try:
            repo.refresh(repo_storage_path, progress_callback=prog)
        except exceptions.DownloadException as err:
            pipe_to_blender.send(messages.DownloadError(err))
            log.exception("Download error")
        except exceptions.BadRepositoryException as err:
            pipe_to_blender.send(messages.BadRepositoryError(err))
            log.exception("Bad repository")

    progress_callback(1.0)
    pipe_to_blender.send(messages.Success())

