import logging
import json
from pathlib import Path
from . import exceptions
from . import utils
from . import actions
from . import display

class Package:
    """
    Stores package methods and metadata
    """

    log = logging.getLogger(__name__ + ".Package")

    def __init__(self):
        self._bl_info = dict()

        ## bl_infos ##
        # required fields
        self.name = str()
        self.version = tuple()
        self.blender = tuple()
        # optional fields
        self.description = str()
        self.author = str()
        self.category = str()
        self.location = str()
        self.support = 'COMMUNITY'
        self.warning = str()
        self.wiki_url = str()
        self.tracker_url = str()

        ## package stuff ##
        self.url     = str()
        self.files   = list()

        ## package stuff which is not stored in repo ##
        self.installed = False
        # contains Path() when not None
        self.installed_location = None
        self.is_user = False
        self.enabled = False
        self.repositories = set()

        ## other ##
        # contains str() when not None
        self.module_name = None

    def set_from_dict(self, package_dict: dict):
        """
        Get attributes from a dict such as produced by `to_dict`
        """
        if package_dict is None:
            raise PackageException("Can't set package from None")
        
        self.files   = package_dict['files']
        self.url     = package_dict['url']
        self.bl_info = package_dict['bl_info']

    @classmethod
    def from_dict(cls, package_dict: dict):
        """
        Return a Package with values from dict
        Used to read the package from json format
        """
        pkg = cls()
        pkg.set_from_dict(package_dict)
        return pkg

    @classmethod
    def from_blinfo(cls, blinfo: dict):
        """
        Return a Package with bl_info filled in
        """
        return cls.from_dict({'bl_info': blinfo})

    @classmethod
    def from_module(cls, module):
        """
        Return a Package object from an addon module
        """
        from pathlib import Path
        filepath = Path(module.__file__)
        if filepath.name == '__init__.py':
            filepath = filepath.parent

        pkg = cls()
        pkg.files = [filepath.name]
        pkg.installed_location = str(filepath)
        pkg.module_name = module.__name__

        try:
            pkg.bl_info = module.bl_info
        except AttributeError as err:
            raise exceptions.PackageException("Module does not appear to be an addon; no bl_info attribute") from err
        return pkg

    def to_dict(self) -> dict:
        """
        Return a dict representation of the package
        Used to store the package in json format
        """
        return {
                'bl_info': self.bl_info,
                'url': self.url,
                'files': self.files,
                }

    import typing
    # bl_info properties 
    # required fields
    @property
    def name(self) -> typing.Optional[str]:
        """Get name from bl_info"""
        return self._bl_info.get('name')
    @name.setter
    def name(self, name:str) -> typing.Optional[str]:
        if not isinstance(name, str):
            raise exceptions.PackageException("refusing to set name to non str %r" % name)
        self._bl_info['name'] = name

    @property
    def version(self) -> typing.Optional[tuple]:
        """Get version from bl_info"""
        return tuple(self._bl_info.get('version'))
    @version.setter
    def version(self, version:tuple) -> typing.Optional[tuple]:
        if isinstance(version, str):
            raise exceptions.PackageException("Refusing to set version to non tuple %r" % version)
        self._bl_info['version'] = version

    @property
    def blender(self) -> typing.Optional[tuple]:
        """Get blender from bl_info"""
        return self._bl_info.get('blender')
    @blender.setter
    def blender(self, blender:tuple):
        if isinstance(blender, str):
            raise exceptions.PackageException("Refusing to set blender to non tuple %r" % blender)
        self._bl_info['blender'] = blender

    # optional fields
    @property
    def description(self) -> typing.Optional[str]:
        """Get description from bl_info"""
        return self._bl_info.get('description')
    @description.setter
    def description(self, description:str):
        self._bl_info['description'] = description

    @property
    def author(self) -> typing.Optional[str]:
        """Get author from bl_info"""
        return self._bl_info.get('author')
    @author.setter
    def author(self, author:str):
        self._bl_info['author'] = author

    @property
    def category(self) -> typing.Optional[str]:
        """Get category from bl_info"""
        return self._bl_info.get('category')
    @category.setter
    def category(self, category:str):
        self._bl_info['category'] = category

    @property
    def location(self) -> typing.Optional[str]:
        """Get location from bl_info"""
        return self._bl_info.get('location')
    @location.setter
    def location(self, location:str):
        self._bl_info['location'] = location

    @property
    def support(self) -> typing.Optional[str]:
        """Get support from bl_info"""
        return self._bl_info.get('support')
    @support.setter
    def support(self, support:str):
        self._bl_info['support'] = support

    @property
    def warning(self) -> typing.Optional[str]:
        """Get warning from bl_info"""
        return self._bl_info.get('warning')
    @warning.setter
    def warning(self, warning:str):
        self._bl_info['warning'] = warning

    @property
    def wiki_url(self) -> typing.Optional[str]:
        """Get wiki_url from bl_info"""
        return self._bl_info.get('wiki_url')
    @wiki_url.setter
    def wiki_url(self, wiki_url:str):
        self._bl_info['wiki_url'] = wiki_url

    @property
    def tracker_url(self) -> typing.Optional[str]:
        """Get tracker_url from bl_info"""
        return self._bl_info.get('tracker_url')
    @tracker_url.setter
    def tracker_url(self, tracker_url:str):
        self._bl_info['tracker_url'] = tracker_url
    

    # useful for handling whole bl_info at once
    @property
    def bl_info(self) -> dict:
        """bl_info dict of package"""
        return {
            "name": self.name,
            "version": self.version,
            "blender": self.blender,
            "description": self.description,
            "author": self.author,
            "category": self.category,
            "location": self.location,
            "support": self.support,
            "warning": self.warning,
            "wiki_url": self.wiki_url,
            "tracker_url": self.tracker_url,
        }
    @bl_info.setter
    def bl_info(self, blinfo: dict):
        self.name        = blinfo["name"]
        self.version     = blinfo["version"]
        self.blender     = blinfo["blender"]

        self.description = blinfo.get("description", self.description)
        self.author      = blinfo.get("author",      self.author)
        self.category    = blinfo.get("category",    self.category)
        self.location    = blinfo.get("location",    self.location)
        self.support     = blinfo.get("support",     self.support)
        self.warning     = blinfo.get("warning",     self.warning)
        self.wiki_url    = blinfo.get("wiki_url",    self.wiki_url)
        self.tracker_url = blinfo.get("tracker_url", self.tracker_url)

    def test_is_user(self) -> bool:
        """Return true if package's install location is in user or preferences scripts path"""
        import bpy
        user_script_path = bpy.utils.script_path_user()
        prefs_script_path = bpy.utils.script_path_pref()

        if user_script_path is not None:
            in_user = Path(user_script_path) in Path(self.installed_location).parents
        else:
            in_user = False

        if prefs_script_path is not None:
            in_prefs = Path(prefs_script_path) in Path(self.installed_location).parents
        else:
            in_prefs = False

        return in_user or in_prefs

    def test_enabled(self) -> bool:
        """Return true if package is enabled"""
        import bpy
        if self.module_name is not None:
            return (self.module_name in bpy.context.user_preferences.addons)
        else:
            return False
    
    def enable(self):
        """Enable package"""
        # TODO: just use addon_utils for now
        if not self.module_name:
            raise PackageException("Cannot enable package with unset module_name")
        import addon_utils
        addon_utils.enable(self.module_name, default_set=True)
        self.enabled = True

    def disable(self):
        """Disable package"""
        if not self.module_name:
            raise PackageException("Cannot disable package with unset module_name")
        import addon_utils
        addon_utils.enable(self.module_name, default_set=True)
        self.enabled = False


    def test_installed(self) -> bool:
        """Return true if package is installed"""
        import addon_utils
        return len([Package.from_module(mod) for mod in addon_utils.modules(refresh=False) if
                addon_utils.module_bl_info(mod)['name'] == self.name and
                addon_utils.module_bl_info(mod)['version'] == self.version]) > 0

    def set_installed_metadata(self, installed_pkg):
        """Sets metadata specific to installed packages from the Package given as `installed_pkg`"""
        self.installed = installed_pkg.test_installed()
        self.enabled = installed_pkg.test_enabled()
        self.is_user = installed_pkg.test_is_user()
        self.module_name = installed_pkg.module_name
        self.installed_location = installed_pkg.installed_location

    def download(self, dest: Path, progress_callback=None) -> Path:
        """Downloads package to `dest`"""

        if not self.url:
            raise ValueError("Cannot download package without a URL")
        
        return actions.download(self.url, dest, progress_callback)

    def install(self, dest_dir: Path, cache_dir: Path, progress_callback=None):
        """Downloads package to `cache_dir`, then extracts/moves package to `dest_dir`"""

        log = logging.getLogger('%s.install' % __name__)

        downloaded = self.download(cache_dir, progress_callback)

        if not downloaded:
            log.debug('Download returned None, not going to install anything.')
            return

        actions.install(downloaded, dest_dir)
        utils.rm(downloaded)

    def __eq__(self, other):
        return self.name == other.name and self.version == other.version

    def __lt__(self, other):
        return self.version < other.version

    def __hash__(self):
        return hash((self.name, self.version))

    def __repr__(self) -> str:
        # return self.name
        return "Package('name': {}, 'version': {})".format(self.name, self.version)

class ConsolidatedPackage:
    """
    Stores a grouping of different versions of the same package
    """

    log = logging.getLogger(__name__ + ".ConsolidatedPackage")

    def __init__(self, pkg=None):
        self.versions = []
        # self.updateable = False

        if pkg is not None:
            self.add_version(pkg)

    @property
    def installed(self) -> bool:
        """Return true if any version of this package is installed"""
        for pkg in self.versions:
            if pkg.installed:
                return True
        return False

    @property
    def name(self) -> str:
        """
        Return name of this package. All package versions in a
        ConsolidatedPackage should have the same name by definition

        Returns None if there are no versions
        """
        try:
            return self.versions[0].name
        except IndexError:
            return None

    def get_latest_installed_version(self) -> Package:
        """
        Return the installed package with the highest version number.
        If no packages are installed, return None.
        """
        #self.versions is always sorted newer -> older, so we can just grab the first we find
        for pkg in self.versions:
            if pkg.installed:
                return pkg 
        return None

    def get_latest_version(self) -> Package:
        """Return package with highest version number, returns None if there are no versions"""
        try:
            return self.versions[0] # this is always sorted with the highest on top
        except IndexError:
            return None

    def get_display_version(self) -> Package:
        """
        Return installed package with highest version number.
        If no version is installed, return highest uninstalled version.
        """
        pkg = self.get_latest_installed_version()
        if pkg is None:
            pkg = self.get_latest_version()
        return pkg

    def test_updateable(self) -> bool:
        """Return true if latest installed version of package is older than latest known version"""
        latest = self.get_latest_version()
        latest_installed = self.get_latest_installed_version()
        if latest is None or latest_installed is None:
            return False
        return latest_installed.version < latest.version

    def add_version(self, newpkg: Package):
        """Adds a package to the collection of versions"""

        if self.name and newpkg.name != self.name:
            raise exceptions.PackageException("Name mismatch, refusing to add %s to %s" % (newpkg, self))

        for pkg in self:
            if pkg == newpkg:
                pkg.repositories.union(newpkg.repositories)
                if newpkg.installed:
                    pkg.set_installed_metadata(newpkg)
                return

        self.versions.append(newpkg)
        self.versions.sort(key=lambda v: v.version, reverse=True)
        # self.updateable = self.test_updateable()


    def __iter__(self):
        return (pkg for pkg in self.versions)

    def __repr__(self):
        return ("ConsolidatedPackage<name={}>".format(self.name))

class Repository:
    """
    Stores repository metadata (including packages)
    """

    log = logging.getLogger(__name__ + ".Repository")

    def __init__(self, url=None):
        self.name = str()
        self.url = url if url is not None else str()
        self.packages = list()
        self.filepath = Path()
        self._headers = dict()

    def refresh(self, storage_path: Path, progress_callback=None):
        """
        Requests repo.json from URL and embeds etag/last-modification headers
        """
        import requests

        if progress_callback is None:
            progress_callback = lambda x: None

        progress_callback(0.0)

        if self.url is None:
            raise ValueError("Cannot refresh repository without a URL")

        url = utils.add_repojson_to_url(self.url)

        self.log.debug("Refreshing repository from %s", self.url)

        req_headers = {}
        # Do things this way to avoid adding empty objects/None to the req_headers dict
        try:
            req_headers['If-None-Match'] = self._headers['etag']
        except KeyError:
            pass
        try:
            req_headers['If-Modified-Since'] = self._headers['last-modified']
        except KeyError:
            pass

        try:
            resp = requests.get(url, headers=req_headers, timeout=60)
        except requests.exceptions.InvalidSchema as err:
            raise exceptions.DownloadException("Invalid schema. Did you mean to use http://?") from err
        except requests.exceptions.ConnectionError as err:
            raise exceptions.DownloadException("Failed to connect. Are you sure '%s' is the correct URL?" % url) from err
        except requests.exceptions.RequestException as err:
            raise exceptions.DownloadException(err) from err

        try:
            resp.raise_for_status()
        except requests.HTTPError as err:
            self.log.error('Error downloading %s: %s', url, err)
            raise exceptions.DownloadException(resp.status_code, resp.reason) from err

        if resp.status_code == requests.codes.not_modified:
            self.log.debug("Packagelist not modified")
            progress_callback(1.0)
            return

        resp_headers = {}
        try:
            resp_headers['etag'] = resp.headers['etag']
        except KeyError:
            pass
        try:
            resp_headers['last-modified'] = resp.headers['last-modified']
        except KeyError:
            pass

        self.log.debug("Found headers: %s", resp_headers)

        progress_callback(0.7)
        
        try:
            repodict = resp.json()
        except json.decoder.JSONDecodeError:
            self.log.exception("Failed to parse downloaded repository")
            raise exceptions.BadRepositoryException(
                    "Could not parse repository downloaded from '%s'. Are you sure this is the correct URL?" % url
                    )
        repodict['_headers'] = resp_headers
        repodict['url'] = self.url

        self.set_from_dict(repodict)
        self.to_file(storage_path / utils.format_filename(self.name, ".json"))

        progress_callback(1.0)

    def to_dict(self, sort=False, ids=False) -> dict:
        """
        Return a dict representation of the repository
        Used to store the repository in json format
        """
        packages = [p.to_dict() for p in self.packages]

        if sort:
            packages.sort(key=lambda p: p['bl_info']['name'].lower())

        if ids:
            for pkg in packages:
                # hash may be too big for a C int
                pkg['id'] = str(hash(pkg['url'] + pkg['bl_info']['name'] + self.name + self.url))

        return {
                'name':     self.name,
                'packages': packages,
                'url':      self.url,
                '_headers': self._headers,
                }

    def set_from_dict(self, repodict: dict):
        """
        Get repository attributes from a dict such as produced by `to_dict`
        Used to read the repository from json format
        """

        try:
            name      = repodict['name']
        except KeyError as err:
            raise exceptions.BadRepositoryException("Cannot set repository from dict; missing name") from err
        try:
            url       = repodict['url']
        except KeyError as err:
            raise exceptions.BadRepositoryException("Cannot set repository from dict; missing url") from err
        try:
            pkg_dicts = repodict['packages']
        except KeyError as err:
            raise exceptions.BadRepositoryException("Cannot set repository from dict; missing packages") from err
        headers = repodict.get('_headers', {})

        self.name = name
        self.url = url
        for pkg_dict in pkg_dicts:
            try:
                pkg = Package.from_dict(pkg_dict)
            except exceptions.PackageException as err:
                msg = "Error parsing package {} in repository {}: {}".format(pkg_dict['bl_info'].get('name'), self.name, err)
                display.pkg_errors.append(msg)
            else:
                self.add_package(pkg)
        self._headers = headers

    @classmethod
    def from_dict(cls, repodict: dict):
        """
        Like `set_from_dict`, but immutable
        """
        repo = cls()
        repo.set_from_dict(repodict)
        return repo

    def to_file(self, path: Path):
        """
        Dump repository to a json file at `path`.
        """
        if len(self.packages) <= 0:
            self.log.warning("Writing an empty repository")

        self.log.debug("URL is %s", self.url)

        with path.open('w', encoding='utf-8') as repo_file:
            json.dump(self.to_dict(), repo_file, indent=4, sort_keys=True)
        self.log.debug("Repository written to %s" % path)

    @classmethod
    def from_file(cls, path: Path):
        """
        Read repository from a json file at `path`.
        """
        repo_file = path.open('r', encoding='utf-8')

        with repo_file:
            try:
                repo = cls.from_dict(json.load(repo_file))
            except json.JSONDecodeError as err:
                raise exceptions.BadRepositoryException(err) from err
            if repo.url is None or len(repo.url) == 0:
                raise exceptions.BadRepositoryException("Repository missing URL")

        repo.filepath = path
        cls.log.debug("Repository read from %s", path)
        return repo

    def add_package(self, pkg:Package):
        """Add package to repository instance"""
        #TODO: check if package exists
        self.packages.append(pkg)

    def __repr__(self):
        return "Repository({}, {})".format(self.name, self.url)
