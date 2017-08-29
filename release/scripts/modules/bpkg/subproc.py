"""Functions to be executed in a subprocess"""

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

    def prog(p: float) -> float:
        pipe_to_blender.send(messages.Progress(p))

    from . import cache
    cache_dir = cache.cache_directory('downloads')

    try:
        package.install(install_path, cache_dir, progress_callback=prog)
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
    files_to_remove = [install_path / Path(p) for p in package.files]

    for pkgfile in files_to_remove:
        if not pkgfile.exists():
            pipe_to_blender.send(messages.UninstallError("Could not find file owned by package: '%s'. Refusing to uninstall." % pkgfile))
            return

    try:
        for pkgfile in files_to_remove:
            utils.rm(pkgfile)
    except Exception as err:
        pipe_to_blender.send(messages.UninstallError(err))
        log.exception(err)
        return

    pipe_to_blender.send(messages.Success())


def refresh_repositories(pipe_to_blender, repo_storage_path: Path, repository_urls: str):
    """Downloads and stores the given repository"""

    log = logging.getLogger(__name__ + '.refresh_repository')

    def progress_callback(p: float) -> float:
        progress_callback._progress += p
        pipe_to_blender.send(messages.Progress(progress_callback._progress))
    progress_callback._progress = 0.0

    repos = utils.load_repositories(repo_storage_path)

    def prog(p: float):
        progress_callback(p/len(repos))

    known_repo_urls = [repo.url for repo in repos]
    for repo_url in repository_urls:
        if repo_url not in known_repo_urls:
            repos.append(Repository(repo_url))

    for repo in repos:
        try:
            repo.refresh(repo_storage_path, progress_callback=prog)
        except exceptions.DownloadException as err:
            pipe_to_blender.send(messages.DownloadError(err))
            log.exception("Download error")
        except exceptions.BadRepositoryException as err:
            pipe_to_blender.send(messages.BadRepositoryError(err))
            log.exception("Bad repository")

    pipe_to_blender.send(messages.Success())

