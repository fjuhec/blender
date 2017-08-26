class BpkgException(Exception):
    """Superclass for all package manager exceptions"""

class InstallException(BpkgException):
    """Raised when there is an error during installation"""

class DownloadException(BpkgException):
    """Raised when there is an error downloading something"""

class BadRepositoryException(BpkgException):
    """Raised when there is an error while reading or manipulating a repository"""

class PackageException(BpkgException):
    """Raised when there is an error while manipulating a package"""
