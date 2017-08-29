# A global storage space for display related stuff which needs to be accessible to operators

# list of names of packages currently displayed (matching filters)
displayed_packages = []
# list of names of packages currently expanded
expanded_packages = []
# name of package who's preferences are shown
preference_package = None

def repository_items(self, context) -> list:
    """Return displayed repository enum items"""
    import bpy
    try:
        repos = context.window_manager['package_repositories']
    except KeyError:
        return []
    repolist = []
    for repo in repos:
        try:
            repolist.append((repo['name'], repo['name'], "{} ({})".format(repo['name'], repo['url'])))
        except KeyError: # name may not be set before refresh() finishes execution, in which case leave it out
            pass
    return repolist

#errors
pkg_errors = []
# def pkg_error(msg: str):
#     """Add a package related error message"""
#     _pkg_errors.append(msg)
#
# def pkg_errors() -> str:
#     """Return list of error messages related to packages"""
#     return _pkg_errors
