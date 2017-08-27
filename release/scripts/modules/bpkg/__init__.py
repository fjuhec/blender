# __all__ = (
#     "exceptions",
#     "types",
# )

from . import utils
from . import types
from pathlib import Path
from collections import OrderedDict
import logging

_tag_reindex = True
_packages = {}

def get_repo_storage_path() -> Path:
    """Return Path to the directory in which downloaded repository indices are
    stored"""
    import bpy
    return Path(bpy.utils.user_resource('CONFIG', 'repositories'))

def get_repositories() -> list:
    """
    Get list of downloaded repositories and update wm.package_repositories
    """
    log = logging.getLogger(__name__ + ".get_repositories")
    storage_path = get_repo_storage_path()
    repos = utils.load_repositories(storage_path)
    return repos

def get_installed_packages(refresh=False) -> list:
    """Get list of packages installed on disk"""
    import addon_utils
    installed_pkgs = []
    #TODO: just use addon_utils for now
    for mod in addon_utils.modules(refresh=refresh):
        pkg = types.Package.from_module(mod)
        pkg.installed = True
        installed_pkgs.append(pkg)
    return installed_pkgs

def _build_packagelist() -> dict: # {{{
    """Return a dict of ConsolidatedPackages from known repositories and
    installed packages, keyed by package name"""

    log = logging.getLogger(__name__ + "._build_packagelist")

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
                masterlist[pkg.name] = types.ConsolidatedPackage(pkg)

    for pkg in installed_packages:
        if pkg.name in masterlist:
            masterlist[pkg.name].add_version(pkg)
        else:
            masterlist[pkg.name] = types.ConsolidatedPackage(pkg)

    return masterlist
# }}}

def tag_reindex():
    """Set flag for rebuilding package list"""
    global _tag_reindex
    _tag_reindex = True

def list_packages():
    """Return same dict as _build_packagelist, but only re-build it when tag_reindex == True"""
    global _packages
    global _tag_reindex
    if _tag_reindex:
        _packages = _build_packagelist()
        print("rebuilding")
        _tag_reindex = False

    return _packages
