# A global storage space for display related stuff which needs to be accessible to operators

# list of names of packages currently displayed (matching filters)
displayed_packages = []
# list of names of packages currently expanded
expanded_packages = []
# name of package who's preferences are shown
preference_package = None


#errors
pkg_errors = []
# def pkg_error(msg: str):
#     """Add a package related error message"""
#     _pkg_errors.append(msg)
#
# def pkg_errors() -> str:
#     """Return list of error messages related to packages"""
#     return _pkg_errors
