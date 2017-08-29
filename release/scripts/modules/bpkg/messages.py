from .types import Repository

class Message:
    """Superclass for all message sent over pipes."""


# Blender messages

class BlenderMessage(Message):
    """Superclass for all messages sent from Blender to the subprocess."""

class Abort(BlenderMessage):
    """Sent when the user requests abortion of a task."""


# Subproc messages

class SubprocMessage(Message):
    """Superclass for all messages sent from the subprocess to Blender."""

class Progress(SubprocMessage):
    """Send from subprocess to Blender to report progress.

    :ivar progress: the progress percentage, from 0-1.
    """

    def __init__(self, progress: float):
        self.progress = progress

class Success(SubprocMessage):
    """Sent when an operation finished sucessfully."""

class RepositoryResult(SubprocMessage):
    """Sent when an operation returns a repository to be used on the parent process."""

    def __init__(self, repository_name: str):
        self.repository = repository

class Aborted(SubprocMessage):
    """Sent as response to Abort message."""

# subproc errors

class SubprocError(SubprocMessage):
    """Superclass for all fatal error messages sent from the subprocess."""

    def __init__(self, message: str):
        self.message = message

class InstallError(SubprocError):
    """Sent when there was an error installing something."""

class UninstallError(SubprocError):
    """Sent when there was an error uninstalling something."""

class BadRepositoryError(SubprocError):
    """Sent when a repository can't be used for some reason"""

class DownloadError(SubprocError):
    """Sent when there was an error downloading something."""

    def __init__(self, message: str, status_code: int = None):
        self.status_code = status_code
        self.message = message

