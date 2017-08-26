# __all__ = (
#     "exceptions",
#     "types",
# )

from . import utils
from .types import (
    Package,
    ConsolidatedPackage,
    Repository,
    )
from pathlib import Path
from collections import OrderedDict
import logging
import bpy

packages = {}

def get_installed_packages(refresh=False) -> list:
    """Get list of packages installed on disk"""
    import addon_utils
    installed_pkgs = []
    for mod in addon_utils.modules(refresh=refresh):
        pkg = Package.from_module(mod)
        pkg.installed = True
        installed_pkgs.append(pkg)
    return installed_pkgs

def get_repo_storage_path() -> Path:
    return Path(bpy.utils.user_resource('CONFIG', 'repositories'))

def get_repositories() -> list:
    """
    Get list of downloaded repositories and update wm.package_repositories
    """
    log = logging.getLogger(__name__ + ".get_repositories")
    storage_path = get_repo_storage_path()
    repos = utils.load_repositories(storage_path)
    log.debug("repos: %s", repos)

    return repos


def list_packages() -> OrderedDict: # {{{
    """Make an OrderedDict of ConsolidatedPackages from known repositories +
    installed packages, keyed by package name"""

    # log = logging.getLogger(__name__ + ".build_composite_packagelist")
    masterlist = {}
    installed_packages = get_installed_packages(refresh=True)
    known_repositories = get_repositories()

    for repo in known_repositories:
        for pkg in repo.packages:
            pkg.repositories.add(repo)
            if pkg.name is None:
                return OrderedDict()
            if pkg.name in masterlist:
                masterlist[pkg.name].add_version(pkg)
            else:
                masterlist[pkg.name] = ConsolidatedPackage(pkg)

    for pkg in installed_packages:
        if pkg.name in masterlist:
            masterlist[pkg.name].add_version(pkg)
        else:
            masterlist[pkg.name] = ConsolidatedPackage(pkg)

    # log.debug(masterlist[None].__dict__)
    return OrderedDict(sorted(masterlist.items()))
# }}}
