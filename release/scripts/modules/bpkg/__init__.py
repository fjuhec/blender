# __all__ = (
#     "exceptions",
#     "types",
# )

from . import utils
from . import types
from pathlib import Path
from collections import OrderedDict
import logging

tag_reindex = True
packages = {}

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

def _build_packagelist() -> dict: # {{{
    """Return a dict of ConsolidatedPackages from known repositories and
    installed packages, keyed by package name"""

    log = logging.getLogger(__name__ + "._build_packagelist")

    def get_installed_packages(refresh=False) -> list:
        """Get list of packages installed on disk"""
        import addon_utils
        installed_pkgs = []
        for mod in addon_utils.modules(refresh=refresh):
            pkg = types.Package.from_module(mod)
            pkg.installed = True
            installed_pkgs.append(pkg)
        return installed_pkgs

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


def list_packages():
    """Return same dict as _build_packagelist, but only re-build it when tag_reindex == True"""
    global packages
    global tag_reindex
    if tag_reindex:
        packages = _build_packagelist()
        tag_reindex = False

    return packages
