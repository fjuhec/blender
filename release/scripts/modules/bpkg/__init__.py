# __all__ = (
#     "exceptions",
#     "types",
# )

from . import utils
from . import types
from . import display
from . import exceptions
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

def refresh_repository_props():
    """Create RepositoryProperty collection from repository files"""
    #TODO: store repository props in .blend so enabled/disabled state can be remembered
    import bpy
    wm = bpy.context.window_manager
    repos = get_repositories()
    wm.package_repositories.clear()
    for repo in repos:
        repo_prop = wm.package_repositories.add()
        repo_prop.name = repo.name
        repo_prop.enabled = True  
        repo_prop.url = repo.url
        repo_prop.file = repo.file

def get_installed_packages(refresh=False) -> list:
    """Get list of packages installed on disk"""
    log = logging.getLogger(__name__ + ".get_installed_packages")
    import addon_utils
    installed_pkgs = []
    #TODO: just use addon_utils for now
    for mod in addon_utils.modules(refresh=refresh):
        try:
            pkg = types.Package.from_module(mod)
        except exceptions.PackageException as err:
            msg = "Error parsing package \"{}\" ({}): {}".format(mod.__name__, mod.__file__, err)
            display.pkg_errors.append(msg)
        else:
            pkg.installed = True
            installed_pkgs.append(pkg)
    return installed_pkgs

def _build_packagelist() -> dict: # {{{
    """Return a dict of ConsolidatedPackages from known repositories and
    installed packages, keyed by package name"""

    log = logging.getLogger(__name__ + "._build_packagelist")

    masterlist = {}
    display.pkg_errors.clear()
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
        _tag_reindex = False

    return _packages
